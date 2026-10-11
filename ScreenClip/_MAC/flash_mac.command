#!/bin/bash
cd "$(dirname "$0")/../code"
source outils/python.sh
"$PY" outils/flash.py
echo
read -n 1 -s -r -p "Appuie sur une touche pour fermer."
