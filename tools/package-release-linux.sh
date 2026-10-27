#!/usr/bin/env bash
# Builds the Linux AppImage for a checkout that lives on a Windows drive and copies the result into its dist/ folder.
# Run it through WSL2 in the Ubuntu 20.04 distribution of docs/LINUX.md. Local only: nothing is published or uploaded.
#
#   tools/package-release-linux.sh [--clean] [--skip-build]
#
# A Windows drive is slow to build on and has no Unix file modes, so the sources are mirrored (only what changed) into
# ${OSL_WSL_BUILD_DIR:-~/.cache/odin-sphere-leifthrasir-pc-build}, where tools/package-linux.sh builds and packages them.
# The pinned dependencies (gpu/deps.txt) are not mirrored: tools/build.sh fetches Linux checkouts of them there.
# Needs the build environment of packaging/linux/ci-deps.sh and rsync; nothing is installed here.
set -euo pipefail
source=$(cd -- "$(dirname -- "$0")/.." && pwd)
clean=0 skip_build=0
while (($#)); do
    case $1 in
        --clean) clean=1 ;;
        --skip-build) skip_build=1 ;;
        -h|--help) sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

step() { printf '\n==> %s\n' "$*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || die 'This needs Linux x86_64 (WSL2 on a 64-bit PC).'
command -v rsync >/dev/null || die 'rsync is missing. Install it (sudo apt-get install rsync) or run packaging/linux/ci-deps.sh.'

mirror=${OSL_WSL_BUILD_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/odin-sphere-leifthrasir-pc-build}
mkdir -p "$mirror"
mirror=$(cd -- "$mirror" && pwd)
[[ $mirror != "$source" && $mirror != "$source"/* ]] || die "The build folder $mirror is inside the checkout."

excludes=(--exclude /.git --exclude /game --exclude /user --exclude /cache --exclude /logs --exclude /build
          --exclude /dist --exclude /out --exclude /.venv --exclude __pycache__)
while read -r path _; do
    [[ -z ${path:-} || $path == \#* ]] && continue
    excludes+=(--exclude "/$(realpath -m --relative-to="$source" "$source/gpu/$path")")
done < "$source/gpu/deps.txt"

step "Mirroring the sources into $mirror"
# Content, not timestamps, decides what is copied: a restored older file must still rebuild.
rsync -rl --checksum --no-times --delete --chmod=D755,F644 "${excludes[@]}" "$source/" "$mirror/"
chmod +x "$mirror"/tools/*.sh "$mirror"/packaging/linux/*.sh "$mirror"/packaging/linux/AppRun

cd "$mirror"
rm -rf dist
args=()
((clean)) && args+=(--clean)
((skip_build)) && args+=(--skip-build)
bash tools/package-linux.sh "${args[@]}"

step "Copying the AppImage into $source/dist"
mkdir -p "$source/dist"
shopt -s nullglob
images=(dist/OdinSphereLeifthrasirPC-v*-x86_64.AppImage)
((${#images[@]} == 1)) || die "Expected one AppImage in $mirror/dist, found ${#images[@]}"
name=$(basename -- "${images[0]}" .AppImage)
rm -rf "$source/dist/$name.AppImage" "$source/dist/$name.sha256" "$source/dist/$name-symbols"
cp -- "dist/$name.AppImage" "dist/$name.sha256" "$source/dist/"
cp -r -- "dist/$name-symbols" "$source/dist/"
(cd "$source/dist" && sha256sum -c "$name.sha256")
printf '\nLinux AppImage: %s\n' "$source/dist/$name.AppImage"
