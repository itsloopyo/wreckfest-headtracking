cmake_minimum_required(VERSION 3.20)
project(WreckfestHeadTracking VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP /W4)
endif()

# MinHook is vendored rather than fetched: `pixi run package` has to build from
# a clean checkout with no network, and the source compiled into the binary
# users install should be the source in this repo.
add_library(minhook STATIC
    extern/minhook/src/buffer.c
    extern/minhook/src/hook.c
    extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c
    extern/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include)
if(MSVC)
    target_compile_options(minhook PRIVATE /W0)
endif()

# HOOKS carries hooks/hook_manager.cpp, the MinHook wrapper the camera hook
# places its detour through. DISCOVERY stays off: this mod reaches the camera
# through the RVA its build profile pins, not by RTTI vtable resolution.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_DISCOVERY OFF CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/config.cpp
    src/camera_hook.cpp
    src/camera_transform.cpp
    src/game_state.cpp
    src/hotkey_names.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
)

add_library(WreckfestHeadTracking SHARED ${MOD_SOURCES})

target_include_directories(WreckfestHeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(WreckfestHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    minhook
    ws2_32
    psapi
)

# Emit a linker map so a crash offset (asi+0xNNNN) resolves to a symbol.
if(MSVC)
    target_link_options(WreckfestHeadTracking PRIVATE /MAP /MAPINFO:EXPORTS)
endif()

# Output as .asi for Ultimate ASI Loader.
set_target_properties(WreckfestHeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# Opt-in unit tests. OFF by default so the normal build and the release
# packager never produce a test binary next to the .asi. Enabled by
# scripts/run-tests.ps1 (`pixi run test`), which builds the wf_tests target.
option(WF_BUILD_TESTS "Build WreckfestHeadTracking unit tests" OFF)
if(WF_BUILD_TESTS)
    enable_testing()

    function(wf_add_test test_name)
        set(target "wf_${test_name}_tests")
        add_executable(${target} tests/${test_name}_tests.cpp ${ARGN})
        target_include_directories(${target} PRIVATE
            src
            cameraunlock-core/cpp/include
        )
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
        # /W4 and /permissive- are already on globally; tests additionally
        # treat warnings as errors.
        if(MSVC)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        add_test(NAME ${test_name} COMMAND ${target})
        set_property(GLOBAL APPEND PROPERTY WF_TEST_TARGETS ${target})
    endfunction()

    # Pure boundary logic - headers only, nothing to link.
    wf_add_test(config_sanitize)

    # Pure camera math, compiled straight from the production translation unit.
    wf_add_test(camera_transform src/camera_transform.cpp)

    # Where the log and the INI land. The mod calls cameraunlock::os for this
    # rather than carrying its own copy, so this locks the contract the
    # bootstrap depends on. The running test EXE stands in for the game.
    wf_add_test(exe_paths)
    target_link_libraries(wf_exe_paths_tests PRIVATE cameraunlock)

    # The gameplay gate's classification, compiled from the production
    # translation unit. Reading the engine's state needs the game; deciding
    # what each state means for tracking does not. The build registry comes
    # along because InitGameState resolves its offsets from the active profile.
    wf_add_test(game_state src/game_state.cpp
                src/builds/build_registry.cpp src/builds/steam_offsets.cpp)
    target_link_libraries(wf_game_state_tests PRIVATE cameraunlock)

    # The one log line that reports which keys the config landed on. What a key
    # is called is the keyboard layout's business; that a name is always
    # produced is this mod's.
    wf_add_test(hotkey_names src/hotkey_names.cpp)

    # Exercises the real INI generator and loader, so it needs the core's
    # IniReader and the log sink config.cpp writes its diagnostics to.
    wf_add_test(config_defaults src/config.cpp)
    target_link_libraries(wf_config_defaults_tests PRIVATE cameraunlock)
    target_compile_definitions(wf_config_defaults_tests PRIVATE
        WF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    # The hotkey half of the same loader: which INI values reach a binding and
    # which leave the action on the key it already had.
    wf_add_test(config_hotkeys src/config.cpp)
    target_link_libraries(wf_config_hotkeys_tests PRIVATE cameraunlock)

    # And the rest of the loader's boundary: which bool, float and port values
    # reach the Config, and which are refused and leave it as it was. A refused
    # value that looks like an unset one is the failure this locks out.
    wf_add_test(config_values src/config.cpp)
    target_link_libraries(wf_config_values_tests PRIVATE cameraunlock)

    # Built from what wf_add_test registered, so a new test cannot be added to
    # ctest and left out of the target run-tests.ps1 builds.
    get_property(wf_test_targets GLOBAL PROPERTY WF_TEST_TARGETS)
    add_custom_target(wf_tests DEPENDS ${wf_test_targets})
endif()
