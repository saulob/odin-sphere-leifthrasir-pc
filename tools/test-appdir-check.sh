#!/usr/bin/env bash
# Checks that tools/package-linux.sh --verify accepts a clean synthetic AppDir and refuses it once it holds game data,
# the player's files, logs, debug files, development markers, local paths (also as UTF-16), system libraries, links,
# files outside the allowed places, an ELF that needs a newer glibc than the baseline, or lacks a required file. The fixture programs are tiny C files compiled here; nothing of the
# project is built, packaged or started. Needs cc, readelf and ldd.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
checker=$root/tools/package-linux.sh
doc=OdinSphereLeifthrasirPC
for tool in cc readelf ldd; do
    command -v "$tool" >/dev/null || { echo "Missing $tool" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The program needs nothing newer than GLIBC_2.2.5 and finds its library through $ORIGIN/../lib, as a release does.
cat > "$work/program.c" <<'EOF'
#include <unistd.h>
int fixture_value(void);
void _start(void) { _exit(fixture_value()); }
EOF
echo 'int fixture_value(void) { return 0; }' > "$work/library.c"
echo 'int main(void) { return 0; }' > "$work/modern.c"
cc -O1 -shared -fPIC -o "$work/libfixture.so" "$work/library.c"
cc -O1 -shared -fPIC -o "$work/libgone.so" "$work/library.c"
cc -O1 -nostartfiles -o "$work/program" "$work/program.c" -L"$work" -lfixture -Wl,--disable-new-dtags \
    -Wl,-rpath,'$ORIGIN/../lib'
cc -O1 -nostartfiles -o "$work/unresolved" "$work/program.c" -Wl,--no-as-needed -L"$work" -lfixture -lgone \
    -Wl,--disable-new-dtags -Wl,-rpath,'$ORIGIN/../lib'
cc -O1 -nostartfiles -o "$work/runpath" "$work/program.c" -L"$work" -lfixture -Wl,-rpath,'$ORIGIN/../lib'
cc -O1 -g -shared -fPIC -o "$work/libdebug.so" "$work/library.c"
cc -O1 -shared -fPIC -Wl,-rpath,/usr/local/lib -o "$work/libabsolute.so" "$work/library.c"
# A plain program built on a current distribution needs __libc_start_main@GLIBC_2.34.
cc -O1 -o "$work/modern" "$work/modern.c"
modern_glibc=$(readelf --version-info -W "$work/modern" | grep -oE 'GLIBC_[0-9.]+' | sed 's/^GLIBC_//' | sort -V | tail -1)
[[ $(printf '%s\n' "${modern_glibc:-0}" 2.31 | sort -V | tail -1) != 2.31 ]] || modern_glibc=

clean=$work/clean
mkdir -p "$clean/usr/bin/assets/launcher" "$clean/usr/lib/compat" "$clean/usr/share/licenses/$doc/NotoSansTC" \
         "$clean/usr/share/doc/$doc" "$clean/usr/share/applications"
printf '#!/bin/sh\nexec "$(dirname "$0")/usr/bin/odin_sphere_leifthrasir" "$@"\n' > "$clean/AppRun"
cp "$root/packaging/linux/odin-sphere-leifthrasir-pc.desktop" "$clean/odin-sphere-leifthrasir-pc.desktop"
cp "$root/packaging/icons/app-icon-256.png" "$clean/odin-sphere-leifthrasir-pc.png"
ln -s odin-sphere-leifthrasir-pc.png "$clean/.DirIcon"
cp "$work/program" "$clean/usr/bin/odin_sphere_leifthrasir"
cp "$work/libfixture.so" "$clean/usr/lib/libfixture.so"
cp "$root/assets/launcher/header-art.png" "$clean/usr/bin/assets/launcher/header-art.png"
cp "$root/assets/launcher/sponsor-heart.png" "$clean/usr/bin/assets/launcher/sponsor-heart.png"
for file in LICENSE.txt THIRD-PARTY-LICENSES.txt THIRD-PARTY-LICENSES-LINUX.txt NotoSansTC/LICENSE-NotoSansTC.txt; do
    echo 'licence text' > "$clean/usr/share/licenses/$doc/$file"
done
echo 'Odin Sphere Leifthrasir - PC port (Linux)' > "$clean/usr/share/doc/$doc/README.txt"
chmod 755 "$clean/AppRun" "$clean/usr/bin/odin_sphere_leifthrasir"

# Each case changes one copy of the clean AppDir.
contaminate() { # case dir
    local dir=$2
    case $1 in
        settings) mkdir -p "$dir/user" && echo 'x' > "$dir/user/osl.ini" ;;
        'save data') mkdir -p "$dir/usr/share/savedata/CUSA05083" ;;
        trophies) mkdir -p "$dir/usr/share/trophy/NPWR10696_00" ;;
        'trophy key') echo '{}' > "$dir/usr/bin/keys.json" ;;
        'shader cache') mkdir -p "$dir/cache/CUSA05083" && echo 'x' > "$dir/cache/CUSA05083/pipeline" ;;
        log) mkdir -p "$dir/logs" && echo 'x' > "$dir/logs/odin_sphere_leifthrasir_001.log" ;;
        'hang report') echo 'x' > "$dir/usr/share/doc/$doc/hang_001.txt" ;;
        'game files') mkdir -p "$dir/game/prepared/CUSA05083-1.00" && echo 'x' > "$dir/game/prepared/CUSA05083-1.00/eboot.bin" ;;
        'game package') echo 'x' > "$dir/UP0177-CUSA05083.pkg" ;;
        'package by content') printf '\x7fCNT\0\0\0\0' > "$dir/usr/share/licenses/$doc/notes" ;;
        'SELF by content') printf '\x4f\x15\x3d\x1d\0\0\0\0' > "$dir/usr/share/licenses/$doc/module" ;;
        'debug file') echo 'x' > "$dir/usr/bin/odin_sphere_leifthrasir.debug" ;;
        'pdb') echo 'x' > "$dir/usr/bin/odin_sphere_leifthrasir.pdb" ;;
        'object file') echo 'x' > "$dir/usr/lib/loader.o" ;;
        'static library') echo 'x' > "$dir/usr/lib/libosl_launcher.a" ;;
        'source file') echo 'x' > "$dir/usr/bin/loader.c" ;;
        'python cache') mkdir -p "$dir/usr/bin/__pycache__" ;;
        'build folder') mkdir -p "$dir/usr/bin/CMakeFiles" ;;
        'nested AppImage') echo 'x' > "$dir/usr/bin/OdinSphereLeifthrasirPC-v0.1.0-x86_64.AppImage" ;;
        'unexpected file') echo 'x' > "$dir/usr/bin/notes.txt" ;;
        'symbolic link') ln -s /usr/lib/x86_64-linux-gnu/libz.so.1 "$dir/usr/lib/libextra.so.1" ;;
        'updater leftover') echo 'x' > "$dir/odin.trg-update-1-0.backup" ;;
        'crash dump') echo 'x' > "$dir/usr/share/doc/$doc/odin_sphere_leifthrasir.dmp" ;;
        'UTF-16 repository path') printf '%s' "$root" | sed 's/./&\x00/g' > "$dir/usr/share/licenses/$doc/notes" ;;
        'Windows user folder') echo 'C:\Users\someone' >> "$dir/usr/share/licenses/$doc/LICENSE.txt" ;;
        'Windows program') printf 'MZ\x90\0' > "$dir/usr/bin/trg-updater" ;;
        renderdoc) cp "$work/libfixture.so" "$dir/usr/lib/librenderdoc.so" ;;
        'vulkan layer') mkdir -p "$dir/usr/share/vulkan/implicit_layer.d" && echo '{}' > "$dir/usr/share/vulkan/implicit_layer.d/VkLayer_x.json" ;;
        'vulkan loader') cp "$work/libfixture.so" "$dir/usr/lib/libvulkan.so.1" ;;
        glibc) cp "$work/libfixture.so" "$dir/usr/lib/libc.so.6" ;;
        'newer glibc') cp "$work/modern" "$dir/usr/bin/helper" ;;
        'debug sections') cp "$work/libdebug.so" "$dir/usr/lib/libfixture.so" ;;
        'absolute RPATH') cp "$work/libabsolute.so" "$dir/usr/lib/libfixture.so" ;;
        'RUNPATH instead of RPATH') cp "$work/runpath" "$dir/usr/bin/odin_sphere_leifthrasir" ;;
        'unresolved library') cp "$work/unresolved" "$dir/usr/bin/odin_sphere_leifthrasir" ;;
        'DEBUG_SAULO marker') echo 'DEBUG_SAULO' >> "$dir/usr/share/doc/$doc/README.txt" ;;
        'repository path') echo "$root/build" >> "$dir/usr/share/licenses/$doc/LICENSE.txt" ;;
        'home path') echo "${HOME:-/nonexistent-home}/build" >> "$dir/usr/share/licenses/$doc/LICENSE.txt" ;;
        'obsolete program name') echo 'osl-loader' >> "$dir/usr/share/doc/$doc/README.txt" ;;
        'missing AppRun') rm "$dir/AppRun" ;;
        'missing program') rm "$dir/usr/bin/odin_sphere_leifthrasir" ;;
        'missing Linux licences') rm "$dir/usr/share/licenses/$doc/THIRD-PARTY-LICENSES-LINUX.txt" ;;
        'missing header art') rm "$dir/usr/bin/assets/launcher/header-art.png" ;;
        'missing sponsor heart') rm "$dir/usr/bin/assets/launcher/sponsor-heart.png" ;;
        'program not executable') chmod 644 "$dir/usr/bin/odin_sphere_leifthrasir" ;;
        *) echo "unknown case $1" >&2; exit 2 ;;
    esac
}
cases=(settings 'save data' trophies 'trophy key' 'shader cache' log 'hang report' 'game files' 'game package' 'package by content'
       'SELF by content' 'debug file' pdb 'object file' 'static library' 'source file' 'python cache' 'build folder'
       'nested AppImage' 'unexpected file' 'symbolic link' 'updater leftover' 'crash dump' 'UTF-16 repository path'
       'Windows user folder' 'Windows program' renderdoc 'vulkan layer' 'vulkan loader' glibc 'newer glibc'
       'debug sections' 'absolute RPATH' 'RUNPATH instead of RPATH' 'unresolved library' 'DEBUG_SAULO marker'
       'repository path' 'home path' 'obsolete program name' 'missing AppRun' 'missing program'
       'missing Linux licences' 'missing header art' 'missing sponsor heart' 'program not executable')

