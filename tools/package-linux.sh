#!/usr/bin/env bash
# Packages the tested Linux build as dist/OdinSphereLeifthrasir-v<version>-x86_64.AppImage and its .AppImage.sha256.
# Local only: nothing is published, tagged or uploaded. The Linux counterpart of tools/package.ps1.
#
#   tools/package-linux.sh                build with tools/build.sh, then package
#   tools/package-linux.sh --skip-build   package the existing build/cmake/linux/bin/odin_sphere_leifthrasir
#   tools/package-linux.sh --out-dir DIR  write the AppImage, its .sha256 and the symbols to DIR instead of dist/
#   tools/package-linux.sh --verify DIR   only check an existing AppDir
#
# Needs patchelf, binutils, curl, git and dpkg (licence texts come from the packages of the bundled libraries). Build
# on Ubuntu 20.04 (packaging/linux/ci-deps.sh): no shipped ELF may need a newer glibc than 2.31. GPU drivers, the
# Vulkan loader, X11/Wayland, audio and glibc always come from the player's system. The patched AppImage runtime is
# built by tools/build-appimage-runtime.sh, which needs root or passwordless sudo (docs/LINUX.md).
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
skip_build=0 verify_dir= out_dir=
while (($#)); do
    case $1 in
        --skip-build) skip_build=1 ;;
        --out-dir) out_dir=${2:?--out-dir needs a folder}; shift ;;
        --verify) verify_dir=$(cd -- "${2:?--verify needs a folder}" && pwd) || exit 2; shift ;;
        -h|--help) sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
if [[ -n $out_dir ]]; then
    mkdir -p -- "$out_dir" && out_dir=$(cd -- "$out_dir" && pwd) || exit 2
fi
cd "$root"

