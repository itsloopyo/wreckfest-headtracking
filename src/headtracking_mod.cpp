// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <windows.h>

#include <array>
#include <cmath>
#include <atomic>
#include <string>
#include <thread>

#include "builds/build_registry.h"
#include "camera_hook.h"
#include "camera_transform.h"
#include "config.h"
#include "game_state.h"
#include "hotkey_names.h"
#include "logging.h"

#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/os/module_paths.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

namespace wf_ht {

namespace {

using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;
// Without IsRemoteConnection() on the receiver the session silently falls back
// to LocalSmoothing forever, with nothing at the call site to show it.
static_assert(Session::kHasRemoteConnection,
              "UdpReceiver must expose IsRemoteConnection() to select Local/RemoteSmoothing");

Config g_config;
cameraunlock::UdpReceiver g_receiver;
Session g_session(g_receiver);
cameraunlock::input::HotkeyPoller g_hotkeys;
cameraunlock::time::FrameClock g_frame_clock;

std::atomic<bool> g_tracking_enabled{false};
std::atomic<bool> g_active{false};

std::atomic<long long> g_frame_counter{0};

// Whether this module is pinned against unloading - see PinModule. Recorded at
// load and reported from the bootstrap, which is the first point there is a log
// to report it into.
std::atomic<bool> g_pinned{false};

// Enough of the engine's camera transform to confirm in a bug report that the
// hook fires and that the matrix still looks like a camera-to-world transform
// (row 3 holding world-scale translation) after a game patch. Bounded because
// this runs on the render path.
constexpr long long kDiagnosticFrames = 3;

// ---------------------------------------------------------------------------
// Config to pipeline
// ---------------------------------------------------------------------------

// Translation only, from the INI-backed Config into the core pipeline's own
// settings types. Both arguments are explicit rather than reaching for the
// file statics, so this reads as - and can be reasoned about as - a mapping
// with no other reach into the mod's state.
void ApplyConfigToPipeline(const Config& config, Session& session) {
    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw = config.yaw_sensitivity;
    sensitivity.pitch = config.pitch_sensitivity;
    sensitivity.roll = config.roll_sensitivity;
    sensitivity.invert_yaw = config.invert_yaw;
    sensitivity.invert_pitch = config.invert_pitch;
    sensitivity.invert_roll = config.invert_roll;
    session.GetProcessor().SetSensitivity(sensitivity);

    // One pair of values for rotation and position alike. The session owns them
    // and recomposes them onto whatever position settings it is handed, so the
    // two calls below compose in either order and no settings rebuild can drop
    // them. Which of the two is in effect is decided per connection from the
    // receiver's source-address check, so nothing here picks one.
    session.SetLocalSmoothing(config.local_smoothing);
    session.SetRemoteSmoothing(config.remote_smoothing);

    session.SetPositionSettings(cameraunlock::PositionSettings::Symmetric(
        config.position_sensitivity_x,
        config.position_sensitivity_y,
        config.position_sensitivity_z,
        config.limit_x, config.limit_y, config.limit_z, config.limit_z_back,
        config.local_smoothing, config.remote_smoothing,
        config.invert_position_x, config.invert_position_y, config.invert_position_z));

    session.SetMode(config.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                                            : cameraunlock::TrackingMode::RotationOnly);
}

// ---------------------------------------------------------------------------
// Render-thread diagnostics
//
// Every function here is edge-triggered or bounded. This is the render path,
// and a line per frame is a log nobody can read.
// ---------------------------------------------------------------------------

// The session re-reads the receiver's source-address check every update, so a
// player who switches from a local OpenTrack instance to a phone on WiFi
// mid-session gets the other smoothing parameter without restarting the game.
// This only records the switch, so a bug report can say which of the two values
// was actually in effect.
void LogConnectionLocality() {
    static bool last_remote = false;
    static bool known = false;

    const bool is_remote = g_session.IsRemoteConnection();
    if (known && is_remote == last_remote) return;
    last_remote = is_remote;
    known = true;

    Log::Line("[udp] tracker source is %s - smoothing=%.2f",
              is_remote ? "a remote device" : "on this machine",
              cameraunlock::math::GetEffectiveSmoothing(
                  g_config.local_smoothing, g_config.remote_smoothing, is_remote));
}

void LogGateChange(ViewState view, bool race_session, bool paused, bool following) {
    static ViewState last_view = ViewState::Unknown;
    static bool last_race_session = false;
    static bool last_paused = false;
    static bool known = false;

    if (known && view == last_view && race_session == last_race_session
        && paused == last_paused) {
        return;
    }
    last_view = view;
    last_race_session = race_session;
    last_paused = paused;
    known = true;

    Log::Line("[state] view is %s, race session %s, %s - head tracking %s",
              ViewStateName(view), race_session ? "live" : "not started",
              paused ? "paused" : "not paused", following ? "following" : "off");
}

void LogFirstFrames(long long frame, const float* transform, bool have_rotation,
                    const HeadPose& pose) {
    if (frame >= kDiagnosticFrames) return;

    Log::Line("[camera] frame %lld  tracker=%s  yaw=%.2f pitch=%.2f roll=%.2f  lean=%.3f %.3f %.3f",
              frame, have_rotation ? "yes" : "no", pose.yaw, pose.pitch, pose.roll,
              pose.lean_x, pose.lean_y, pose.lean_z);
    for (unsigned row = 0; row < kCameraTransformFloats / 4; ++row) {
        Log::Line("[camera]   m[%u] % 14.5f % 14.5f % 14.5f % 14.5f", row,
                  transform[row * 4 + 0], transform[row * 4 + 1],
                  transform[row * 4 + 2], transform[row * 4 + 3]);
    }
}

// Latched, and reported ahead of the gate. The diagnostic frames above are
// logged the moment the hook first runs, which is long before a tracker is
// usually connected, so without this nothing in the log ever says the head pose
// reached the camera. Behind the gate instead, one press of End - or one lap
// spent in the garage - would hide the evidence.
void LogFirstPoseReachingCamera(long long frame, bool have_rotation, const HeadPose& pose) {
    static std::atomic<bool> logged{false};
    if (!have_rotation) return;
    if (logged.exchange(true, std::memory_order_relaxed)) return;

    Log::Line("[camera] head pose reached the camera hook on frame %lld: "
              "yaw=%.2f pitch=%.2f roll=%.2f", frame, pose.yaw, pose.pitch, pose.roll);
}

// How far off centre a pose has to be before it counts as the player having
// deliberately moved, rather than the jitter a tracker emits sitting still.
constexpr float kDeliberateMovementDegrees = 5.0f;

// Once, for the first pose that both cleared the gate and was big enough to see.
// LogFirstPoseReachingCamera above fires on the first packet to arrive, which is
// normally a near-centred one - it says the data got here, not that the view
// ever turned. This is the line that separates "the tracker is not reaching the
// camera" from "the camera is not reaching the screen", which is the whole of
// triaging a report that head tracking does nothing.
void LogFirstComposedPose(const HeadPose& pose) {
    static bool logged = false;
    if (logged) return;
    if (std::fabs(pose.yaw) < kDeliberateMovementDegrees
        && std::fabs(pose.pitch) < kDeliberateMovementDegrees) {
        return;
    }
    logged = true;
    Log::Line("[camera] composed a head pose into the frame: yaw=%.2f pitch=%.2f roll=%.2f "
              "lean=%.3f %.3f %.3f", pose.yaw, pose.pitch, pose.roll,
              pose.lean_x, pose.lean_y, pose.lean_z);
}

// ---------------------------------------------------------------------------
// Hotkeys
// ---------------------------------------------------------------------------

void ToggleTracking() {
    const bool on = !g_tracking_enabled.load();
    g_tracking_enabled.store(on);
    Log::Line("[input] tracking %s", on ? "enabled" : "disabled");
}

void CycleTrackingMode() {
    const char* name = "";
    switch (g_session.CycleMode()) {
        case cameraunlock::TrackingMode::RotationAndPosition: name = "rotation and position"; break;
        case cameraunlock::TrackingMode::RotationOnly:        name = "rotation only"; break;
        case cameraunlock::TrackingMode::PositionOnly:        name = "position only"; break;
    }
    Log::Line("[input] tracking mode: %s", name);
}

// Every action is reachable two ways: its nav-cluster key, and the
// Ctrl+Shift+<letter> chord for keyboards without a nav cluster. Pairing them
// in one row is what keeps the two lists from drifting apart - a new action
// cannot pick up a nav key and silently miss its chord, and the line that tells
// the user which keys they ended up on is built from these same rows rather
// than from a second hand-written copy of the pairing.
struct HotkeyBinding {
    const char* action;
    int nav_key;
    int chord_key;
    void (*handler)();
};

std::array<HotkeyBinding, 2> Bindings(const Config& config) {
    return {{
        { "toggle tracking",     config.toggle_key,     config.chord_toggle_key,     ToggleTracking },
        { "cycle tracking mode", config.cycle_mode_key, config.chord_cycle_mode_key, CycleTrackingMode },
    }};
}

void RegisterHotkeys(const Config& config) {
    using namespace cameraunlock::input;

    for (const HotkeyBinding& binding : Bindings(config)) {
        g_hotkeys.AddHotkey(binding.nav_key, NavGuarded(binding.handler));
        g_hotkeys.AddHotkey(binding.chord_key, ChordGuarded(binding.handler));
    }

    g_hotkeys.Start();
}

void LogHotkeys(const Config& config) {
    std::string ready = "[boot] ready.";
    for (const HotkeyBinding& binding : Bindings(config)) {
        ready += " " + HotkeyName(binding.nav_key) + "/Ctrl+Shift+"
               + HotkeyName(binding.chord_key) + " " + binding.action + ",";
    }
    ready.back() = '.';

    // Through %s, never as the format itself: the names come from the keyboard
    // layout, and a layout that names a key with a '%' would otherwise turn this
    // line into a format string reading arguments that were never passed.
    Log::Line("%s", ready.c_str());
}

// ---------------------------------------------------------------------------
// Bootstrap
//
// Each step either leaves the mod ready or leaves it dormant with the reason in
// the log. Split out so the sequence below reads as the sequence.
// ---------------------------------------------------------------------------

bool OpenLogAndResolveGameDirectory(std::string& exe_dir) {
    // The core's resolver rather than a local copy of it: it grows its buffer
    // past MAX_PATH, so a game under a long install path resolves instead of
    // leaving the mod dormant, and it refuses a best-fit ANSI narrowing rather
    // than handing back the name of a different directory that happens to exist.
    const std::wstring exe_dir_wide = cameraunlock::os::HostExeDirectory();

    // Beside the game EXE, not in the process working directory: a launcher can
    // start the game from anywhere, and a bare relative name then drops the log
    // wherever that happens to be - or fails to create it at all - exactly when
    // a user is being asked to send one. An unresolved directory still needs
    // somewhere to say so before the mod goes dormant.
    Log::Open(exe_dir_wide.empty() ? std::wstring(L"HeadTracking.log")
                                   : exe_dir_wide + L"\\HeadTracking.log");
    Log::Line("=== Wreckfest Head Tracking ===");

    // The INI layer is ANSI-only (IniReader wraps GetPrivateProfile*A), so a
    // directory with no ANSI form is as unusable as one that would not resolve.
    if (exe_dir_wide.empty() || !cameraunlock::os::NarrowToAnsi(exe_dir_wide, exe_dir)) {
        Log::Line("[boot] could not resolve the game directory - mod is dormant, game runs vanilla.");
        return false;
    }
    Log::Line("[boot] game directory: %s", exe_dir.c_str());
    return true;
}

void LoadAndApplyConfig(const std::string& exe_dir) {
    WriteDefaultConfigIfMissing(exe_dir);
    LoadConfig(exe_dir, g_config);
    Log::Line("[boot] config: port=%u enableOnStartup=%d localSmoothing=%.2f "
              "remoteSmoothing=%.2f position=%d",
              static_cast<unsigned>(g_config.udp_port), g_config.enable_on_startup ? 1 : 0,
              g_config.local_smoothing, g_config.remote_smoothing,
              g_config.position_enabled ? 1 : 0);

    ApplyConfigToPipeline(g_config, g_session);
    g_tracking_enabled.store(g_config.enable_on_startup);
}

void StartReceiver() {
    g_receiver.SetLog([](const std::string& msg) { Log::Line("[udp] %s", msg.c_str()); });
    if (g_receiver.Start(g_config.udp_port)) {
        Log::Line("[boot] listening for OpenTrack data on UDP %u",
                  static_cast<unsigned>(g_config.udp_port));
    }
}

// Takes a reference on this module so an explicit FreeLibrary cannot unmap it.
// Three things outlive such a call: the detached bootstrap thread, the hotkey
// and receiver threads, and - the one that is fatal - an inline detour sitting
// in the game's camera update, which a thread can be executing at the moment
// the pages go away. Shutdown() exists to undo all of that, but it runs from
// DllMain under the loader lock, where joining a thread deadlocks, so it cannot
// be made to work from there either. Refusing the unload removes both problems;
// the process exiting still reclaims everything.
//
// The address handed in has to be one inside this module, hence a function of
// our own rather than anything the caller passes.
bool PinModule() {
    HMODULE self = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&ApplyConfigToPipeline),
                              &self) != FALSE;
}

