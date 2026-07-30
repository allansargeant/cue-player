#!/usr/bin/env bash
# release-local.sh — cut a full SimpleCue release from this Mac.
#
# GitHub Actions minutes are exhausted, so releases are built here.
#
#   macOS    universal (arm64 + x86_64) via CMAKE_OSX_ARCHITECTURES, which
#            CMakeLists.txt already sets *before* project() — a later set()
#            silently produces an arm64-only "universal" build.
#   Windows  x64 and ARM64, built in the Parallels guest: JUCE links the MSVC
#            runtime, so there is no cross-compilation route from macOS.
#   Linux    NOT BUILDABLE HERE. JUCE needs ALSA/X11/GTK dev headers and a
#            Linux toolchain; there is no Linux host and no container runtime.
#
# Artefacts: macOS .dmg + .pkg + portable .zip; Windows portable .zip + NSIS.
#
#   scripts/release-local.sh                  build into dist-release/
#   scripts/release-local.sh --version 0.4.0  set an explicit version
#   scripts/release-local.sh --no-vm          skip the Windows builds
#   scripts/release-local.sh --upload         tag and publish the GitHub release
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"
source "$repo/scripts/release-lib.sh"

out="$repo/dist-release"
upload=0; use_vm=1; version=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --upload)  upload=1 ;;
    --no-vm)   use_vm=0 ;;
    --version) version="$2"; shift ;;
    *) echo "unknown flag: $1" >&2; exit 2 ;;
  esac
  shift
done

current="$(sed -n 's/^project(SimpleCue VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[[ -z "$current" ]] && { echo "cannot read version from CMakeLists.txt" >&2; exit 1; }
[[ -z "$version" ]] && version="$(awk -F. '{printf "%d.%d.%d", $1, $2, $3+1}' <<<"$current")"
tag="v${version}"
echo "==> SimpleCue ${current} -> ${version}"
sed -i '' "s/^project(SimpleCue VERSION ${current}/project(SimpleCue VERSION ${version}/" CMakeLists.txt

rl_init "SimpleCue" simplecue "$version" com.stoatworks.simplecue "$out"
rm -rf "$out"; mkdir -p "$out"

# ------------------------------------------------------------------ macOS ---

echo "==> configuring macOS (universal)"
cmake -B build-release -DCMAKE_BUILD_TYPE=Release >/dev/null
echo "==> building macOS"
cmake --build build-release --config Release --parallel "$(sysctl -n hw.ncpu)" >/dev/null

app="build-release/SimpleCue_artefacts/Release/SimpleCue.app"
[[ -d "$app" ]] || { echo "SimpleCue.app not produced" >&2; exit 1; }

# Trust lipo, not the build log: a mis-ordered CMAKE_OSX_ARCHITECTURES yields a
# single-arch binary that still reports success.
archs="$(lipo -archs "$app/Contents/MacOS/SimpleCue")"
echo "    SimpleCue.app: ${archs}"
[[ "$archs" == *arm64* && "$archs" == *x86_64* ]] \
  || { echo "expected a universal binary, got: ${archs}" >&2; exit 1; }

stage="$out/.stage-mac"
rm -rf "$stage"; mkdir -p "$stage"
cp -R "$app" "$stage/"
cp README.md LICENSE "$stage/"
rl_adhoc_sign "$stage/SimpleCue.app"
rl_dmg macos-universal "$stage" --app "SimpleCue.app"
rl_pkg macos-universal "$stage" --app "SimpleCue.app"
# ditto, not zip: it preserves the bundle's symlinks and resource forks.
( cd "$stage" && ditto -c -k --sequesterRsrc --keepParent "SimpleCue.app" \
    "$out/simplecue-${version}-macos-universal.zip" )
rm -rf "$stage"

# ---------------------------------------------------------------- Windows ---

if (( use_vm )) && command -v prlctl >/dev/null 2>&1 \
   && prlctl list -a 2>/dev/null | grep -q "running.*Windows 11"; then
  echo "==> Windows (Parallels VM)"
  bash "$repo/scripts/release-windows-vm-cmake.sh" "$repo" simplecue "'*.exe'" \
    || rl_skip "Windows builds (VM build failed)"

  for label in x86_64 aarch64; do
    src="$out/win-${label}"
    if [[ -d "$src" ]] && compgen -G "$src/SimpleCue.exe" >/dev/null; then
      wstage="$out/.stage-win-${label}"
      rm -rf "$wstage"; mkdir -p "$wstage"
      cp "$src/SimpleCue.exe" "$wstage/"
      cp README.md LICENSE "$wstage/"
      rl_zip  "windows-${label}" "$wstage"
      rl_nsis "windows-${label}" "$wstage" --gui "SimpleCue.exe"
      rm -rf "$wstage" "$src"
    else
      rl_skip "Windows ${label} (SimpleCue.exe not produced)"
      rm -rf "$src"
    fi
  done
else
  rl_skip "Windows builds (VM not running or --no-vm)"
fi

rl_skip "Linux builds (JUCE needs a Linux host; no container runtime here)"

rl_summary

cat <<'NOTE'

    Unsigned. macOS users must run
      xattr -dr com.apple.quarantine "/Applications/SimpleCue.app"
    after installing.
NOTE

if (( upload )); then
  echo "==> tagging ${tag}"
  git add -A
  git commit -m "release: ${tag}" || true
  git tag -a "$tag" -m "SimpleCue ${version}" || true
  git push origin HEAD --tags
  gh release create "$tag" --title "SimpleCue ${version}" \
     --notes "Local build — GitHub Actions minutes are exhausted, so these artefacts were cut on a Mac. Unsigned: see the README for the macOS quarantine step." \
     "$out"/* \
    || gh release upload "$tag" "$out"/* --clobber
fi
