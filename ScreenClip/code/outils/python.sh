PY="$HOME/.platformio/penv/bin/python"
if ! "$PY" -c "import platformio, serial" >/dev/null 2>&1; then
  PY="$HOME/.screenclip/venv/bin/python"
fi
if ! "$PY" -c "import platformio, serial" >/dev/null 2>&1; then
  echo "Installation des outils (Python, PlatformIO) : il faut internet, veuillez patienter quelques minutes."
  UV="$(command -v uv || true)"
  if [ -z "$UV" ]; then
    UV="$HOME/.local/bin/uv"
    if [ ! -x "$UV" ]; then
      curl -LsSf https://astral.sh/uv/install.sh | sh || { echo "Installation impossible : vérifie ta connexion internet, puis relance."; exit 1; }
    fi
  fi
  "$UV" venv --python 3.12 "$HOME/.screenclip/venv" || { echo "Installation de Python impossible : vérifie ta connexion internet, puis relance."; exit 1; }
  "$UV" pip install --python "$PY" platformio pyserial || { echo "Installation de PlatformIO impossible : vérifie ta connexion internet, puis relance."; exit 1; }
  echo "Installation terminée."
fi
