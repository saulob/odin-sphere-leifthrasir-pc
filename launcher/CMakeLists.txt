# The launcher: TRG Launcher (fetched by tools\build.ps1, built from its copy patched with gpu/patches/trg-launcher)
# in a window of its own that is closed before the loader creates the game window.
set(TRG_DIR ${OSL_SOURCE_DIR_trg-launcher})
set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/gpu/third_party/imgui)

# TRG's settings file, platform helpers and HTTPS downloads do not need Dear ImGui.
add_library(trg_core STATIC ${TRG_DIR}/src/settings.cpp ${TRG_DIR}/src/platform.cpp ${TRG_DIR}/src/download.cpp)
target_include_directories(trg_core PUBLIC ${TRG_DIR}/include)
target_compile_definitions(trg_core PRIVATE _CRT_SECURE_NO_WARNINGS)
if (WIN32)
    target_link_libraries(trg_core PRIVATE shell32 ole32 comdlg32 winhttp)
else()
    target_link_libraries(trg_core PRIVATE Threads::Threads)
endif()

# The launcher draws with the Dear ImGui compiled into the video core library.
add_library(trg_launcher STATIC ${TRG_DIR}/src/launcher.cpp ${TRG_DIR}/src/headers.cpp)
target_include_directories(trg_launcher PUBLIC ${IMGUI_DIR})
target_compile_definitions(trg_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(trg_launcher PUBLIC trg_core oslgpu)

# Written for C++17: in C++20 and later u8string() is a different type.
set_target_properties(trg_core trg_launcher PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

add_library(osl_imgui_sdl STATIC ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(osl_imgui_sdl PUBLIC ${IMGUI_DIR})
target_link_libraries(osl_imgui_sdl PUBLIC SDL3::SDL3)

# Game files (check, PKG unpacking, install, boot image), the trophies (src/trophies), the settings file and the
# Display options: no window needed (tests link only this).
find_package(ZLIB REQUIRED)
add_library(osl_launcher_core STATIC bookshelf.c boot_image.cpp controller_prompts.cpp cpk_patch.cpp file_browser.cpp game_files.cpp
    game_install.cpp game_profile.cpp options.cpp paths.cpp pkg_extract.cpp resolution.c settings.cpp skip_intro.c
    trophy_content.cpp
    ${PROJECT_SOURCE_DIR}/src/trophies/trophy_data.cpp ${PROJECT_SOURCE_DIR}/src/trophies/trophy_key.cpp
    ${PROJECT_SOURCE_DIR}/src/trophies/trophy_pack.cpp ${PROJECT_SOURCE_DIR}/src/trophies/trophy_runtime.cpp
    ${PROJECT_SOURCE_DIR}/src/trophies/trophy_service.cpp ${PROJECT_SOURCE_DIR}/src/trophies/trophy_store.cpp
    ${PROJECT_SOURCE_DIR}/src/trophies/trophy_view.cpp ${PROJECT_SOURCE_DIR}/src/trophies/trp_archive.cpp)
target_include_directories(osl_launcher_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(osl_launcher_core PUBLIC trg_core SDL3::SDL3 osl_keys PRIVATE ZLIB::ZLIB)
# Hashes and ciphers: CNG on Windows, OpenSSL's libcrypto elsewhere.
if (WIN32)
    target_sources(osl_launcher_core PRIVATE crypto_win.cpp)
    target_link_libraries(osl_launcher_core PRIVATE bcrypt)
    set(OSL_LOG_SOURCE ${PROJECT_SOURCE_DIR}/src/runtime_log.c)
else()
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    target_sources(osl_launcher_core PRIVATE crypto_openssl.cpp)
    target_link_libraries(osl_launcher_core PRIVATE OpenSSL::Crypto)
    set(OSL_LOG_SOURCE ${PROJECT_SOURCE_DIR}/src/runtime_log_posix.c)
endif()
set(OSL_LOG_SOURCES ${OSL_LOG_SOURCE} ${PROJECT_SOURCE_DIR}/src/runtime_session_log.c)

# The Controller prompts patch (prompts/xbox-prompts.dcpx) as a byte array, prompts_patch.h.
set(OSL_PROMPTS_PATCH ${CMAKE_CURRENT_SOURCE_DIR}/prompts/xbox-prompts.dcpx)
set(OSL_PROMPTS_PATCH_SOURCE ${CMAKE_BINARY_DIR}/generated/prompts_patch_data.cpp)
add_custom_command(OUTPUT ${OSL_PROMPTS_PATCH_SOURCE}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${OSL_PROMPTS_PATCH} -DOUTPUT=${OSL_PROMPTS_PATCH_SOURCE} -DSYMBOL=kXboxPromptsPatch
            -P ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
    DEPENDS ${OSL_PROMPTS_PATCH} ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
    COMMENT "Embedding prompts/xbox-prompts.dcpx")

# The launcher's self-update from GitHub releases (update_config.cpp, updater/).
set(OSL_UPDATER_SOURCES updater/github_release.cpp updater/json.cpp updater/sha256.cpp updater/update_controller.cpp
    updater/update_job.cpp updater/update_paths.cpp updater/update_platform.cpp updater/update_zip.cpp updater/version.cpp)

add_library(osl_launcher STATIC host.cpp launcher.cpp package_browser.cpp problem_dialog.cpp shift_key.cpp trophy_page.cpp
    update_config.cpp ${OSL_UPDATER_SOURCES} updater/update_ui.cpp ${OSL_PROMPTS_PATCH_SOURCE})
target_compile_definitions(osl_launcher PRIVATE OSL_PORT_VERSION="${OSL_PORT_VERSION}")
target_include_directories(osl_launcher PRIVATE ${PROJECT_SOURCE_DIR}/gpu/shim)
target_link_libraries(osl_launcher PUBLIC osl_launcher_core trg_launcher osl_imgui_sdl miniz ${CMAKE_DL_LIBS})

# Windows: the helper that replaces the program after the launcher exits. It runs alone from a temporary folder,
# so it is linked statically and imports only Windows' own DLLs (checked by tools\package.ps1 and the helper tests).
# Release packages ship tools\build-updater.ps1's build of these sources instead of this target.
if (WIN32)
    set(OSL_UPDATER_HELPER_SOURCES updater/helper_main.cpp updater/install_transaction.cpp updater/sha256.cpp
        updater/update_job.cpp updater/update_paths.cpp updater/update_platform.cpp)
    add_executable(trg-updater WIN32 ${OSL_UPDATER_HELPER_SOURCES})
    target_include_directories(trg-updater PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(trg-updater PRIVATE OSL_UPDATER_HELPER_BUILD=1)
    target_compile_options(trg-updater PRIVATE -Wall -Wextra)
    target_link_libraries(trg-updater PRIVATE shell32)
    target_link_options(trg-updater PRIVATE -static)
    set_target_properties(trg-updater PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
endif()

set_target_properties(osl_launcher_core osl_launcher PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(osl_launcher_core PRIVATE -Wall -Wextra)
target_compile_options(osl_launcher PRIVATE -Wall -Wextra)

# Focused updater tests (tools\test.ps1): versions, release JSON, checksums, ZIP safety, protected paths, the install
# transaction, rollback and the update configuration; no network. Not part of the default build.
add_executable(osl-update-tests EXCLUDE_FROM_ALL ../tests/update_tests.cpp update_config.cpp ${OSL_UPDATER_SOURCES}
    updater/install_transaction.cpp)
target_compile_definitions(osl-update-tests PRIVATE OSL_UPDATER_HELPER_BUILD=1 OSL_PORT_VERSION="${OSL_PORT_VERSION}"
    OSL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(osl-update-tests PRIVATE osl_launcher_core miniz)
set_target_properties(osl-update-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(osl-update-tests PRIVATE -Wall -Wextra)

# The real trg-updater.exe run alone from a temporary folder: imports, parent wait, replacement, restart quoting,
# test variable removal and rollback. The marker stands in for the launcher it waits for and restarts.
if (WIN32)
    add_executable(osl-update-restart-marker EXCLUDE_FROM_ALL WIN32 ../tests/update_restart_marker.cpp)
    target_link_libraries(osl-update-restart-marker PRIVATE shell32)
    target_link_options(osl-update-restart-marker PRIVATE -static)
    set_target_properties(osl-update-restart-marker PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_compile_options(osl-update-restart-marker PRIVATE -Wall -Wextra)

    add_executable(osl-update-helper-tests EXCLUDE_FROM_ALL ../tests/update_helper_tests.cpp)
    target_compile_definitions(osl-update-helper-tests PRIVATE OSL_UPDATER_EXE="$<TARGET_FILE:trg-updater>"
        OSL_MARKER_EXE="$<TARGET_FILE:osl-update-restart-marker>")
    target_link_libraries(osl-update-helper-tests PRIVATE shell32)
    add_dependencies(osl-update-helper-tests trg-updater osl-update-restart-marker)
    set_target_properties(osl-update-helper-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_compile_options(osl-update-helper-tests PRIVATE -Wall -Wextra)
endif()

# Focused tests of the local trophies (tools\test.ps1); not part of the default build.
find_package(PNG REQUIRED)
add_executable(osl-trophy-tests EXCLUDE_FROM_ALL ../tests/trophy_tests.cpp ${OSL_LOG_SOURCES} ../gpu/shim/osl_png.cpp)
target_link_libraries(osl-trophy-tests PRIVATE osl_launcher_core PNG::PNG)
target_include_directories(osl-trophy-tests PRIVATE ${PROJECT_SOURCE_DIR}/gpu/shim)
target_compile_definitions(osl-trophy-tests PRIVATE OSL_TROPHY_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/trophies")
set_target_properties(osl-trophy-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(osl-trophy-tests PRIVATE -Wall -Wextra)
