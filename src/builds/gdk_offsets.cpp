// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/build_profile.h"

// Every Xbox / Game Pass (GDK) build profile lives here, append-only. Never
// edit an existing profile's numbers to "fix" a patch and never delete one: a
// user who has held back on an older build must keep matching their old
// profile from the same mod binary. Adding a profile is the only correct
// response to a patch.
//
// The GDK build is a separate binary from the Steam one, not a repack, so its
// numbers are all its own - the two share only the struct offsets, which the
// engine's own layout fixes. Its EXE cannot be read off disk: the package
// folder denies reads of the executable whatever the ACL says, so the numbers
// below were taken from the module as it is mapped in the running process.

namespace wf_ht::builds {

// Wreckfest, Xbox / Microsoft Store package NordicGames.631082A550AE7 version
// 1.4.3.0, Wreckfest_x64.exe built 2021-02-23 09:07:26 UTC. CheckSum is 0, the
// same as the Steam build - the linker never stamped one - so the fingerprint
// leans on TimeDateStamp + SizeOfImage.
extern const BuildProfile kGdkProfile_20210223 = {
    "gdk-win64-20210223",
    { 0x6034C5CE, 0x0B1F8000, 0x00000000 },
    {
        /* view_manager_update_rva         */ 0x00289F20,
        /* camera_view_matrix_rva          */ 0x00E1C530,
        /* camera_world_transform          */ 0x10,
        /* camera_world_transform_floats   */ 16,
        /* view_manager_ptr_rva            */ 0x0133A7D8,
        /* view_manager_current_camera     */ 0x68,
        /* view_manager_active_controller  */ 0x30,
        /* garage_camera_vtable_rva        */ 0x00EAC960,
        /* ingame_camera_vtable_rva        */ 0x00EAA720,
        /* ingame_car_camera_vtable_rva    */ 0x00EAA848,
        /* ingame_free_camera_vtable_rva   */ 0x00EAA9C8,
        /* ingame_trackside_camera_vtable_rva */ 0x00EAAAF0,
        /* ingame_animated_camera_vtable_rva  */ 0x00EAC928,
        /* race_session_active_rva         */ 0x0133C146,
        /* pause_state_ptr_rva             */ 0x0133A420,
        /* pause_state_object              */ 0x30,
        /* pause_state_paused              */ 0x46,
    },
};

}  // namespace wf_ht::builds
