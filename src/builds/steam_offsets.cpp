// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/build_profile.h"

// Every Steam build profile lives here, append-only. Never edit an existing
// profile's numbers to "fix" a patch and never delete one: a user who has held
// back on an older build must keep matching their old profile from the same
// mod binary. Adding a profile is the only correct response to a patch.

namespace wf_ht::builds {

// Wreckfest, Steam app 228380, Wreckfest_x64.exe built 2023-09-19 10:08:29 UTC
// (Steam build 16986367). CheckSum is 0 in the shipped EXE - the linker never
// stamped one - so the fingerprint leans on TimeDateStamp + SizeOfImage.
extern const BuildProfile kSteamProfile_20230919 = {
    "steam-win64-20230919",
    { 0x6509731D, 0x0B43A000, 0x00000000 },
    {
        /* view_manager_update_rva         */ 0x002E7FB0,
        /* camera_view_matrix_rva          */ 0x00F02040,
        /* camera_world_transform          */ 0x10,
        /* camera_world_transform_floats   */ 16,
        /* view_manager_ptr_rva            */ 0x014FE040,
        /* view_manager_current_camera     */ 0x68,
        /* view_manager_active_controller  */ 0x30,
        /* garage_camera_vtable_rva        */ 0x00FDB698,
        /* ingame_camera_vtable_rva        */ 0x00FD97E8,
        /* ingame_car_camera_vtable_rva    */ 0x00FD9908,
        /* ingame_free_camera_vtable_rva   */ 0x00FD9A78,
        /* ingame_trackside_camera_vtable_rva */ 0x00FD9B90,
        /* ingame_animated_camera_vtable_rva  */ 0x00FDB670,
        /* race_session_active_rva         */ 0x01854F67,
        /* pause_state_ptr_rva             */ 0x014FDAF8,
        /* pause_state_object              */ 0x30,
        /* pause_state_paused              */ 0x46,
    },
};

}  // namespace wf_ht::builds
