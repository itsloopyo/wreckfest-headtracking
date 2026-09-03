// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_state.h"

#include <windows.h>

#include <cstdint>

#include "builds/build_registry.h"
#include "logging.h"

#include "cameraunlock/memory/safe_memory.h"

namespace wf_ht {

namespace {

// Address of the module-static POINTER, not of what it points at: the engine
// fills it long after this mod loads and replaces it across a session, so every
// read below starts from the static.
std::uintptr_t g_view_manager_static = 0;

// Copied out of the active profile at init so the render path never looks a
// profile up. The active profile cannot change while the mod is running.
unsigned g_active_controller_offset = 0;

// Address of the engine's live-race-session byte, resolved at init.
std::uintptr_t g_race_session_flag = 0;

// The pause chain: the module-static pointer, and the two hops from it to the
// engine's global pause byte. The static is zero when the active profile has no
// pause offsets, which is the one case IsGamePaused answers without reading.
std::uintptr_t g_pause_state_static = 0;
unsigned g_pause_state_object = 0;
unsigned g_pause_state_paused = 0;

// One camera controller class the view manager can make active, with its vtable
// resolved to a running address. Identity by vtable pointer rather than by RTTI
// name: the comparison is what the render path can afford, and a vtable address
// is exactly as specific as the class.
struct ControllerClass {
    std::uintptr_t vtable;
    bool is_gameplay;
    const char* name;
};

// GarageCamera is the menus and the garage. The rest are the race: the car
// camera the player drives with, the trackside and scripted cameras replays cut
// to, the free camera, and the IngameCamera base they all derive from. A
// controller matching none of them is not assumed to be either.
constexpr unsigned kControllerClassCount = 6;
ControllerClass g_controllers[kControllerClassCount];

// How far into the view manager the unknown-controller diagnostic looks for a
// slot that does hold one. The object is small and the scan runs at most once,
// on a path that has already given up.
constexpr unsigned kViewManagerScanBytes = 0x100;

// A chain that faults says so once and then stays quiet. These reads are on the
// render path and a chain that faults tends to fault every frame, so a line per
// fault is a log nobody can read - while saying nothing at all leaves the one
// failure that silently disables the gate with no trace in the log. One latch
// per chain so a noisy chain cannot hide a quieter one. Only the render thread
// touches them.
struct ChainFault {
    const char* name;
    // What the mod does while this chain is down. The two chains answer a fault
    // in opposite directions, so one shared sentence would be wrong for one of
    // them - and this line is the whole of what a bug report has to go on.
    const char* consequence;
    bool logged;
};

ChainFault g_view_chain{"view manager",
                        "the gameplay gate reads unknown and head tracking stays off", false};
ChainFault g_pause_chain{"pause state",
                         "the gate cannot see the pause menu and head tracking keeps "
                         "following through it", false};

// Latched separately again, and for the same reason: a controller the profile
// does not recognise is not a faulting chain, it is a profile that has fallen
// behind the game.
bool g_logged_unknown_controller = false;

// Which controller class the gate last saw, so the log carries the class the
// view actually changed to rather than only the state it mapped to.
const char* g_last_controller_name = nullptr;

void NoteFault(ChainFault& chain) {
    if (chain.logged) return;
    chain.logged = true;
    Log::Line("[state] the %s pointer chain faulted - %s while it does.",
              chain.name, chain.consequence);
}

// Reads a pointer-sized field, treating a null or unreadable one as "no
// answer". Every engine read in this file is this shape.
bool ReadPointer(std::uintptr_t address, std::uintptr_t& out, ChainFault& chain) {
    std::uintptr_t value = 0;
    if (!cameraunlock::memory::SafeRead(address, value)) {
        NoteFault(chain);
        return false;
    }
    out = value;
    return value != 0;
}

// Reads the vtable pointer out of an object and matches it against the classes
// the profile pins. Null when the object is unreadable or is not one of them.
const ControllerClass* ClassifyController(std::uintptr_t object) {
    std::uintptr_t vtable = 0;
    if (!cameraunlock::memory::SafeRead(object, vtable)) return nullptr;
    for (const ControllerClass& candidate : g_controllers) {
        if (candidate.vtable == vtable) return &candidate;
    }
    return nullptr;
}

// One line, once, and only when the pinned slot did not hold a camera
// controller at all. The active-controller offset is the least corroborated
// number in the profile and a wrong one leaves the gate shut forever with
// nothing in the log to say why, so this walks the view manager's own bytes and
// names the slot that does hold a controller. That is the whole fix, in the log
// line that reports the fault.
void LogUnknownController(std::uintptr_t view_manager, std::uintptr_t controller) {
    if (g_logged_unknown_controller) return;
    g_logged_unknown_controller = true;

    std::uintptr_t vtable = 0;
    cameraunlock::memory::SafeRead(controller, vtable);
    Log::Line("[state] view manager +0x%X holds an object whose vtable is 0x%p - not a camera "
              "controller this profile knows. The gate reads unknown and head tracking stays off.",
              g_active_controller_offset, reinterpret_cast<void*>(vtable));

    for (unsigned offset = 0; offset < kViewManagerScanBytes; offset += sizeof(std::uintptr_t)) {
        std::uintptr_t candidate = 0;
        if (!cameraunlock::memory::SafeRead(view_manager + offset, candidate) || candidate == 0) {
            continue;
        }
        const ControllerClass* found = ClassifyController(candidate);
        if (found != nullptr) {
            Log::Line("[state]   view manager +0x%X holds a %s - "
                      "view_manager_active_controller may belong there.", offset, found->name);
        }
    }
}

void LogControllerChange(const ControllerClass& active) {
    if (g_last_controller_name == active.name) return;
    g_last_controller_name = active.name;
    Log::Line("[state] active camera controller is %s", active.name);
}

}  // namespace

const char* ViewStateName(ViewState state) {
    switch (state) {
        case ViewState::Unknown:  return "unknown";
        case ViewState::NoView:   return "no view";
        case ViewState::Menu:     return "menu";
        case ViewState::Gameplay: return "gameplay";
    }
    return "unknown";
}

bool InitGameState() {
    const builds::BuildProfile& profile = builds::ActiveProfile();
    const auto module_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));

    g_view_manager_static = module_base + profile.Offsets.view_manager_ptr_rva;
    g_active_controller_offset = profile.Offsets.view_manager_active_controller;
    g_race_session_flag = module_base + profile.Offsets.race_session_active_rva;

    // Zero means this profile never derived the chain, not that it lives at the
    // image base. Left at zero, IsGamePaused reads nothing and answers false.
    g_pause_state_static = profile.Offsets.pause_state_ptr_rva == 0
                         ? 0
                         : module_base + profile.Offsets.pause_state_ptr_rva;
    g_pause_state_object = profile.Offsets.pause_state_object;
    g_pause_state_paused = profile.Offsets.pause_state_paused;

    const ControllerClass classes[kControllerClassCount] = {
        { module_base + profile.Offsets.garage_camera_vtable_rva,           false, "GarageCamera" },
        { module_base + profile.Offsets.ingame_camera_vtable_rva,           true,  "IngameCamera" },
        { module_base + profile.Offsets.ingame_car_camera_vtable_rva,       true,  "IngameCarCamera" },
        { module_base + profile.Offsets.ingame_free_camera_vtable_rva,      true,  "IngameFreeCamera" },
        { module_base + profile.Offsets.ingame_trackside_camera_vtable_rva, true,  "IngameTrackSideCamera" },
        { module_base + profile.Offsets.ingame_animated_camera_vtable_rva,  true,  "IngameAnimatedCamera" },
    };
    for (unsigned i = 0; i < kControllerClassCount; ++i) g_controllers[i] = classes[i];

    // The static is data in the game's own image and is readable from the moment
    // it maps, whatever it happens to hold. A fault here means the RVA does not
    // land in the image at all, which is a profile that would take the gate down
    // every frame rather than once at boot.
    std::uintptr_t probe = 0;
    if (!cameraunlock::memory::SafeRead(g_view_manager_static, probe)) {
        Log::Line("[state] profile %s: the view manager static is not readable in this "
                  "image - the gameplay gate stays closed.", profile.Name);
        return false;
    }

    Log::Line("[state] view manager static 0x%p", reinterpret_cast<void*>(g_view_manager_static));
    if (g_pause_state_static == 0) {
        Log::Line("[state] profile %s has no pause chain - head tracking will keep following "
                  "through the pause menu on this build.", profile.Name);
    }
    return true;
}

