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
export PATH="/opt/homebrew/bin:$PATH"
if ! command -v brew >/dev/null 2>&1; then
    echo "❌ Errore: Homebrew non trovato in /opt/homebrew. Installalo da https://brew.sh"
    exit 1
fi

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

# 5. Pulizia file AppleDouble se su filesystem FAT/exFAT
find "$REPO_DIR" -name "._*" -delete 2>/dev/null || true

# 6. Compilazione del binario nativo
echo ""
echo "🔨 2/3: Compilazione con CMake e Ninja..."
cmake -B build -G Ninja
ninja -C build

# 7. Creazione del pacchetto .app
echo ""
echo "📦 3/3: Creazione dell'applicazione nativa per macOS..."
OUTPUT_APP="/Applications/The Legend of Zelda - Breath of the Wild.app"
rm -rf "$OUTPUT_APP"
mkdir -p "$OUTPUT_APP/Contents/MacOS"
mkdir -p "$OUTPUT_APP/Contents/Resources"

# Icona
if [ -f "/tmp/AppIcon.icns" ]; then
    cp /tmp/AppIcon.icns "$OUTPUT_APP/Contents/Resources/AppIcon.icns"
fi

cat << 'PLIST_EOF' > "$OUTPUT_APP/Contents/Info.plist"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>ZeldaBotW</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleIdentifier</key>
    <string>com.nintendo.zelda.botw.recomp</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>Zelda BotW</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.5.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSMinimumSystemVersion</key>
    <string>13.3</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
PLIST_EOF

cat << LAUNCH_EOF > "$OUTPUT_APP/Contents/MacOS/ZeldaBotW"
#!/bin/zsh
export PATH="/opt/homebrew/bin:\$PATH"
exec "$REPO_DIR/build/BotWRecomp" --game "$GAME_PATH" "\$@"
LAUNCH_EOF

chmod +x "$OUTPUT_APP/Contents/MacOS/ZeldaBotW"
xattr -cr "$OUTPUT_APP" 2>/dev/null || true

echo ""
echo "=================================================================="
echo "🎉 BUILD COMPLETATA CON SUCCESSO!"
echo "   L'applicazione è pronta in: $OUTPUT_APP"
echo "   (Puoi avviarla direttamente da Launchpad o dalla cartella Applicazioni)"
echo "=================================================================="
echo ""
