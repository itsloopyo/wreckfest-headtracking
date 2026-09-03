// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_hook.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "builds/build_registry.h"
#include "camera_transform.h"
#include "headtracking_mod.h"
#include "logging.h"

#include "cameraunlock/hooks/hook_manager.h"

namespace wf_ht {

namespace {

//   void __fastcall ViewManager::Update(ViewManager* this, FrameContext* frame)
using ViewManagerUpdateFn = void(__fastcall*)(void*, void*);
//   void __fastcall BCORE::Camera::UpdateViewMatrix(Camera* this)
using ViewMatrixFn = void(__fastcall*)(void*);

ViewManagerUpdateFn g_original_view_manager_update = nullptr;
ViewMatrixFn g_original_view_matrix = nullptr;

void* g_view_manager_update_target = nullptr;
void* g_view_matrix_target = nullptr;

// Read once at install time rather than per frame: the active profile cannot
// change while the hooks are installed, and both sit on the render path.
unsigned g_world_transform_offset = 0;
unsigned g_current_camera_offset = 0;

// The camera the head pose was last composed into, and the transform the engine
// had computed before it was. Written and read only on the thread that runs the
// view manager's camera update.
void* g_injected_camera = nullptr;
float g_clean_transform[kCameraTransformFloats];

float* WorldTransform(void* camera) {
    return reinterpret_cast<float*>(
        reinterpret_cast<std::uint8_t*>(camera) + g_world_transform_offset);
}

void* CurrentCamera(void* view_manager) {
    return *reinterpret_cast<void**>(
        reinterpret_cast<std::uint8_t*>(view_manager) + g_current_camera_offset);
}

void __fastcall ViewManagerUpdateDetour(void* view_manager, void* frame) {
    // Take the head pose back out before the engine's own camera update runs.
    // It interpolates the render camera from its previous value, and on the
    // branch where the target state is still in the future it copies the render
    // camera into its own blend source - so a tracked rotation left in place is
    // fed back into engine state and compounds frame on frame.
    //
    // Only into the camera the pose was composed into, and only while the view
    // manager still holds that same camera: the manager swaps cameras between
    // the garage and the grid, and the pointer we kept is no guarantee the
    // object behind it is still one.
    if (g_injected_camera != nullptr && g_injected_camera == CurrentCamera(view_manager)) {
        std::memcpy(WorldTransform(g_injected_camera), g_clean_transform,
                    sizeof(g_clean_transform));
    }
    g_injected_camera = nullptr;

    g_original_view_manager_update(view_manager, frame);
}

void __fastcall ViewMatrixDetour(void* camera) {
    float* world = WorldTransform(camera);
    std::memcpy(g_clean_transform, world, sizeof(g_clean_transform));

    // Composed BEFORE the original call so the view matrix it derives - and the
    // history entry the next frame reprojects against - are both built from the
    // tracked eye, and left in place AFTER it because the renderer reads the
    // camera's world transform again later in the frame, long after this
    // returns. Restoring here instead would leave the head pose reaching
    // nothing but the motion vectors, which draws a smear across a view that
    // never moved.
    if (ApplyTrackingToCameraTransform(world)) g_injected_camera = camera;

    g_original_view_matrix(camera);
}

bool Failed(cameraunlock::hooks::HookStatus status, const char* what) {
    using cameraunlock::hooks::HookStatus;
    if (status == HookStatus::Ok) return false;
    Log::Line("[camera] %s failed: %s", what,
              cameraunlock::hooks::HookStatusToString(status));
    return true;
}

bool Install(void* target, void* detour, void** original, const char* what) {
    cameraunlock::hooks::HookManager& hooks = cameraunlock::hooks::HookManager::Instance();
    if (Failed(hooks.CreateHook(target, detour, original), what)) return false;
    if (Failed(hooks.EnableHook(target), what)) {
        hooks.RemoveHook(target);
        return false;
    }
    return true;
}

void Remove(void*& target) {
    if (target == nullptr) return;
    cameraunlock::hooks::HookManager& hooks = cameraunlock::hooks::HookManager::Instance();
    hooks.DisableHook(target);
    hooks.RemoveHook(target);
    target = nullptr;
}

}  // namespace

bool InstallCameraHook() {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;

    const builds::BuildProfile& profile = builds::ActiveProfile();
    if (profile.Offsets.camera_world_transform_floats != kCameraTransformFloats) {
        Log::Line("[camera] profile %s declares a %u-float camera transform, but this mod "
                  "composes a 4x4 (%u floats). Not patching.",
                  profile.Name, profile.Offsets.camera_world_transform_floats,
                  kCameraTransformFloats);
        return false;
    }
    g_world_transform_offset = profile.Offsets.camera_world_transform;
    g_current_camera_offset = profile.Offsets.view_manager_current_camera;

    HookManager& hooks = HookManager::Instance();
    // Another ASI in the same process may already own MinHook's single global
    // state. That is the library working as intended, not a failure to install.
    const HookStatus initialized = hooks.Initialize();
    if (initialized != HookStatus::ErrorAlreadyInitialized
        && Failed(initialized, "MinHook init")) {
        return false;
    }

    std::uint8_t* module_base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    void* view_manager_update = module_base + profile.Offsets.view_manager_update_rva;
    void* view_matrix = module_base + profile.Offsets.camera_view_matrix_rva;

    if (!Install(view_manager_update, reinterpret_cast<void*>(&ViewManagerUpdateDetour),
                 reinterpret_cast<void**>(&g_original_view_manager_update),
                 "hooking the view manager camera update")) {
        return false;
    }
    if (!Install(view_matrix, reinterpret_cast<void*>(&ViewMatrixDetour),
                 reinterpret_cast<void**>(&g_original_view_matrix),
                 "hooking the camera view matrix derivation")) {
        Remove(view_manager_update);
        return false;
    }

    g_view_manager_update_target = view_manager_update;
    g_view_matrix_target = view_matrix;
    Log::Line("[camera] hooked ViewManager::Update at 0x%p and Camera::UpdateViewMatrix at "
              "0x%p (profile %s, transform +0x%X, render camera +0x%X)",
              view_manager_update, view_matrix, profile.Name, g_world_transform_offset,
              g_current_camera_offset);
    return true;
}

void UninstallCameraHook() {
    Remove(g_view_matrix_target);
    Remove(g_view_manager_update_target);
    g_injected_camera = nullptr;
}

}  // namespace wf_ht
