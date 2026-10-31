#!/usr/bin/env bash
# Builds New Super Mario Bros. Wii for this PC (Linux x86_64) from your own disc, in one go: checks the
# disc and what the build needs, fetches the pinned tools, translates the game's code and compiles it
# (build-linux.sh), and puts the game in a folder ready to play. The release's Linux build kit runs this
# from build-nsmbw.sh. Nothing of the game is downloaded; its code comes from your disc.
#
#   build-game.sh [--game DIR] [--output DIR] [--jobs N] [--compat]
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: build-nsmbw.sh [--game DIR] [--output DIR] [--jobs N] [--compat]

  --game DIR     New Super Mario Bros. Wii (USA, SMNE01, revision 1) extracted with Dolphin's
                 Extract Entire Disc (DIR holds files/ and sys/). Asked for when not given.
  --output DIR   where the game goes (default: NSMBW beside the source folder)
  --jobs N       parallel jobs (default: every CPU thread; translated shards are capped by memory)
  --compat       build for older x86-64 CPUs (no AVX2) even on a newer one; picked by itself on an older one
EOF
}

repo=$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../../.." && pwd)
parent=$(dirname "$repo")
game_work=$parent/game  # recomp.yml reads main.dol and the linked REL from ../game
tools=$repo/Launcher/artifacts/portable-tools
game=""
output=""
jobs=$(nproc)
compat=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --game) game=$2; shift 2 ;;
        --output) output=$2; shift 2 ;;
        --jobs) jobs=$2; shift 2 ;;
        --compat) compat=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage; echo "unknown argument: $1" >&2; exit 1 ;;
    esac
done
output=${output:-$parent/NSMBW}

fail() {
    printf '\nThe build stopped: %s\n' "$*" >&2
    exit 1
}

step() {
    printf '\n== %s\n' "$*"
}

echo "New Super Mario Bros. Wii: building the game for this PC from your own disc."
echo "Source: $repo"
[[ "$(uname -m)" == x86_64 ]] || fail "this builds the x86_64 version; this machine is $(uname -m)"

# ------------------------------------------------------------------ 1. the disc
step "1/6  Your game"
if [[ -z "$game" ]]; then
    echo "Type the folder Dolphin made with Extract Entire Disc from your New Super Mario Bros. Wii"
    echo "(USA, SMNE01, revision 1). It holds the \"files\" and \"sys\" folders."
    read -r -p "Extracted game folder: " game || true
fi
game=${game/#\~/$HOME}
[[ -n "$game" && -d "$game" ]] || fail "no such folder: '$game'"
game=$(cd "$game" && pwd)
echo "Game: $game"
command -v python3 >/dev/null || fail "Python 3 is needed: install python3 with your package manager"
python3 "$repo/projects/nsmbw/tools/unpack_game.py" "$game" "$game_work" ||
    fail "that game folder can't be used (see above). This needs New Super Mario Bros. Wii for the USA (SMNE01), revision 1, extracted with Dolphin's Extract Entire Disc; Dolphin shows the revision in the game's Properties, Info tab."
python3 "$repo/projects/nsmbw/tools/prelink_rels.py" "$game_work" || fail "linking the game's modules failed (see above)."

# ------------------------------------------------------------------ 2. what the build needs
step "2/6  What the build needs from this PC"
missing=()
for tool in curl tar xz unzip strip sha256sum awk pkg-config; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
# The game's window and sound: X11 and ALSA headers for SDL, Vulkan's for the renderer (BUILDING.md).
for package in x11 alsa vulkan; do
    command -v pkg-config >/dev/null && pkg-config --exists "$package" 2>/dev/null || missing+=("$package (development files)")
done
if (( ${#missing[@]} > 0 )); then
    printf 'Missing: %s\n\n' "${missing[*]}" >&2
    cat >&2 <<'EOF'
Install them with your package manager, for example:
  Debian, Ubuntu, Mint:  sudo apt install curl xz-utils unzip binutils pkg-config python3 libx11-dev libasound2-dev libvulkan-dev
  Fedora:                sudo dnf install curl xz unzip binutils pkgconf-pkg-config python3 libX11-devel alsa-lib-devel vulkan-loader-devel
  Arch, SteamOS:         sudo pacman -S --needed curl xz unzip binutils pkgconf python libx11 alsa-lib vulkan-icd-loader vulkan-headers
For native Wayland, PipeWire and PulseAudio also install their development files (BUILDING.md section 6).
EOF
    fail "this PC is missing what the build needs (above)."
fi
echo "All there."

# ------------------------------------------------------------------ 3. tools
step "3/6  Compiler tools (clang, CMake, Ninja, .NET 8; downloaded once)"
"$repo/Launcher/prepare-portable-tools.sh" --arch x86_64
# The translator runs on .NET 8: the SDK on PATH (or in ~/.dotnet) if it is 8, else a private one.
has_dotnet8() {
    local dotnet
    dotnet=$(command -v dotnet || true)
    [[ -n "$dotnet" ]] || dotnet=$HOME/.dotnet/dotnet
    [[ -x "$dotnet" ]] && "$dotnet" --list-sdks 2>/dev/null | grep -q '^8\.'
}
if [[ -x "$tools/dotnet/dotnet" ]]; then
    export PATH="$tools/dotnet:$PATH"
elif ! has_dotnet8; then
    echo "Installing the .NET 8 SDK for the translator into $tools/dotnet (only used by this build)..."
    mkdir -p "$repo/Launcher/artifacts/downloads"
    installer=$repo/Launcher/artifacts/downloads/dotnet-install.sh
    curl -fsSL -o "$installer" https://dot.net/v1/dotnet-install.sh
    bash "$installer" --channel 8.0 --install-dir "$tools/dotnet" --no-path
    [[ -x "$tools/dotnet/dotnet" ]] || fail "the .NET 8 SDK could not be installed."
    export PATH="$tools/dotnet:$PATH"
fi

# This CPU's x86-64 level: v3 needs AVX2, BMI2, FMA and F16C (Haswell, Excavator, Zen and newer).
x86_level=v3
if (( compat )); then
    x86_level=v2
else
    flags=$(grep -m1 '^flags' /proc/cpuinfo || true)
    for feature in avx2 bmi2 fma f16c; do
        [[ " $flags " == *" $feature "* ]] || x86_level=v2
    done
fi
if [[ "$x86_level" == v3 ]]; then echo "CPU: x86-64-v3 (AVX2)"; else echo "CPU: building the compat version for processors without AVX2"; fi

# ------------------------------------------------------------------ 4. translate and compile
step "4/6  Translating and compiling the game ($jobs jobs; the first build takes a long time)"
"$repo/projects/nsmbw/tools/build-linux.sh" --game "$game" --output "$output" --jobs "$jobs" --x86-level "$x86_level"

# ------------------------------------------------------------------ 5. the game folder
step "5/6  The game folder"
mv -f "$output/WiiCompiled" "$output/NSMBW"
echo "$output"

# ------------------------------------------------------------------ 6. done
step "6/6  Done"
echo "The game is $output/NSMBW."
echo "Keep your extracted game folder: the game reads its levels, music and graphics from it."
echo "Run this again after updating the build kit; it reuses the tools and only rebuilds what changed."
