#!/bin/bash
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$DIR/build"
SKETCH="$DIR/LanTestLogger.ino"
ARDUINO_CLI="/home/angelo/local/bin/arduino-cli"
BOARD="rp2040:rp2040:rpipicow:flash=2097152_1048576,ipbtstack=ipv4btcble"

mkdir -p "$BUILD_DIR"

echo "Compilando..."
$ARDUINO_CLI compile -b "$BOARD" "$DIR" 2>&1

UF2_SRC=$(find /tmp/arduino/sketches -name "LanTestLogger.ino.uf2" -newer "$SKETCH" 2>/dev/null | head -1)
if [ -z "$UF2_SRC" ]; then
  # fallback: pega o mais recente
  UF2_SRC=$(find /tmp/arduino/sketches -name "LanTestLogger.ino.uf2" -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2)
fi

if [ -n "$UF2_SRC" ]; then
  DATE=$(date +%Y-%m-%d)
  DEST="$BUILD_DIR/LanTestLogger-$DATE.uf2"
  cp "$UF2_SRC" "$DEST"
  echo "UF2 salvo em: $DEST"
  ls -lh "$DEST"
else
  echo "Erro: arquivo .uf2 não encontrado!"
  exit 1
fi