failures=0 ran=0
if ! bash "$checker" --verify "$clean" >"$work/clean.log" 2>&1; then
    echo "FAIL clean AppDir refused:" >&2
    cat "$work/clean.log" >&2
    failures=$((failures + 1))
fi
ran=$((ran + 1))
for name in "${cases[@]}"; do
    dir=$work/case
    rm -rf "$dir"
    cp -a "$clean" "$dir"
    contaminate "$name" "$dir"
    if [[ $name == 'program not executable' && -x $dir/usr/bin/odin_sphere_leifthrasir ]]; then
        echo "skip $name (this file system ignores file modes)"
        continue
    fi
    if [[ $name == 'newer glibc' && -z $modern_glibc ]]; then
        echo "skip $name (programs built here need no glibc newer than 2.31)"
        continue
    fi
    ran=$((ran + 1))
    if bash "$checker" --verify "$dir" >"$work/case.log" 2>&1; then
        echo "FAIL $name (accepted)" >&2
        failures=$((failures + 1))
    else
        printf 'ok   %-26s %s\n' "$name" "$(grep -E '^  ' "$work/case.log" | grep -vE '^  [^ ]+ +glibc [^ ]+$' | head -1 | sed 's/^ *//')"
    fi
done
echo "$ran AppDir check cases, $failures failures (fixture glibc: ${modern_glibc:-none})"
((failures == 0))
