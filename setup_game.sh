#!/bin/bash
# Assemble a game folder from user-provided base/update dumps. Optional fourth argument is DLC.
set -euo pipefail

usage() {
  echo "Usage: $0 <base-game-folder> <update-v208-folder> <destination> [dlc-folder]" >&2
  echo "Each source folder must contain code/, content/ and meta/." >&2
  exit 2
}

[ "$#" -ge 3 ] && [ "$#" -le 4 ] || usage
SRC_GAME="$1"
SRC_UPDATE="$2"
DEST="$3"
SRC_DLC="${4:-}"

check_game_folder() {
  local path="$1" label="$2"
  for dir in code content meta; do
    if [ ! -d "$path/$dir" ]; then
      echo "$label is missing $dir/: $path" >&2
      exit 1
    fi
  done
}

check_game_folder "$SRC_GAME" "Base game"
check_game_folder "$SRC_UPDATE" "Update"
if [ -n "$SRC_DLC" ] && [ ! -d "$SRC_DLC/content" ]; then
  echo "DLC folder is missing content/: $SRC_DLC" >&2
  exit 1
fi

mkdir -p "$DEST"
echo "Copying base game to $DEST..."
rsync -a --exclude='._*' "$SRC_GAME/code" "$SRC_GAME/content" "$SRC_GAME/meta" "$DEST/"
echo "Applying update by overlaying v208 files..."
rsync -a --exclude='._*' "$SRC_UPDATE/code" "$SRC_UPDATE/content" "$SRC_UPDATE/meta" "$DEST/"

if [ -n "$SRC_DLC" ]; then
  echo "Copying DLC content..."
  mkdir -p "$DEST/aoc"
  rsync -a --exclude='._*' "$SRC_DLC/content/" "$DEST/aoc/"
fi

find "$DEST" -name '._*' -delete 2>/dev/null || true
echo "Game folder ready: $DEST"