ViewState ReadViewState() {
    std::uintptr_t view_manager = 0;
    if (!ReadPointer(g_view_manager_static, view_manager, g_view_chain)) {
        return ViewState::Unknown;
    }

    std::uintptr_t active_controller = 0;
    if (!cameraunlock::memory::SafeRead(view_manager + g_active_controller_offset,
                                        active_controller)) {
        NoteFault(g_view_chain);
        return ViewState::Unknown;
    }
    if (active_controller == 0) return ViewState::NoView;

    // Which class of controller owns the view is the whole of the gate. The
    // menus and the garage run a GarageCamera; a race runs one of the Ingame
    // family. Comparing the object's vtable pointer against the classes the
    // profile pins settles it in a handful of integer compares, with no RTTI
    // walk and no string compare on the render path.
    const ControllerClass* active = ClassifyController(active_controller);
    if (active == nullptr) {
        LogUnknownController(view_manager, active_controller);
        return ViewState::Unknown;
    }
    LogControllerChange(*active);
    return active->is_gameplay ? ViewState::Gameplay : ViewState::Menu;
}

bool IsGamePaused() {
    if (g_pause_state_static == 0) return false;

    std::uintptr_t owner = 0;
    if (!ReadPointer(g_pause_state_static, owner, g_pause_chain)) return false;

    std::uintptr_t state = 0;
    if (!ReadPointer(owner + g_pause_state_object, state, g_pause_chain)) return false;

    std::uint8_t paused = 0;
    if (!cameraunlock::memory::SafeRead(state + g_pause_state_paused, paused)) {
        NoteFault(g_pause_chain);
        return false;
    }
    return paused != 0;
}

bool IsRaceSessionLive() {
    std::uint8_t live = 0;
    if (!cameraunlock::memory::SafeRead(g_race_session_flag, live)) {
        NoteFault(g_view_chain);
        return false;
    }
    return live != 0;
}

bool ShouldFollowHead(bool tracking_enabled, ViewState view, bool race_session_live,
                      bool paused) {
    if (!tracking_enabled) return false;
    if (!race_session_live) return false;
    if (paused) return false;
    return view == ViewState::Gameplay;
}

}  // namespace wf_ht
