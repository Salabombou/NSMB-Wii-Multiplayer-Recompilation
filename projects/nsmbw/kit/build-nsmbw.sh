#!/usr/bin/env bash
# New Super Mario Bros. Wii build kit: builds the game for this PC from your own disc (see README.txt).
#   ./build-nsmbw.sh --game <extracted disc folder> [--output DIR] [--jobs N] [--compat]
kit=$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)
exec bash "$kit/source/projects/nsmbw/tools/build-game.sh" "$@"
