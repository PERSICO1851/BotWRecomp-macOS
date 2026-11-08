#!/bin/zsh
# ==============================================================================
#  BotWRecomp All-in-One Automated Builder (macOS Apple Silicon)
#  100% Legal BYOG (Bring Your Own Game) pipeline
# ==============================================================================
set -e

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
EXPECTED_HASH="ba58da5b95ce929e005d058ceb08b9b2788d1ab2bbc8a6c189bbadca0bb34d30"

echo ""
echo "=================================================================="
echo "   🎮 Zelda: Breath of the Wild - Native Mac App Builder"
echo "   Build nativa per Apple Silicon (Metal) da file Wii U (BYOG)"
echo "=================================================================="
echo ""

# 1. Controllo architettura Apple Silicon
ARCH=$(uname -m)
if [ "$ARCH" != "arm64" ]; then
    echo "❌ Errore: questo builder richiede un Mac con Apple Silicon (M1/M2/M3/M4/arm64)."
    exit 1
fi

# 2. Controllo Homebrew e dipendenze
if ! command -v brew >/dev/null 2>&1; then
    echo "❌ Errore: Homebrew non trovato nel PATH. Installalo da https://brew.sh"
    exit 1
fi
BREW_PREFIX="$(brew --prefix)"
export PATH="$BREW_PREFIX/bin:$PATH"
MACOS_TARGET="${BOTW_MACOS_DEPLOYMENT_TARGET:-$(sw_vers -productVersion)}"

echo "🔍 Controllo delle dipendenze di sistema..."
DEPS=(cmake ninja uv sdl3 fmt boost glm libpng openssl@3 zstd)
MISSING_DEPS=()
for dep in "${DEPS[@]}"; do
    if ! brew list "$dep" >/dev/null 2>&1; then
        MISSING_DEPS+=("$dep")
    fi
done

if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo "📦 Installazione dipendenze mancanti via Homebrew: ${MISSING_DEPS[*]}..."
    brew install "${MISSING_DEPS[@]}"
else
    echo "✅ Tutte le dipendenze Homebrew sono già installate."
fi

# 3. Individuazione cartella del gioco
GAME_PATH="$1"
if [ -z "$GAME_PATH" ]; then
    echo ""
    echo "Trascina qui la cartella dei file di gioco (oppure scrivi il percorso completo):"
    read -r GAME_PATH
    # Rimuovi eventuali apici o escape da drag&drop
    GAME_PATH="${GAME_PATH%\'}"
    GAME_PATH="${GAME_PATH#\'}"
    GAME_PATH="${GAME_PATH%\"}"
    GAME_PATH="${GAME_PATH#\"}"
fi

if [ ! -d "$GAME_PATH" ]; then
    echo "❌ Errore: Cartella non trovata: $GAME_PATH"
    exit 1
fi

# Cerca U-King.rpx nella cartella fornita
RPX_PATH=$(find "$GAME_PATH" -name "U-King.rpx" | head -n 1)
if [ -z "$RPX_PATH" ] || [ ! -f "$RPX_PATH" ]; then
    echo "❌ Errore: 'U-King.rpx' non trovato all'interno di $GAME_PATH."
    echo "   Assicurati di fornire la cartella estratta (code/content/meta) dell'aggiornamento v1.5.0 (v208)."
    exit 1
fi

echo "✅ Trovato eseguibile Wii U: $RPX_PATH"
ACTUAL_HASH=$(shasum -a 256 "$RPX_PATH" | awk '{print $1}')
if [ "$ACTUAL_HASH" != "$EXPECTED_HASH" ]; then
    echo "⚠️  Attenzione: l'hash dell'eseguibile non corrisponde a quello di BotW v1.5.0 (v208)!"
    echo "   Atteso: $EXPECTED_HASH"
    echo "   Trovato: $ACTUAL_HASH"
    echo "   La ricompilazione potrebbe fallire se non si usa la versione v208."
    read -q "CONTINUE?Vuoi tentare comunque la ricompilazione? (s/n) "
    echo ""
    if [[ ! "$CONTINUE" =~ ^[SsYy]$ ]]; then
        exit 1
    fi
fi

# 4. Ricompilazione statica PowerPC -> C++
echo ""
echo "⚙️  1/3: Ricompilazione statica PowerPC -> C++ nativo (recompile.py)..."
cd "$REPO_DIR"
uv run python -I tools/recompile.py "$RPX_PATH" generated