void Bootstrap() {
    std::string exe_dir;
    if (!OpenLogAndResolveGameDirectory(exe_dir)) return;

    if (!g_pinned.load()) {
        Log::Line("[boot] WARNING: this module could not be pinned against unloading. "
                  "If something calls FreeLibrary on it while the game is running, the "
                  "camera hook is left pointing at unmapped memory.");
    }

    if (builds::SelectProfile(GetModuleHandleW(nullptr)) != builds::ProfileSelection::Matched) {
        Log::Line("[boot] no usable build profile - mod is dormant, game runs vanilla.");
        return;
    }
    if (!InitGameState()) {
        Log::Line("[boot] the gameplay gate could not be resolved - mod is dormant, "
                  "game runs vanilla.");
        return;
    }

    LoadAndApplyConfig(exe_dir);
    StartReceiver();

    if (!InstallCameraHook()) {
        // Nothing will ever read the tracker now, so give the port back rather
        // than sitting on 4242 for the rest of the session and blocking
        // whatever else the user points their tracker at.
        g_receiver.Stop();
        Log::Line("[boot] the camera update could not be hooked - mod is inert.");
        return;
    }

    RegisterHotkeys(g_config);
    g_active.store(true);
    LogHotkeys(g_config);
}

}  // namespace

bool ApplyTrackingToCameraTransform(float* transform) {
    if (!g_active.load(std::memory_order_relaxed)) return false;

    // Several cameras can update in the same frame. Splitting a frame's delta
    // across those calls is harmless: the smoothing and interpolation are both
    // exponential in dt, so the total advance per frame is the same whether it
    // arrives in one step or several.
    const float dt = g_frame_clock.Tick();

    // The pipeline advances whatever the gate says. Freezing it in the garage
    // and thawing it on the grid would compose a pose from wherever the head
    // was when the player last drove; advancing it means the first tracked
    // frame is composed from where the head is now, with nothing to jump from.
    if (g_session.Update(dt)) LogConnectionLocality();

    const long long frame = g_frame_counter.fetch_add(1, std::memory_order_relaxed);

    HeadPose pose;
    const bool have_rotation = g_session.GetRotation(pose.yaw, pose.pitch, pose.roll);
    g_session.GetPositionOffset(pose.lean_x, pose.lean_y, pose.lean_z);

    LogFirstFrames(frame, transform, have_rotation, pose);
    LogFirstPoseReachingCamera(frame, have_rotation, pose);

    const ViewState view = ReadViewState();
    const bool race_session = IsRaceSessionLive();
    const bool paused = IsGamePaused();
    const bool following = ShouldFollowHead(g_tracking_enabled.load(std::memory_order_relaxed),
                                            view, race_session, paused);
    LogGateChange(view, race_session, paused, following);

    // No tracker data, or the gate is shut: leave the engine's camera exactly
    // as it computed it.
    if (!have_rotation || !following) return false;

    LogFirstComposedPose(pose);
    ApplyHeadPose(transform, pose);
    return true;
}

void Initialize() {
    // Before anything else, and before any thread exists to be caught by it.
    g_pinned.store(PinModule());

    // Detached: DllMain runs under the loader lock, so the bootstrap (which
    // opens a log, reads the INI and resolves engine statics) cannot run here.
    std::thread(Bootstrap).detach();
}

void Shutdown() {
    g_active.store(false);
    UninstallCameraHook();
    g_hotkeys.Stop();
    g_receiver.Stop();
    Log::Line("[boot] shutdown");
    Log::Close();
}

}  // namespace wf_ht