step() { printf '\n==> %s\n' "$*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

max_glibc=2.31
program=odin_sphere_leifthrasir
app=odin-sphere-leifthrasir-pc
doc_name=OdinSphereLeifthrasirPC
appimagetool_version=1.9.1
appimagetool_sha256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0

# Provided by the player's system: glibc, the Vulkan loader and drivers, the display servers and the audio daemons.
excluded='^(ld-linux.*|libc|libm|libdl|librt|libpthread|libresolv|libutil|libnsl|libanl|libcrypt|libmvec|libvulkan|libGL|libGLX|libGLdispatch|libEGL|libOpenGL|libgbm|libdrm.*|libX11|libX11-xcb|libxcb.*|libXau|libXdmcp|libXext|libXfixes|libXrender|libXi|libXcursor|libXrandr|libxkbcommon.*|libwayland-.*|libasound|libudev|libpulse.*|libpipewire.*|libdbus-1|libfontconfig|libOpenCL|libva.*|libvdpau|libnvidia-.*|libcuda|libz|libselinux|libpcre2-.*|libsystemd|libcap)\.so'
# Fallback copies, used only when the system's are older than the build's (packaging/linux/AppRun).
compat_libs='^(libstdc\+\+|libgcc_s)\.so'

forbidden_names='^(game|user|logs|cache|build|prepared|extracted|image|savedata|trophy|download0|temp0|shader|screenshots|sce_sys|sce_module|eboot\.bin|param\.sfo|osl\.ini|keys\.json|boot\.bin|content\.bin|image\.json|imports\.json|symbols|__pycache__|CMakeFiles)$'
forbidden_patterns='\.(pkg|pdb|debug|log|dmp|ini|bin|self|sprx|prx|elf|cpk|sfo|json|trp|spv|pipeline_cache|o|obj|a|lib|tmp|bak|part|sav|py|pyc|ps1|c|cpp|h|hpp|exe|dll|AppImage)$'

newer_than() { [[ $1 != "$2" && $(printf '%s\n' "$1" "$2" | sort -V | tail -1) == "$1" ]]; }
magic() { head -c 4 -- "$1" | od -An -tx1 | tr -d ' \n'; }
# The GLIBC_ versions in the ELF's version-needs section: the dynamic loader refuses to start it without every one.
newest_glibc() {
    local needs
    needs=$(readelf --version-info -W -- "$1") || return 1
    awk '/^Version needs section/ {needs = 1; next} /^Version / {needs = 0} needs' <<<"$needs" |
        { grep -oE 'Name: GLIBC_[0-9]+(\.[0-9]+)*' || true; } | sed 's/^Name: GLIBC_//' | sort -V | tail -1
}

check_elf() { # label base file
    local label=$1 base=$2 file=$3 rel newest header sections dynamic entry origin='^\$ORIGIN(/.*)?$'
    rel=${file#"$base"/}
    header=$(readelf -h -- "$file" 2>/dev/null) || { problem "unreadable ELF: $rel"; return; }
    grep -q 'Class:.*ELF64' <<<"$header" && grep -q 'Machine:.*X86-64' <<<"$header" || problem "not an x86-64 ELF: $rel"
    newest=$(newest_glibc "$file") || { problem "unreadable ELF: $rel"; return; }
    printf '  %-48s glibc %s\n' "$rel" "${newest:-(none)}"
    if [[ -n $newest ]] && newer_than "$newest" "$max_glibc"; then
        problem "$label $rel needs glibc $newest, newer than the $max_glibc baseline (build on Ubuntu 20.04)"
    fi
    sections=$(readelf -S -W -- "$file" 2>/dev/null || true)
    grep -qE '\.(z?debug_info|debug_line|gnu_debugdata)\b' <<<"$sections" && problem "debug information in $rel"
    dynamic=$(readelf -d -W -- "$file" 2>/dev/null || true)
    grep -qiE 'NEEDED.*(renderdoc|VkLayer)' <<<"$dynamic" && problem "$rel links RenderDoc or a Vulkan layer"
    # Only $ORIGIN-relative search paths: an absolute one would load libraries from the build machine's folders.
    while read -r entry; do
        [[ -z $entry || $entry =~ $origin ]] || problem "absolute library search path '$entry' in $rel"
    done < <(grep -E '\((RPATH|RUNPATH)\)' <<<"$dynamic" | sed -E 's/.*\[(.*)\].*/\1/' | tr ':' '\n')
}

check_appdir() { # dir [the AppImage file, whose runtime is an ELF too]
    local dir=$1 problems=0 item rel name needle needles elves=() leaf missing dynamic
    problem() { echo "  $*" >&2; problems=$((problems + 1)); }
    needles=("$root" "${HOME:-/nonexistent-home}" VisualStudio 'S:\Decomp' 'S:/Decomp' DEBUG_SAULO referencias-ps4
             osl-loader osl.loader dragons_crown DragonsCrown)
    while IFS= read -r -d '' item; do
        rel=${item#"$dir"/}
        name=$(basename -- "$item")
        [[ $name =~ $forbidden_names ]] && problem "forbidden name: $rel"
        [[ $name =~ $excluded ]] && problem "the player's system provides this library: $rel"
        [[ ${name,,} == *renderdoc* || ${name,,} == *vklayer* || $name == *_layer.d ]] && problem "RenderDoc or Vulkan layer file: $rel"
        if [[ $rel != usr/share/licenses/* && $rel != usr/share/doc/* && -f $item && $name =~ $forbidden_patterns ]]; then
            problem "forbidden file type: $rel"
        fi
        [[ -f $item && ! -L $item ]] || continue
        case $(magic "$item") in
            7f434e54) problem "PS4 package: $rel" ;;
            4f153d1d) problem "PS4 SELF: $rel" ;;
            43504b20) problem "CPK archive: $rel" ;;
            4d5a*) problem "Windows program: $rel" ;;
            7f454c46) elves+=("$item") ;;
        esac
        for needle in "${needles[@]}"; do
            grep -a -q -i -F -- "$needle" "$item" && { problem "local path, development marker or stale name '$needle' in $rel"; break; }
        done
    done < <(find "$dir" -mindepth 1 -print0)
    for leaf in AppRun "$app.desktop" "$app.png" .DirIcon "usr/bin/$program" usr/bin/assets/launcher/header-art.png \
                "usr/share/licenses/$doc_name/LICENSE.txt" "usr/share/licenses/$doc_name/THIRD-PARTY-LICENSES.txt" \
                "usr/share/licenses/$doc_name/THIRD-PARTY-LICENSES-LINUX.txt" \
                "usr/share/licenses/$doc_name/NotoSansTC/LICENSE-NotoSansTC.txt" "usr/share/doc/$doc_name/README.txt"; do
        [[ -e $dir/$leaf ]] || problem "missing: $leaf"
    done
    for leaf in AppRun "usr/bin/$program"; do
        [[ ! -e $dir/$leaf || -x $dir/$leaf ]] || problem "not executable: $leaf"
    done
    if [[ -f $dir/usr/bin/$program ]]; then
        dynamic=$(readelf -d -W -- "$dir/usr/bin/$program" 2>/dev/null || true)
        grep -qF '(RPATH)' <<<"$dynamic" && grep -qF 'Library rpath: [$ORIGIN/../lib]' <<<"$dynamic" ||
            problem "usr/bin/$program: the RPATH is not \$ORIGIN/../lib"
        missing=$(env -u LD_LIBRARY_PATH ldd "$dir/usr/bin/$program" 2>&1 | grep 'not found' || true)
        [[ -z $missing ]] || problem "unresolved libraries: $missing"
    fi
    echo "glibc needed by each ELF (baseline $max_glibc):"
    for item in "${elves[@]}"; do check_elf 'the AppDir:' "$dir" "$item"; done
    (($# < 2)) || check_elf 'the AppImage runtime:' "$(dirname -- "$2")" "$2"
    ((problems == 0)) || die "release check failed ($problems problem(s))"
    echo 'AppDir is clean'
}

if [[ -n $verify_dir ]]; then
    check_appdir "$verify_dir"
    exit 0
fi

[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || die 'Packaging needs Linux x86_64 (Windows: tools\package.ps1).'
for tool in patchelf objcopy readelf strip curl dpkg dpkg-query ldd sha256sum; do
    command -v "$tool" >/dev/null || die "Missing $tool"
done
host_glibc=$(getconf GNU_LIBC_VERSION | cut -d' ' -f2)
newer_than "$host_glibc" "$max_glibc" &&
    die "This system has glibc $host_glibc. Release AppImages are built on Ubuntu 20.04 (glibc $max_glibc): see packaging/linux/ci-deps.sh."

version=$(sed -nE 's/^set\(OSL_PORT_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"\)$/\1/p' CMakeLists.txt)
[[ -n $version ]] || die 'OSL_PORT_VERSION not found in CMakeLists.txt'
name=OdinSphereLeifthrasir-v$version-x86_64
build_dir=$root/build/cmake/linux
loader=$build_dir/bin/$program
dist=${out_dir:-$root/dist}
work=$root/build/appimage
appdir=$work/AppDir
symbols=$dist/$name-symbols

step "Odin Sphere Leifthrasir PC $version - Linux AppImage (local only)"
((skip_build)) || "$root/tools/build.sh"
[[ -x $loader ]] || die "Missing $loader (run without --skip-build)"
# tools/build.sh's one configuration (-O2 -g -DNDEBUG); the debug information is split off below.
grep -qs 'CMAKE_BUILD_TYPE:STRING=RelWithDebInfo' "$build_dir/CMakeCache.txt" ||
    die "$build_dir is not the RelWithDebInfo build of tools/build.sh (run without --skip-build)"

rm -rf "$appdir"
mkdir -p "$dist" "$symbols" "$appdir/usr/bin" "$appdir/usr/lib/compat" "$appdir/usr/share/licenses/$doc_name" \
         "$appdir/usr/share/doc/$doc_name" "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/256x256/apps"
rm -f "$dist/$name.AppImage" "$dist/$name.AppImage.sha256"

step "Staging $program without debug information"
objcopy --only-keep-debug "$loader" "$symbols/$program.debug"
objcopy --strip-debug --add-gnu-debuglink="$symbols/$program.debug" "$loader" "$appdir/usr/bin/$program"
chmod 755 "$appdir/usr/bin/$program"

step 'Staging the launcher header art'
install -d "$appdir/usr/bin/assets/launcher"
install -m644 assets/launcher/header-art.png "$appdir/usr/bin/assets/launcher/header-art.png"

step 'Staging the libraries it loads'
declare -A seen=()
declare -A owners=()
bundle() {
    local elf=$1 line lib file
    while read -r line; do
        lib=$(awk '/=> \//{print $3}' <<<"$line")
        [[ -n $lib ]] || continue
        file=$(basename -- "$lib")
        [[ -n ${seen[$file]:-} ]] && continue
        seen[$file]=1
        [[ $file =~ $excluded ]] && continue
        if [[ $file =~ $compat_libs ]]; then
            install -m755 "$(readlink -f -- "$lib")" "$appdir/usr/lib/compat/$file"
        else
            install -m755 "$(readlink -f -- "$lib")" "$appdir/usr/lib/$file"
            bundle "$lib"
        fi
        owners[$file]=$lib
    done < <(env -u LD_LIBRARY_PATH ldd "$elf")
}
bundle "$loader"
for lib in "$appdir"/usr/lib/*.so*; do
    strip --strip-unneeded "$lib"
    patchelf --set-rpath '$ORIGIN' "$lib"
done
strip --strip-unneeded "$appdir"/usr/lib/compat/*.so* 2>/dev/null || true
# DT_RPATH (not RUNPATH) is inherited by the libraries' own dependencies and wins over LD_LIBRARY_PATH.
patchelf --force-rpath --set-rpath '$ORIGIN/../lib' "$appdir/usr/bin/$program"
echo "Bundled: $(cd "$appdir/usr/lib" && ls -- *.so* | tr '\n' ' ')"
echo "Fallback: $(cd "$appdir/usr/lib/compat" && ls -- *.so* 2>/dev/null | tr '\n' ' ')"

step 'Staging AppRun, desktop entry, icon, licences and README'
glibcxx_need=$(objdump -T "$appdir/usr/bin/$program" "$appdir"/usr/lib/*.so* | grep -o 'GLIBCXX_3\.4\.[0-9]*' |
               sed 's/.*\.//' | sort -n | tail -1)
gcc_need=$(objdump -T "$appdir/usr/bin/$program" "$appdir"/usr/lib/*.so* | grep -o 'GCC_[0-9][0-9.]*' |
           sed 's/^GCC_//; s/\..*//' | sort -n | tail -1)
sed "s/@GLIBCXX_NEED@/${glibcxx_need:-0}/g; s/@GCC_NEED@/${gcc_need:-0}/g" packaging/linux/AppRun > "$appdir/AppRun"
chmod 755 "$appdir/AppRun"
install -m644 "packaging/linux/$app.desktop" "$appdir/$app.desktop"
install -m644 "packaging/linux/$app.desktop" "$appdir/usr/share/applications/$app.desktop"
install -m644 packaging/icons/app-icon-256.png "$appdir/$app.png"
install -m644 packaging/icons/app-icon-256.png "$appdir/usr/share/icons/hicolor/256x256/apps/$app.png"
ln -s "$app.png" "$appdir/.DirIcon"
if command -v desktop-file-validate >/dev/null; then
    desktop-file-validate "$appdir/$app.desktop" || die "$app.desktop is not a valid desktop entry"
fi
sed "s/@VERSION@/$version/g" packaging/README-linux.txt > "$appdir/usr/share/doc/$doc_name/README.txt"

licenses=$appdir/usr/share/licenses/$doc_name
install -m644 LICENSE "$licenses/LICENSE.txt"
install -m644 packaging/licenses/THIRD-PARTY-LICENSES.txt packaging/licenses/THIRD-PARTY-LICENSES-LINUX.txt "$licenses/"
declare -A built_in=(
    [shadPS4]=gpu/shadps4/LICENSE [sirit]=gpu/third_party/sirit/LICENSE.txt
    [SPIRV-Headers]=gpu/third_party/sirit/externals/SPIRV-Headers/LICENSE [half]=gpu/third_party/half/LICENSE.txt
    [SMAA]=gpu/shadps4/video_core/host_shaders/smaa/LICENSE.txt [DejaVu]=gpu/third_party/fonts/LICENSE-DejaVu.txt
    [NotoSans]=third_party/fonts/LICENSE-NotoSans.txt [NotoSansTC]=third_party/fonts/LICENSE-NotoSansTC.txt
    [imgui]=gpu/third_party/imgui/LICENSE.txt [magic_enum]=gpu/third_party/magic_enum/LICENSE
    [miniz]=gpu/third_party/miniz/LICENSE [xbyak]=gpu/third_party/xbyak/COPYRIGHT)
for component in "${!built_in[@]}"; do
    [[ -f ${built_in[$component]} ]] || die "Missing licence file: ${built_in[$component]}"
    mkdir -p "$licenses/$component"
    install -m644 "${built_in[$component]}" "$licenses/$component/"
done
# The licence texts of the Ubuntu packages the bundled libraries come from, or of the sources
# packaging/linux/ci-deps.sh built them from (it installs those texts in /usr/local/share/licenses/<component>).
declare -A source_built=([libSDL3]=SDL3 [libfmt]=fmt [libZydis]=zydis [libZycore]=zydis [libxxhash]=xxhash
                         [libcrypto]=openssl)
manifest=$licenses/SYSTEM-PACKAGES.txt
{
    echo "Libraries bundled in the AppImage, with the Ubuntu packages or sources they come from and their licence texts."
    echo "Libraries and drivers the player's system provides (glibc, Vulkan, X11/Wayland, audio) are not bundled."
} > "$manifest"
for lib in $(cd "$appdir/usr/lib" && ls -- *.so* compat/*.so* 2>/dev/null | sed 's|.*/||' | sort -u); do
    path=${owners[$lib]:-}
    [[ -n $path ]] || continue
    # dpkg knows a file by one name only, which on a merged-/usr system may be either of these.
    package=$(dpkg-query -S "$(readlink -f -- "$path")" "$path" 2>/dev/null | head -1 | cut -d: -f1 || true)
    if [[ -n $package ]]; then
        version_of=$(dpkg-query -W -f='${Version}' "$package")
        mkdir -p "$licenses/system/$package"
        install -m644 "/usr/share/doc/$package/copyright" "$licenses/system/$package/copyright"
        printf '\n%s (%s %s)\n  Licence text: licenses/system/%s/copyright\n' "$lib" "$package" "$version_of" "$package" >> "$manifest"
    elif component=${source_built[${lib%%.so*}]:-} && [[ -n $component && $(readlink -f -- "$path") == /usr/local/lib/* ]]; then
        texts=(/usr/local/share/licenses/"$component"/*)
        [[ -f ${texts[0]} ]] || die "No licence text in /usr/local/share/licenses/$component (run packaging/linux/ci-deps.sh)"
        mkdir -p "$licenses/system/$component"
        install -m644 "${texts[@]}" "$licenses/system/$component/"
        printf '\n%s (%s, built from source by packaging/linux/ci-deps.sh)\n  Licence text: licenses/system/%s/\n' \
            "$lib" "$component" "$component" >> "$manifest"
    else
        die "No package owns $path: add its licence handling to tools/package-linux.sh"
    fi
done

step 'Checking the AppDir'
check_appdir "$appdir"

step 'Writing the AppImage'
tool=$work/appimagetool-$appimagetool_version-x86_64.AppImage
if [[ ! -x $tool ]]; then
    curl -fL --retry 3 -o "$tool.part" \
        "https://github.com/AppImage/appimagetool/releases/download/$appimagetool_version/appimagetool-x86_64.AppImage"
    echo "$appimagetool_sha256  $tool.part" | sha256sum -c - || die 'appimagetool does not match the pinned checksum'
    chmod +x "$tool.part"
    mv "$tool.part" "$tool"
fi
echo "$appimagetool_sha256  $tool" | sha256sum -c --status - || die "$tool does not match the pinned checksum"
# appimagetool would otherwise download the unpinned "continuous" runtime.
runtime=$work/runtime-osl-x86_64
bash tools/build-appimage-runtime.sh "$runtime" "$work/appimage-runtime"
ARCH=x86_64 "$tool" --appimage-extract-and-run --no-appstream --runtime-file "$runtime" "$appdir" "$dist/$name.AppImage.new" \
    >"$work/appimagetool.log" 2>&1 || { cat "$work/appimagetool.log" >&2; die 'appimagetool failed'; }
chmod 755 "$dist/$name.AppImage.new"
bash tools/build-appimage-runtime.sh --check "$dist/$name.AppImage.new"

step 'Checking the contents of the written AppImage'
rm -rf "$work/written"
mkdir -p "$work/written"
(cd "$work/written" && "$dist/$name.AppImage.new" --appimage-extract >/dev/null)
check_appdir "$work/written/squashfs-root" "$dist/$name.AppImage.new"
rm -rf "$work/written"
mv -f "$dist/$name.AppImage.new" "$dist/$name.AppImage"
(cd "$dist" && sha256sum "$name.AppImage" > "$name.AppImage.sha256")

printf '\nAPPIMAGE READY (nothing was published)\n'
printf '  AppImage: %s (%s)\n' "$dist/$name.AppImage" "$(du -h "$dist/$name.AppImage" | cut -f1)"
printf '  SHA-256:  %s\n' "$(cut -d' ' -f1 "$dist/$name.AppImage.sha256")"
printf '  Symbols:  %s (not in the package)\n' "$symbols/$program.debug"
