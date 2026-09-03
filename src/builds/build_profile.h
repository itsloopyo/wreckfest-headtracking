// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include "cameraunlock/memory/pe_fingerprint.h"

namespace wf_ht::builds {

// Everything this mod pins to a specific Wreckfest_x64.exe build.
//
// Bugbear's engine ships full MSVC RTTI, so the camera CONTROLLER classes
// (IngameCarCamera and its IngameCamera siblings, GarageCamera) are resolved at
// runtime by class name and are not pinned. What RTTI cannot reach is the
// render camera itself: BCORE::Camera is a plain data object reached through a
// module-static pointer, and neither the function that derives its view matrix
// nor the view manager update that drives it has a vtable at all. Those are the
// pinned surface, along with the struct offsets the mod reads through.
struct OffsetTable {
    // The view manager's per-frame camera update. It interpolates the render
    // camera at view_manager_current_camera between the two camera states
    // either side of the current time, and then calls the view matrix
    // derivation below. The mod hooks it to take the previous frame's head pose
    // back OUT before the interpolation runs: on the branch where the target
    // state is still in the future the engine copies the render camera into its
    // own blend source, so a tracked rotation left in place is fed back in and
    // compounds frame on frame.
    //   void __fastcall Update(ViewManager* this, FrameContext* frame)
    unsigned int view_manager_update_rva;

    // BCORE::Camera's view matrix derivation, called once per rendered frame
    // from the view manager update above. It rolls the camera's eight-deep
    // history of view matrices forward and then writes a fresh one by inverting
    // the camera-to-world transform. The head pose is composed into that
    // transform immediately before this runs, so the view matrix this frame is
    // drawn with - and the history entry the next frame reprojects against -
    // are both built from the tracked eye.
    //   void __fastcall UpdateViewMatrix(Camera* this)
    unsigned int camera_view_matrix_rva;

    // Byte offset of the camera-to-world transform inside BCORE::Camera, and
    // the number of floats it occupies. Row-major: rows 0..2 are the camera's
    // right, up and forward axes, row 3 is its world position.
    unsigned int camera_world_transform;
    unsigned int camera_world_transform_floats;

    // Module-static pointer to the view manager (the object that owns the
    // camera controllers and the BCORE::Camera instances), and the byte offsets
    // of the two fields the mod reads: the render camera the frame is drawn
    // from, and the camera controller currently driving it.
    unsigned int view_manager_ptr_rva;
    unsigned int view_manager_current_camera;
    unsigned int view_manager_active_controller;

    // Vtable RVAs of every camera controller class the view manager can make
    // active - the whole BGAME::IngameCamera family plus BGAME::GarageCamera.
    // The gameplay gate reads the active controller's vtable pointer and looks
    // it up here, which is one comparison per class on the render path and no
    // RTTI walk. GarageCamera is the menus and the garage; the Ingame family is
    // the race. A controller whose vtable is in neither list leaves the gate
    // reading unknown, so a class added by a patch cannot quietly be treated as
    // gameplay.
    unsigned int garage_camera_vtable_rva;
    unsigned int ingame_camera_vtable_rva;
    unsigned int ingame_car_camera_vtable_rva;
    unsigned int ingame_free_camera_vtable_rva;
    unsigned int ingame_trackside_camera_vtable_rva;
    unsigned int ingame_animated_camera_vtable_rva;

    // Module-static byte the engine raises while a race session is live: the
    // compiled state machines call the context's race-session-begin entry when
    // the cars go on the grid, and its teardown entry when the session ends,
    // and those two are the only writers. The getter beside it in the context
    // table is the one asserts spell "c->bgIsRaceStarted()", which is a
    // different byte one address along - that one is the green light, so it is
    // false through the whole grid countdown. This is the session, so it covers
    // the countdown, the race and the pause menu over the top of it.
    //
    // Read directly rather than through the context's getter. The value is a
    // byte in the game's own data, so reading it needs no function pointer and
    // cannot end with the mod jumping into whatever a wrong offset held.
    unsigned int race_session_active_rva;

    // The engine's global pause byte, reached as
    // *(*(module + pause_state_ptr_rva) + pause_state_object) + pause_state_paused.
    // Written by exactly two functions: the engine's SetPaused(paused, silent),
    // which raises or clears it and fires the PAUSE_ALL / RESUME_ALL audio
    // events either side, and the session teardown, which clears it. The pause
    // menu, and a window that loses focus, both reach it through SetPaused.
    //
    // Deliberately NOT part of IsProfileComplete: a build whose pause chain has
    // not been derived leaves pause_state_ptr_rva zero, and the mod skips the
    // read and tracks as it did before rather than going dormant. Every failure
    // to read this reads as NOT paused - see IsGamePaused.
    unsigned int pause_state_ptr_rva;
    unsigned int pause_state_object;
    unsigned int pause_state_paused;
};

struct BuildProfile {
    const char* Name;
    cameraunlock::memory::PeFingerprint Fingerprint;
    OffsetTable Offsets;
};

// A profile with no camera addresses is a placeholder landed ahead of the
// rederive: the fingerprint routes, but the mod must stay dormant.
inline bool IsProfileComplete(const BuildProfile& p) {
    return p.Offsets.view_manager_update_rva != 0
        && p.Offsets.camera_view_matrix_rva != 0
        && p.Offsets.camera_world_transform_floats != 0
        && p.Offsets.view_manager_ptr_rva != 0
        && p.Offsets.garage_camera_vtable_rva != 0
        && p.Offsets.race_session_active_rva != 0;
}

}  // namespace wf_ht::builds
