// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Behaviour-locking tests for the gameplay gate's classification. Reading the
// engine's view manager needs the game; deciding what each state means for head
// tracking does not, and that decision is the part that can silently start
// modifying a camera it should have left alone.

#include "game_state.h"

#include "test_support.h"

#include <cstdio>
#include <cstring>

using namespace wf_ht;
using wf_test::Check;

namespace {

const ViewState kAllViews[] = {
    ViewState::Unknown, ViewState::NoView, ViewState::Menu, ViewState::Gameplay,
};

void GameplayIsTheOnlyFollowingState() {
    std::printf("Only gameplay follows the head\n");

    Check(ShouldFollowHead(true, ViewState::Gameplay, true, false),
          "gameplay in a live, unpaused race session with tracking on follows");

    for (ViewState view : kAllViews) {
        if (view == ViewState::Gameplay) continue;
        Check(!ShouldFollowHead(true, view, true, false),
              "a view that is not gameplay leaves the camera alone");
    }
}

// The case the view alone gets wrong: Wreckfest's main menu renders its scene
// through an IngameCarCamera, so the camera controller reads as gameplay in the
// menus exactly as it does on track. Without the engine's own race session flag
// the gate opens over the main menu, which is what shipped first.
void AMenuRenderedThroughARaceCameraIsStillAMenu() {
    std::printf("A live race session is required on top of the view\n");

    for (ViewState view : kAllViews) {
        Check(!ShouldFollowHead(true, view, false, false),
              "no live race session never follows the head");
    }
}

// The case neither the view nor the session flag can see. The pause menu draws
// the same cockpit through the same IngameCarCamera, and the race session stays
// live across it - the session byte deliberately covers the countdown, the race
// AND the pause menu - so the engine's own pause byte is the only thing left
// that separates a paused race from a running one.
void APausedRaceIsOff() {
    std::printf("A paused race does not follow the head\n");

    for (ViewState view : kAllViews) {
        Check(!ShouldFollowHead(true, view, true, true),
              "a paused race never follows the head");
    }
}

void TheToggleIsRespected() {
    std::printf("The player's toggle shuts the gate on its own\n");

    for (ViewState view : kAllViews) {
        Check(!ShouldFollowHead(false, view, true, false),
              "tracking toggled off never follows the head");
    }
}

void EveryViewStateIsNamed() {
    // The name is what a "head tracking does nothing" bug report is triaged
    // from, so an unnamed state is a state nobody can diagnose.
    std::printf("Every view state has a name for the log\n");

    for (ViewState view : kAllViews) {
        const char* name = ViewStateName(view);
        Check(name != nullptr && name[0] != '\0', "the state has a non-empty name");
    }

    Check(std::strcmp(ViewStateName(ViewState::Gameplay),
                      ViewStateName(ViewState::Menu)) != 0,
          "gameplay and menu do not share a name");
}

}  // namespace

int main() {
    std::printf("Wreckfest head tracking - gameplay gate tests\n");
    std::printf("=======================================================\n");
    GameplayIsTheOnlyFollowingState();
    AMenuRenderedThroughARaceCameraIsStillAMenu();
    APausedRaceIsOff();
    TheToggleIsRespected();
    EveryViewStateIsNamed();
    return wf_test::Summary("gameplay gate");
}
