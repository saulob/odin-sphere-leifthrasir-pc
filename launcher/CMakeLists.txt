# The launcher: TRG Launcher (third_party/trg-launcher, fetched by tools\build.ps1) in a window of its own that is
# closed before the loader creates the game window.
set(TRG_DIR ${PROJECT_SOURCE_DIR}/third_party/trg-launcher)
if (NOT EXISTS ${TRG_DIR}/include/trg/launcher.h)
    message(FATAL_ERROR "TRG Launcher is missing: run tools\\build.ps1, which fetches it (gpu\\deps.txt).")
endif()
set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/gpu/third_party/imgui)

# TRG's settings file and platform helpers do not need Dear ImGui.
add_library(trg_core STATIC ${TRG_DIR}/src/settings.cpp ${TRG_DIR}/src/platform.cpp)
target_include_directories(trg_core PUBLIC ${TRG_DIR}/include)
target_compile_definitions(trg_core PRIVATE _CRT_SECURE_NO_WARNINGS)
if (WIN32)
    target_link_libraries(trg_core PRIVATE shell32 ole32 comdlg32)
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

# Game files (check, PKG unpacking, install, boot image), the settings file and the Display options: no window needed
# (tests link only this).
find_package(ZLIB REQUIRED)
add_library(osl_launcher_core STATIC boot_image.cpp file_browser.cpp game_files.cpp game_install.cpp game_profile.cpp
    options.cpp paths.cpp pkg_extract.cpp settings.cpp)
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

add_library(osl_launcher STATIC host.cpp launcher.cpp package_browser.cpp problem_dialog.cpp shift_key.cpp)
target_compile_definitions(osl_launcher PRIVATE OSL_PORT_VERSION="${OSL_PORT_VERSION}")
target_include_directories(osl_launcher PRIVATE ${PROJECT_SOURCE_DIR}/gpu/shim)
target_link_libraries(osl_launcher PUBLIC osl_launcher_core trg_launcher osl_imgui_sdl ${CMAKE_DL_LIBS})

set_target_properties(osl_launcher_core osl_launcher PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(osl_launcher_core PRIVATE -Wall -Wextra)
target_compile_options(osl_launcher PRIVATE -Wall -Wextra)
