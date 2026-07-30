#!/usr/bin/env bash
# release-windows-vm-cmake.sh — build a CMake/C++ project's Windows binaries by
# driving the Parallels "Windows 11" guest.
#
# JUCE and FFGL projects link against the MSVC runtime and the Windows SDK, so
# there is no cross-compilation story from macOS the way there is for Rust.
# This mirrors the tree into the guest, configures and builds with the Visual
# Studio generator for both architectures, and copies the artefacts back.
#
# Native tools log progress to stderr, which PowerShell turns into a terminating
# NativeCommandError under $ErrorActionPreference='Stop', so the generated
# scripts use 'Continue' and check $LASTEXITCODE explicitly.
#
# Requires in the guest: VS Build Tools 2022 with the C++ x64 and ARM64 toolsets
# (CMake ships inside VS, so no separate install).
#
#   release-windows-vm-cmake.sh <repo> <slug> <artefact-glob> [cmake-extra-args]
#
# <artefact-glob> is what to fetch back, e.g. '*.exe' or '*.vst3' or '*.dll'.
# Results land in <repo>/dist-release/win-<arch>/ for the caller to package.
set -euo pipefail

repo="$1"; slug="$2"; glob="$3"; extra="${4:-}"
vm="${RL_VM_NAME:-Windows 11}"

reponame="$(basename "$repo")"
guest="C:\\build\\${slug}"
staging="$HOME/Projects/.release-vm"
mkdir -p "$staging"

psfile() {
  prlctl exec "$vm" powershell -NoProfile -ExecutionPolicy Bypass \
    -File "\\\\psf\\Projects\\.release-vm\\$1"
}

cat >"$staging/mirror.ps1" <<PS1
\$ErrorActionPreference = 'Continue'
robocopy '\\\\psf\\Projects\\${reponame}' '${guest}' /E /NJH /NJS /NP /NFL /NDL /R:1 /W:1 \`
  /XD build build-release target node_modules .git dist-release .release-vm | Out-Null
if (\$LASTEXITCODE -ge 8) { Write-Error "robocopy failed: \$LASTEXITCODE"; exit 1 }
Write-Output 'mirrored'
exit 0
PS1

echo "    mirroring ${reponame} into the guest"
psfile mirror.ps1 >/dev/null || { echo "    mirror FAILED"; exit 1; }

for pair in "x86_64:x64" "aarch64:ARM64"; do
  label="${pair%%:*}"; arch="${pair##*:}"
  echo "    guest cmake build windows-${label} (${arch})"

  cat >"$staging/cmake.ps1" <<PS1
\$ErrorActionPreference = 'Continue'
\$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path \$cmake)) { \$cmake = 'cmake' }

Set-Location '${guest}'
Write-Output '--- configure'
& \$cmake -B build-${arch} -A ${arch} -DCMAKE_BUILD_TYPE=Release ${extra} 2>&1 | Select-Object -Last 15
if (\$LASTEXITCODE -ne 0) { Write-Output "CONFIGURE FAILED"; exit 1 }

Write-Output '--- build'
& \$cmake --build build-${arch} --config Release --parallel 2>&1 | Select-Object -Last 20
if (\$LASTEXITCODE -ne 0) { Write-Output "BUILD FAILED"; exit 1 }
Write-Output 'BUILD OK'
exit 0
PS1

  if ! psfile cmake.ps1 2>&1 | tail -25; then
    echo "    guest build windows-${label} FAILED"
    continue
  fi

  cat >"$staging/fetch.ps1" <<PS1
\$ErrorActionPreference = 'Continue'
\$dst = '\\\\psf\\Projects\\${reponame}\\dist-release\\win-${label}'
if (Test-Path \$dst) { Remove-Item \$dst -Recurse -Force }
New-Item -ItemType Directory -Force -Path \$dst | Out-Null
\$found = 0
Get-ChildItem -Path '${guest}\\build-${arch}' -Recurse -Include ${glob} -EA 0 |
  Where-Object { \$_.FullName -notmatch '\\\\_deps\\\\' -and \$_.FullName -notmatch 'CMakeFiles' } |
  ForEach-Object {
    if (\$_.PSIsContainer) { Copy-Item \$_.FullName \$dst -Recurse -Force }
    else { Copy-Item \$_.FullName \$dst -Force }
    Write-Output \$_.Name
    \$found++
  }
if (\$found -eq 0) { Write-Output 'no artefacts matched ${glob}' }
exit 0
PS1

  echo "    retrieving windows-${label}"
  psfile fetch.ps1 | sed 's/^/      /'
done

rm -rf "$staging"
