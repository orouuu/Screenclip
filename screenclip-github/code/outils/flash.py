import os
import subprocess
import sys
import threading
import time

import carte

GESTE = (
    "Pour passer la barrette en mode programmation :\n"
    "  1. appuie sur le bouton BOOT et garde-le enfoncé ;\n"
    "  2. sans lâcher BOOT, appuie une fois sur RST et relâche RST ;\n"
    "  3. relâche BOOT seulement après.\n"
    "L'écran doit rester noir. S'il se rallume, recommence.\n"
    "Ensuite relance ce fichier."
)

port = carte.find(programming=True)
if not port:
    if carte.find(programming=False):
        sys.exit("La barrette est allumée mais pas en mode programmation.\n\n" + GESTE)
    sys.exit("Barrette introuvable : branche-la en USB avec un câble qui passe les données.\n\n" + GESTE)

STAGES = (
    ("Downloading", "Téléchargement des outils"),
    ("Unpacking", "Installation des outils"),
    ("Compiling", "Préparation du programme"),
    ("Linking", "Préparation du programme"),
    ("Uploading", "Envoi vers la barrette"),
    ("Writing at", "Envoi vers la barrette"),
)

NETWORK_ERRORS = ("getaddrinfo", "name resolution", "nodename nor servname", "connection error", "connectionerror",
                  "max retries exceeded", "network is unreachable", "timed out", "httpclienterror",
                  "could not resolve", "internetconnectionerror")

def run_with_wait(args, cwd):
    proc = subprocess.Popen(args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            encoding="utf-8", errors="replace")
    log, stage = [], ["Démarrage"]

    def read():
        for line in proc.stdout:
            log.append(line)
            for word, name in STAGES:
                if word in line:
                    stage[0] = name

    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    start, tick = time.time(), 0
    while proc.poll() is None:
        seconds = int(time.time() - start)
        line = f"{stage[0]}, veuillez patienter{'.' * (tick % 4)}"
        sys.stdout.write(f"\r{line:<52}{seconds // 60}:{seconds % 60:02d} ")
        sys.stdout.flush()
        tick += 1
        time.sleep(0.4)
    reader.join(2)
    sys.stdout.write("\r" + " " * 62 + "\r")
    if proc.returncode != 0:
        print("".join(log[-30:]))
    text = "".join(log).lower()
    offline = any(word in text for word in NETWORK_ERRORS)
    return proc.returncode, offline

project = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
first_time = not os.path.isdir(os.path.join(project, ".pio", "build"))
if first_time:
    print("Première fois : il faut internet, comptez plusieurs minutes. Ne fermez pas la fenêtre.")
code, offline = run_with_wait([sys.executable, "-m", "platformio", "run", "-t", "upload", "--upload-port", port],
                              project)
if code != 0:
    if offline:
        sys.exit("Le téléchargement des outils a échoué : vérifie ta connexion internet, puis relance ce fichier.")
    if first_time:
        print("Le flash a échoué. La première fois il faut internet : vérifie ta connexion.")
        sys.exit("Vérifie aussi le câble, puis recommence.\n\n" + GESTE)
    sys.exit("Le flash a échoué. Vérifie le câble, puis recommence.\n\n" + GESTE)

home = os.path.join(os.path.expanduser("~"), ".platformio")
esptool = os.path.join(home, "packages", "tool-esptoolpy", "esptool.py")

tool_python = os.path.join(home, "penv", "Scripts" if os.name == "nt" else "bin", "python.exe" if os.name == "nt" else "python")
if not os.path.exists(tool_python):
    tool_python = sys.executable
if subprocess.call([tool_python, esptool, "--port", port, "--after", "watchdog-reset", "read-mac"],
                   stdout=subprocess.DEVNULL) != 0:
    print("Flash terminé. Appuie une fois sur RST pour relancer la barrette.")
else:
    print("Flash terminé, la barrette redémarre.")
