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
    src/legacy_config/legacy_config.cpp
    src/camera_hook.cpp
    src/camera_transform.cpp
    src/game_state.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
    src/builds/gdk_offsets.cpp
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

    # The frozen reader's value guards - headers only, nothing to link.
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
                src/builds/build_registry.cpp src/builds/steam_offsets.cpp
                src/builds/gdk_offsets.cpp)
    target_link_libraries(wf_game_state_tests PRIVATE cameraunlock)

    # The committed config against the table's fresh render, the fleet's
    # default hotkeys, and the mode cycle's save. `wf_config_tests
    # --render-config <path>` is pixi run render-config.
    wf_add_test(config src/config.cpp src/legacy_config/legacy_config.cpp)
    target_link_libraries(wf_config_tests PRIVATE cameraunlock)
    target_compile_definitions(wf_config_tests PRIVATE
        WF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    # The hotkey half of the frozen loader: which INI values reach a binding and
    # which leave the action on the key it already had.
    wf_add_test(config_hotkeys src/legacy_config/legacy_config.cpp)
    target_link_libraries(wf_config_hotkeys_tests PRIVATE cameraunlock)

    # And the rest of the frozen loader's boundary: which bool, float and port
    # values reach the Config, and which are refused and leave it as it was. A
    # refused value that looks like an unset one is the failure this locks out.
    wf_add_test(config_values src/legacy_config/legacy_config.cpp)
    target_link_libraries(wf_config_values_tests PRIVATE cameraunlock)

    # The canonical config conversion's differential test: v0.1.0's reader and
    # startup code (the oracle, compiled from byte copies in
    # tests/config_differential/oracle) against the frozen reader in
    # src/legacy_config/, and that against the config owner's migration into
    # CameraUnlock.ini.
    set(WF_DIFFERENTIAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    add_executable(wf_config_differential
        ${WF_DIFFERENTIAL_DIR}/differential_tests.cpp
        ${WF_DIFFERENTIAL_DIR}/oracle/oracle_reader.cpp
        src/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_include_directories(wf_config_differential PRIVATE
        src
        ${WF_DIFFERENTIAL_DIR}
        cameraunlock-core/cpp/include
    )
    target_compile_definitions(wf_config_differential PRIVATE
        WF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(wf_config_differential PRIVATE cameraunlock)
    set_target_properties(wf_config_differential PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    if(MSVC)
        target_compile_options(wf_config_differential PRIVATE /WX /EHsc /bigobj)
    endif()
    add_test(NAME config_differential COMMAND wf_config_differential)
    set_property(GLOBAL APPEND PROPERTY WF_TEST_TARGETS wf_config_differential)
    # Core's canonical config lint over the committed file and every file the
    # differential test migrated, which it writes beside itself under migrated/.
    add_test(NAME config_lint COMMAND node
             ${WF_DIFFERENTIAL_DIR}/lint-migrated.mjs
             $<TARGET_FILE_DIR:wf_config_differential>/migrated)
    set_tests_properties(config_lint PROPERTIES DEPENDS config_differential)

    # What the differential test compiles is pinned by hash, line endings aside:
    # the oracle's byte copies must stay v0.1.0's (git show v0.1.0:src/<file>),
    # the core sources both readers compile must stay the ones v0.1.0 was built
    # with (core d992124), and the frozen reader is never edited.
    function(wf_require_hash path expected)
        file(READ "${path}" text)
        string(REPLACE "\r" "" text "${text}")
        string(SHA256 actual "${text}")
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "${path} has changed (SHA-256 of its text ${actual}, pinned "
                                "${expected}). The config differential test compares against "
                                "exactly this file; see CMakeLists.txt.")
        endif()
    endfunction()
    set(WF_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp)
    foreach(pinned
            "${WF_DIFFERENTIAL_DIR}/oracle/src/config.cpp|751796612f659fe4fa7a8469448f49ceef141fa1646c45ec6055b5f7ddd2e942"
            "${WF_DIFFERENTIAL_DIR}/oracle/src/config.h|0d8cfdc0d247f9abe90ab9e7b7c8ea6f2a2b29620d303960ce93992cd8cde272"
            "${WF_DIFFERENTIAL_DIR}/oracle/src/config_sanitize.h|8e353fcda0243ec40cf7de7186d96cc9c3efdb76848d4ec21e9e71e2a2b3e580"
            "${WF_DIFFERENTIAL_DIR}/oracle/src/logging.h|c767b785d002514792aa418160b1ace1722d613f9fc795e81ae5481acbb9dfe4"
            "${WF_CORE_DIR}/include/cameraunlock/config/ini_reader.h|a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"
            "${WF_CORE_DIR}/src/config/ini_reader.cpp|e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"
            "${WF_CORE_DIR}/include/cameraunlock/protocol/port_utils.h|91bff564d5e279b66527ec5553afcf4d591812dab0e78f71a48ef412e4db7a44"
            "${WF_CORE_DIR}/include/cameraunlock/math/smoothing_utils.h|fc2146f8c585e5f610c7234e302f59de4945679cfa28ff479ca47477ec073f22"
            "${WF_CORE_DIR}/include/cameraunlock/math/angle_utils.h|d7a905270933e3cb0c4c361d29d3fd701655498cbcd1875ea79d180468bdbe6a"
            "${WF_CORE_DIR}/include/cameraunlock/data/position_settings.h|b24dceb8e25475aebc5a468a5c7362a4a4e64204d183d1408525345f32f547f5"
            "${WF_CORE_DIR}/include/cameraunlock/logging/file_log.h|43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"
            "${WF_CORE_DIR}/src/logging/file_log.cpp|73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.h|178666b5fabef7cc8fe76ba13f5e70ae52670be4bbf124de178e669cf7615d23"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.cpp|726e6030618abc01d81d737f151c348c6fc87cd8566947963ba03c59c60e169f"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/config_sanitize.h|38170fe2b0e836fa0b5d01db8f6588b35d665ce7429d589b6ab84723cb2ff68a")
        string(REPLACE "|" ";" pair "${pinned}")
        list(GET pair 0 pinned_path)
        list(GET pair 1 pinned_hash)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pinned_path}")
        wf_require_hash("${pinned_path}" "${pinned_hash}")
    endforeach()

    # Built from what wf_add_test registered, so a new test cannot be added to
    # ctest and left out of the target run-tests.ps1 builds.
    get_property(wf_test_targets GLOBAL PROPERTY WF_TEST_TARGETS)
    add_custom_target(wf_tests DEPENDS ${wf_test_targets})
endif()
