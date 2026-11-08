#!/bin/zsh
# usage: [BOTW_GAME=<game folder>] tools/play.sh [runtime flags...]   windowed, with sound and controller (F11: fullscreen)
# fresh settings default: 60 fps target, 1.5x (1080p); --fps 30 / --scale 1 for original resolution
cd "$(dirname "$0")/.."
exec ./build/BotWRecomp --game "${BOTW_GAME:-../game}" "$@"
