// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <windows.h>

// Nothing on DLL_PROCESS_DETACH. Initialize pins the module, so the only detach
// is process exit, and the process reclaims the hooks, threads and socket.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        wf_ht::Initialize();
    }
    return TRUE;
}
