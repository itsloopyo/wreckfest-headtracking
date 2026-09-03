// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

namespace wf_ht {

// What the engine is showing right now, as far as head tracking cares.
enum class ViewState {
    // The view manager or its active camera controller could not be read. The
    // mod treats this as "not gameplay" rather than tracking blind: this state
    // covers the whole of startup, when the render camera exists but nothing
    // has decided what it is looking at yet.
    Unknown,
    // No camera controller is active. Loading screens and the boot sequence.
    NoView,
    // A BGAME::GarageCamera owns the view: the menus and the garage. A different
    // camera on a different scene from the one the player drives in, so a head
    // pose composed for the cockpit has no meaning here.
    Menu,
    // One of the BGAME::IngameCamera family owns the view: the car camera, a
    // trackside camera, a scripted camera, or the free camera. Includes replays,
    // which render a real 3D view the player is watching.
    Gameplay,
};

const char* ViewStateName(ViewState state);

// Resolves the view manager static and the camera controller vtables from the
// active build profile, so the per-frame reads below are pointer loads and
// integer compares rather than a profile lookup. Runs after
// builds::SelectProfile() has matched, and returns false if the static falls
// outside the running module - in which case the gate reports Unknown forever
// and the mod never follows the head.
bool InitGameState();

// The current view, read fresh. Call from the render thread.
ViewState ReadViewState();

// True while the engine's global pause is raised: the pause menu is up, or the
// game window has lost focus. False when it is down, and false whenever the
// chain cannot be read.
//
// Fail OPEN, which is the opposite call to everything else in this file, and
// the reason is what a wrong answer costs. The pause chain is pinned per build,
// so a patch moving it makes the read fail; treating that as "paused" would
// take head tracking down everywhere on the first stale offset, and being wrong
// the other way only means the head still moves the camera behind the pause
// menu until the profile is updated. A cosmetic regression beats a dead mod.
// The failure is logged once so it is still visible. Call from the render
// thread.
bool IsGamePaused();

// True while a race session is live: from the moment the cars go on the grid to
// the moment the session is torn down. False in the menus and the garage.
//
// The view alone cannot answer this. Wreckfest's main menu renders its scene
// through an IngameCarCamera, exactly as a race does, so the camera controller
// says "a race camera" in both places and only the engine's own session flag
// separates them. Call from the render thread.
bool IsRaceSessionLive();

// The gate: true when the camera hook should compose the head pose into the
// frame it is about to draw. Pure, so the classification can be tested without
// a game running.
//
// Anything that is not Gameplay is off, Unknown included - a view the mod could
// not read is not one to modify - and a live race session is required on top of
// it, because the menus render through a race camera too. The engine's pause
// is off on top of both: the pause menu renders the same cockpit from the same
// race camera with the session still live, so nothing else in the gate can see
// it.
//
// Pausing does cost two jumps - the view snaps to the engine's camera when the
// menu comes up and back to the tracked one when it closes - and that is the
// point rather than a side effect. The pipeline keeps advancing while the gate
// is shut, so the frame tracking resumes on is composed from where the head is
// then, not from where it was when the player paused.
//
// Online races follow the head exactly as local ones do. The gate used to read
// the engine's bgMultiplayerGameGet() and shut for anything that was not
// positively a local session, which cost every online player the mod and cost
// the mod its only indirect call into an engine function. Nothing here is
// visible to a server or to another player: the pose is composed into the
// camera transform for the frame being drawn and taken back out before the
// engine interpolates from it, so car control, physics and everything sent over
// the wire read the camera the game computed.
bool ShouldFollowHead(bool tracking_enabled, ViewState view,
                      bool race_session_live, bool paused);

}  // namespace wf_ht