# 5. Compilazione del binario nativo. CMake filters AppleDouble files from its source globs.
echo ""
echo "🔨 2/3: Compilazione con CMake e Ninja..."
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH="$BREW_PREFIX" -DBOTW_HOMEBREW_PREFIX="$BREW_PREFIX" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_TARGET"
ninja -C build

# 6. Creazione del pacchetto .app
echo ""
echo "📦 3/3: Creazione dell'applicazione nativa per macOS..."
OUTPUT_APP="${BOTW_APP_OUTPUT:-$REPO_DIR/dist/BotWRecomp.app}"
mkdir -p "$OUTPUT_APP/Contents/MacOS"
mkdir -p "$OUTPUT_APP/Contents/Resources"

# Build an iconset from the repository's source PNG, then package it as ICNS.
ICON_TMP="$REPO_DIR/build/macos-icon"
mkdir -p "$ICON_TMP/iconset"
swift "$REPO_DIR/tools/make_macos_icon.swift" "$ICON_TMP/icon.png"
sips -z 16 16 "$ICON_TMP/icon.png" --out "$ICON_TMP/iconset/icon_16x16.png" >/dev/null
sips -z 32 32 "$ICON_TMP/icon.png" --out "$ICON_TMP/iconset/icon_32x32.png" >/dev/null
sips -z 64 64 "$ICON_TMP/icon.png" --out "$ICON_TMP/iconset/icon_64x64.png" >/dev/null
sips -z 128 128 "$ICON_TMP/icon.png" --out "$ICON_TMP/iconset/icon_128x128.png" >/dev/null
sips -z 256 256 "$ICON_TMP/icon.png" --out "$ICON_TMP/iconset/icon_256x256.png" >/dev/null
sips -z 512 512 "$ICON_TMP/icon.png" --out "$ICON_TMP/iconset/icon_512x512.png" >/dev/null
cp "$ICON_TMP/icon.png" "$ICON_TMP/iconset/icon_512x512@2x.png"
python3 "$REPO_DIR/tools/make_macos_icns.py" "$ICON_TMP/iconset" "$OUTPUT_APP/Contents/Resources/AppIcon.icns"
cp "$REPO_DIR/build/BotWRecomp" "$OUTPUT_APP/Contents/MacOS/BotWRecomp-runtime"

cat << 'PLIST_EOF' > "$OUTPUT_APP/Contents/Info.plist"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>BotWRecomp</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleIdentifier</key>
    <string>com.botwrecomp.mac</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>BotWRecomp</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.5.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSMinimumSystemVersion</key>
    <string>__MACOS_MIN_VERSION__</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
PLIST_EOF
/usr/bin/sed -i '' "s/__MACOS_MIN_VERSION__/$MACOS_TARGET/" "$OUTPUT_APP/Contents/Info.plist"

cat << 'LAUNCH_EOF' > "$OUTPUT_APP/Contents/MacOS/BotWRecomp"
#!/bin/bash
set -e
APP_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
SUPPORT_DIR="$HOME/Library/Application Support/BotWRecomp"
GAME_PATH="${BOTW_GAME:-}"
if [ -z "$GAME_PATH" ] && [ -f "$SUPPORT_DIR/game_path" ]; then
    IFS= read -r GAME_PATH < "$SUPPORT_DIR/game_path"
fi
if [ -z "$GAME_PATH" ] || [ ! -f "$GAME_PATH/code/U-King.rpx" ]; then
    if [ -f "$APP_DIR/../../game/code/U-King.rpx" ]; then
        GAME_PATH="$APP_DIR/../../game"
    else
        GAME_PATH="$(/usr/bin/osascript -e 'POSIX path of (choose folder with prompt "Select your legally obtained BotW game folder (code, content, meta)")')"
    fi
    mkdir -p "$SUPPORT_DIR"
    printf '%s\n' "$GAME_PATH" > "$SUPPORT_DIR/game_path"
fi
exec "$APP_DIR/Contents/MacOS/BotWRecomp-runtime" --game "$GAME_PATH" "$@"
LAUNCH_EOF

chmod +x "$OUTPUT_APP/Contents/MacOS/BotWRecomp"
xattr -cr "$OUTPUT_APP" 2>/dev/null || true

echo ""
echo "=================================================================="
echo "🎉 BUILD COMPLETATA CON SUCCESSO!"
echo "   L'applicazione è pronta in: $OUTPUT_APP"
echo "   (Puoi avviarla direttamente da Launchpad o dalla cartella Applicazioni)"
echo "=================================================================="
echo ""
