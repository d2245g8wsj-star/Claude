#!/bin/bash
# Dubbelklik om iPod Sync te starten (macOS). De eerste keer wordt alles geïnstalleerd.
set -e
cd "$(dirname "$0")"

PY=""
for c in /opt/homebrew/bin/python3 /usr/local/bin/python3 python3; do
  if command -v "$c" >/dev/null 2>&1 && "$c" -c "import tkinter" >/dev/null 2>&1; then PY="$c"; break; fi
done
if [ -z "$PY" ]; then
  echo "Python met Tkinter niet gevonden."
  echo "Installeer het met:  brew install python python-tk ffmpeg"
  read -r -p "Druk op Enter om te sluiten…"; exit 1
fi
if ! command -v ffmpeg >/dev/null 2>&1 && [ ! -x /opt/homebrew/bin/ffmpeg ] && [ ! -x /usr/local/bin/ffmpeg ]; then
  echo "Let op: ffmpeg ontbreekt (nodig voor YouTube en flac/ogg). Installeer met: brew install ffmpeg"
fi

if [ ! -x .venv/bin/python ]; then
  echo "Eerste keer: omgeving klaarzetten…"
  "$PY" -m venv .venv
fi
.venv/bin/python -m pip install -q --upgrade pip >/dev/null
# yt-dlp moet regelmatig worden bijgewerkt omdat YouTube vaak verandert
.venv/bin/python -m pip install -q --upgrade -e . yt-dlp
exec .venv/bin/python -m ipodsync
