cmake_minimum_required(VERSION 3.24)
project(odin_sphere_leifthrasir_pc LANGUAGES C CXX)
set(OSL_PORT_VERSION "0.1.0")

if (NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Build with Clang: MSYS2 CLANG64 (tools\\build.ps1) on Windows, tools/build.sh on Linux.")
endif()
if (NOT WIN32 AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$"))
    message(FATAL_ERROR "Supported targets: Windows x64 and Linux x86_64.")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if (WIN32)
    set(CMAKE_C_EXTENSIONS OFF)
else()
    set(CMAKE_C_EXTENSIONS ON) # POSIX interfaces (sigjmp_buf, memfd_create) are hidden in strict ISO mode
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# Repository paths in __FILE__ and std::source_location stay out of the binary; debug info keeps them.
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=>")
if (NOT WIN32)
    # Non-PIE: the heap handed to the guest (thread handles, TLS) must stay below 1 TiB (loader.c).
    set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-fno-pie>")
    add_link_options(-no-pie)
endif()

if (NOT OSL_PORT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    message(FATAL_ERROR "OSL_PORT_VERSION must start with MAJOR.MINOR.PATCH")
endif()
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/osl_version.h CONTENT "#define OSL_VERSION_NUMBERS \
${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0\n#define OSL_FILE_VERSION_STRING \
\"${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}.0\"\n#define OSL_VERSION_STRING \"${OSL_PORT_VERSION}\"\n")

include(cmake/OslPinnedSources.cmake)
osl_pinned_sources()

find_package(SDL3 CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(Threads REQUIRED)

# Keyboard bindings shared by the launcher, the runtime pad and the video core.
add_library(osl_keys STATIC src/osl_keys.c)
target_include_directories(osl_keys PUBLIC src)
target_link_libraries(osl_keys PUBLIC SDL3::SDL3)
target_compile_options(osl_keys PRIVATE -Wall -Wextra -Werror)

add_subdirectory(gpu)
add_subdirectory(launcher)

set(OSL_RUNTIME_SOURCES
    src/runtime.c
    src/runtime_audio.c
    src/runtime_eventflag.c
    src/runtime_fiber.c
    src/runtime_file.c
    src/runtime_font.c
    src/runtime_kernel.c
    src/runtime_memory.c
    src/runtime_mutex.c
    src/runtime_pad.c
    src/runtime_platform.c
    src/runtime_redzone.c
    src/runtime_rtc.c
    src/runtime_rwlock.c
    src/runtime_savedata.c
    src/runtime_session_log.c
    src/runtime_sema.c
    src/runtime_services.c
    src/runtime_thread.c
    src/runtime_time.c)
# The built-in system fonts are assembled into runtime_font.c (.incbin): rebuild it when a font changes.
set(OSL_NOTO_SANS ${CMAKE_SOURCE_DIR}/third_party/fonts/NotoSans-Regular.ttf)
set(OSL_NOTO_SANS_TC ${CMAKE_SOURCE_DIR}/third_party/fonts/NotoSansTC-Regular.otf)
set_source_files_properties(src/runtime_font.c PROPERTIES
    COMPILE_DEFINITIONS "OSL_NOTO_SANS_PATH=\"${OSL_NOTO_SANS}\";OSL_NOTO_SANS_TC_PATH=\"${OSL_NOTO_SANS_TC}\""
    OBJECT_DEPENDS "${OSL_NOTO_SANS};${OSL_NOTO_SANS_TC}")

if (WIN32)
    enable_language(RC)
    # Relink when the application icon, the manifest or the version changes.
    set_source_files_properties(src/odin_sphere_leifthrasir.rc PROPERTIES OBJECT_DEPENDS
        "${CMAKE_SOURCE_DIR}/packaging/icons/app-icon.ico;${CMAKE_SOURCE_DIR}/src/odin_sphere_leifthrasir.manifest;${CMAKE_BINARY_DIR}/generated/osl_version.h")
    set(OSL_PLATFORM_SOURCES src/compat_win.c src/runtime_log.c src/hang_watchdog.c src/odin_sphere_leifthrasir.rc)
else()
    set(OSL_PLATFORM_SOURCES src/runtime_log_posix.c)
endif()
set(OSL_MAIN_SOURCES src/loader.c src/crash_report.c src/hang_report.c src/isa_emulate.c src/lzcnt_compat.c ${OSL_RUNTIME_SOURCES} ${OSL_PLATFORM_SOURCES})
if (WIN32)
    # Windows subsystem: no console window. The run log takes stdout and stderr (src/runtime_log.c).
    add_executable(odin_sphere_leifthrasir WIN32 ${OSL_MAIN_SOURCES})
else()
    add_executable(odin_sphere_leifthrasir ${OSL_MAIN_SOURCES})
endif()
set_target_properties(odin_sphere_leifthrasir PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(odin_sphere_leifthrasir PRIVATE src launcher ${CMAKE_BINARY_DIR}/generated)
target_compile_options(odin_sphere_leifthrasir PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Werror -fno-omit-frame-pointer>)
target_compile_options(odin_sphere_leifthrasir PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
if (WIN32)
    # Windows 10 1903+ APIs (VirtualAlloc2, MapViewOfFile3).
    target_compile_definitions(odin_sphere_leifthrasir PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000007)
    # The PS4 user range (64 GiB - 1 TiB) must stay free of host allocations: no high-entropy ASLR.
    # The guest main thread runs on the process stack: 16 MiB, committed (guest code has no stack probes).
    target_link_options(odin_sphere_leifthrasir PRIVATE -Wl,--disable-high-entropy-va "SHELL:-Xlinker --stack -Xlinker 16777216,16777216")
    target_link_libraries(odin_sphere_leifthrasir PRIVATE osl_launcher oslgpu SDL3::SDL3 Freetype::Freetype onecore bcrypt ws2_32 -pthread)
else()
    # -rdynamic: fault reports name host symbols (dladdr).
    target_link_options(odin_sphere_leifthrasir PRIVATE -rdynamic)
    target_link_libraries(odin_sphere_leifthrasir PRIVATE osl_launcher oslgpu SDL3::SDL3 Freetype::Freetype Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# A release folder holds the update helper next to the program (launcher/CMakeLists.txt).
if (WIN32)
    add_dependencies(odin_sphere_leifthrasir trg-updater)
endif()

# Windows Release: ThinLTO across the program and the project libraries it is linked from, with the LTO backend at
# -O3 like the compile. Without debug information the program has no use for a COFF symbol table or a build id.
if (WIN32)
    foreach(target odin_sphere_leifthrasir oslgpu osl_launcher osl_launcher_core trg_launcher trg_core osl_imgui_sdl osl_keys)
        target_compile_options(${target} PRIVATE "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C,CXX>>:-flto=thin>")
    endforeach()
    set(OSL_RELEASE_LINK -flto=thin -Wl,--lto-O3 "-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/lto-cache" -Wl,--strip-all
        -Wl,--build-id=none)
    target_link_options(odin_sphere_leifthrasir PRIVATE "$<$<CONFIG:Release>:${OSL_RELEASE_LINK}>")
endif()

# The launcher reads its images from <exe dir>/assets/launcher.
add_custom_command(TARGET odin_sphere_leifthrasir POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:odin_sphere_leifthrasir>/assets/launcher"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/launcher/header-art.png"
        "$<TARGET_FILE_DIR:odin_sphere_leifthrasir>/assets/launcher/header-art.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/launcher/sponsor-heart.png"
        "$<TARGET_FILE_DIR:odin_sphere_leifthrasir>/assets/launcher/sponsor-heart.png"
    VERBATIM)

# Focused tests of the game profile, the settings file, the keys and the window fit (tools\test.ps1);
# not part of the default build.
if (WIN32)
    set(OSL_TEST_LOG_SOURCES src/runtime_log.c)
else()
    set(OSL_TEST_LOG_SOURCES src/runtime_log_posix.c)
endif()
set(OSL_TEST_LOG_SOURCES ${OSL_TEST_LOG_SOURCES} src/runtime_session_log.c src/crash_report.c src/hang_report.c)
add_executable(osl-tests EXCLUDE_FROM_ALL tests/game_tests.cpp ${OSL_TEST_LOG_SOURCES})
set_target_properties(osl-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(osl-tests PRIVATE src launcher gpu/shim)
target_compile_definitions(osl-tests PRIVATE OSL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_compile_options(osl-tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(osl-tests PRIVATE osl_launcher_core SDL3::SDL3)

# Focused tests of the run log's retention on the open log file (tools\test.ps1); not part of the default build.
add_executable(osl-log-tests EXCLUDE_FROM_ALL tests/log_tests.c ${OSL_TEST_LOG_SOURCES})
target_include_directories(osl-log-tests PRIVATE src)
target_compile_options(osl-log-tests PRIVATE -Wall -Wextra -Werror)
if (WIN32)
    add_executable(osl-log-gui-probe WIN32 EXCLUDE_FROM_ALL tests/log_gui_probe.c ${OSL_TEST_LOG_SOURCES})
    target_include_directories(osl-log-gui-probe PRIVATE src)
    target_compile_options(osl-log-gui-probe PRIVATE -Wall -Wextra -Werror)
    add_dependencies(osl-log-tests osl-log-gui-probe)
    target_compile_definitions(osl-log-tests PRIVATE OSL_LOG_GUI_PROBE="$<TARGET_FILE:osl-log-gui-probe>")
else()
    target_link_libraries(osl-log-tests PRIVATE Threads::Threads)
endif()

# Focused tests of the MOVBE and BMI1 emulation and the LZCNT rewrite used when the host CPU lacks the instructions, on
# synthetic images and the local boot images (tools\test.ps1); not part of the default build.
add_executable(osl-isa-tests EXCLUDE_FROM_ALL tests/isa_tests.c src/isa_emulate.c src/lzcnt_compat.c)
target_include_directories(osl-isa-tests PRIVATE src)
target_compile_definitions(osl-isa-tests PRIVATE OSL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(osl-isa-tests PRIVATE -Wall -Wextra -Werror)

# Focused tests of the GPU tiling parameters and macro-tiled addresses (tools\test.ps1); not part of the default build.
add_executable(osl-tiling-tests EXCLUDE_FROM_ALL tests/tiling_tests.cpp)
set_target_properties(osl-tiling-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(osl-tiling-tests PRIVATE gpu/shim gpu/shadps4 tests src ${CMAKE_BINARY_DIR}/gpu/include
    gpu/third_party/gcn/include gpu/third_party/half/include gpu/third_party/xbyak)
target_compile_options(osl-tiling-tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(osl-tiling-tests PRIVATE oslgpu)

# Focused tests of the runtime's locks and guest memory table (tools/test.ps1); not part of the default build.
add_executable(osl-runtime-tests EXCLUDE_FROM_ALL tests/runtime_sync_tests.c src/runtime_rwlock.c src/runtime_mutex.c src/runtime_eventflag.c)
target_include_directories(osl-runtime-tests PRIVATE src)
target_compile_options(osl-runtime-tests PRIVATE -Wall -Wextra -Werror)
if (WIN32)
    target_compile_definitions(osl-runtime-tests PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000007)
    target_link_libraries(osl-runtime-tests PRIVATE onecore -pthread)
else()
    target_link_libraries(osl-runtime-tests PRIVATE Threads::Threads)
endif()

# Focused tests of the sparse Windows guest memory backing (tools/test.ps1); not part of the default build.
if (WIN32)
    add_executable(osl-memory-windows-tests EXCLUDE_FROM_ALL tests/memory_windows_tests.c)
    target_include_directories(osl-memory-windows-tests PRIVATE src)
    target_compile_definitions(osl-memory-windows-tests PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000007)
    target_compile_options(osl-memory-windows-tests PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(osl-memory-windows-tests PRIVATE onecore -pthread)
    target_link_options(osl-memory-windows-tests PRIVATE -Wl,--disable-high-entropy-va)
endif()

# Focused tests of the game window's cursor visibility (tools/test.ps1); not part of the default build.
add_executable(osl-cursor-tests EXCLUDE_FROM_ALL tests/cursor_tests.cpp)
set_target_properties(osl-cursor-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(osl-cursor-tests PRIVATE gpu/shim)
target_compile_definitions(osl-cursor-tests PRIVATE OSL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(osl-cursor-tests PRIVATE -Wall -Wextra -Werror)

# Focused tests of the startup Gameplay notices and frame-counter visibility gating; not part of the default build.
add_executable(osl-notice-tests EXCLUDE_FROM_ALL tests/notice_tests.cpp)
set_target_properties(osl-notice-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(osl-notice-tests PRIVATE gpu/shim)
target_compile_options(osl-notice-tests PRIVATE -Wall -Wextra -Werror)

# Focused tests of the windowed-mode size and centring arithmetic; not part of the default build.
add_executable(osl-window-tests EXCLUDE_FROM_ALL tests/window_fit_tests.cpp)
set_target_properties(osl-window-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(osl-window-tests PRIVATE src)
target_compile_options(osl-window-tests PRIVATE -Wall -Wextra -Werror)
