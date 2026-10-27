#!/usr/bin/env bash
# Builds odin_sphere_leifthrasir on Linux x86_64 incrementally (Clang, CMake + Ninja): the Linux counterpart of
# tools/build.ps1. The one build configuration, RelWithDebInfo, is what is tested and what an AppImage contains.
#
#   tools/build.sh            build build/cmake/linux/bin/odin_sphere_leifthrasir
#   tools/build.sh --clean    delete build/cmake/linux first
#   tools/build.sh --test     also build and run the focused tests
#
# Missing tools or libraries are listed; packaging/linux/ci-deps.sh installs them on Ubuntu 20.04 (the release baseline).
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$root/build/cmake/linux
config=RelWithDebInfo
clean=0 test=0
while (($#)); do
    case $1 in
        --clean) clean=1 ;;
        --test) test=1 ;;
        -h|--help) sed -n '2,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

step() { printf '\n==> %s\n' "$*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || die 'This script builds for Linux x86_64 only (Windows: tools\build.ps1).'

cc=${CC:-clang} cxx=${CXX:-clang++}
missing=()
for tool in "$cc" "$cxx" cmake ninja pkg-config git; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
command -v glslang >/dev/null || command -v glslangValidator >/dev/null || missing+=(glslang)
if command -v pkg-config >/dev/null; then
    for module in sdl3 freetype2 libpng zlib openssl libxxhash vulkan; do
        pkg-config --exists "$module" || missing+=("pkg-config:$module")
    done
fi
if ((${#missing[@]})); then
    echo "Missing: ${missing[*]}" >&2
    echo 'packaging/linux/ci-deps.sh prepares the Ubuntu 20.04 release build environment (docs/LINUX.md).' >&2
    exit 1
fi

step "Toolchain $($cc --version | head -1), $config"

# As tools\build.ps1: a fetched dependency stays the pristine checkout of its pinned commit, recorded in .osl_commit;
# CMake builds the patched copies (cmake/OslPinnedSources.cmake).
step 'Pinned sources (gpu/deps.txt)'
while read -r path url commit; do
    [[ -z ${path:-} || $path == \#* ]] && continue
    dir=$root/gpu/$path
    [[ -f $dir/.osl_commit && $(<"$dir/.osl_commit") == "$commit" ]] && continue
    echo "Fetching $path (${commit:0:7})"
    rm -rf "$dir"
    mkdir -p "$dir"
    git -C "$dir" init --quiet
    git -C "$dir" fetch --quiet --depth 1 "$url" "$commit"
    git -C "$dir" -c advice.detachedHead=false checkout --quiet FETCH_HEAD
    [[ -f $dir/.gitmodules ]] && git -C "$dir" submodule update --quiet --init --recursive --depth 1
    printf '%s' "$commit" > "$dir/.osl_commit"
done < "$root/gpu/deps.txt"

((clean)) && { step 'Cleaning build/cmake/linux'; rm -rf "$build_dir"; }
if ! grep -qs "CMAKE_BUILD_TYPE:STRING=$config" "$build_dir/CMakeCache.txt" || [[ ! -f $build_dir/build.ninja ]]; then
    step 'Configuring'
    cmake -S "$root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=$config \
        -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx"
fi
step 'Building'
targets=(odin_sphere_leifthrasir)
((test)) && targets+=(osl-tests osl-trophy-tests osl-window-tests osl-cursor-tests osl-runtime-tests osl-tiling-tests
                      osl-update-tests)
ninja -C "$build_dir" "${targets[@]}"
loader=$build_dir/bin/odin_sphere_leifthrasir
[[ -x $loader ]] || die "Missing $loader"

if ((test)); then
    step 'Running the patch workflow tests'
    cmake "-DOSL_TEST_DIR=$build_dir/patch-workflow-tests" -P "$root/tests/patch_workflow_tests.cmake"
    for test_program in osl-tests osl-trophy-tests osl-window-tests osl-cursor-tests osl-runtime-tests osl-tiling-tests \
                        osl-update-tests; do
        step "Running $test_program"
        "$build_dir/bin/$test_program"
    done
fi
printf '\nBuilt %s\n' "$loader"
