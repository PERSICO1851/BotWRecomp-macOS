#!/bin/bash
set -e

SRC_GAME="/Volumes/T7/Applications/The Legend of Zelda Breath of the Wild [Game] [00050000101c9500]"
SRC_UPDATE="/Volumes/T7/Applications/The Legend of Zelda Breath of the Wild [Update] [0005000e101c9500]"
SRC_DLC="/Volumes/T7/Applications/The Legend of Zelda Breath of the Wild [DLC] [0005000c101c9500]"
DEST="/Volumes/T7/game"

echo "=== Creazione cartella $DEST ==="
mkdir -p "$DEST"

echo "1/3: Copia del gioco base..."
rsync -a --exclude="._*" "$SRC_GAME/code" "$SRC_GAME/content" "$SRC_GAME/meta" "$DEST/"

echo "2/3: Applicazione aggiornamento v208 (sovrascrittura)..."
rsync -a --exclude="._*" "$SRC_UPDATE/code" "$SRC_UPDATE/content" "$SRC_UPDATE/meta" "$DEST/"

if [ -d "$SRC_DLC/content" ]; then
    echo "3/3: Configurazione DLC in $DEST/aoc..."
    mkdir -p "$DEST/aoc"
    rsync -a --exclude="._*" "$SRC_DLC/content/" "$DEST/aoc/"
fi

# Pulizia di eventuali file sidecar ._*
find "$DEST" -name "._*" -delete 2>/dev/null || true

echo "=== COMPLETATO CON SUCCESSO! ==="
