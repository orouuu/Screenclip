# screenclip

![screenclip](screenclip.png)

Une barrette à écran : elle affiche en boucle les GIF d'une carte microSD sur un écran Waveshare ESP32-S3-LCD-3.16 (320 x 820).

## Installer

1. Télécharge ce dossier (bouton vert « Code », puis « Download ZIP ») et extrais tout le zip.
2. Branche la barrette en USB.
3. Double-clique sur `_MAC/flash_mac.command` ou `_WINDOWS/flash_windows.bat`.

La première fois, il faut internet et quelques minutes : les outils s'installent tout seuls. Si la barrette n'est pas en mode programmation, la fenêtre explique le geste à faire avec les boutons BOOT et RST.

## Utiliser

Tout se fait avec le bouton BOOT :

| Geste | Action |
|---|---|
| Un appui | image suivante |
| Appui long | image précédente |
| Deux appuis | tourne l'image de 90° (sur l'écran Métro : change de ligne) |

Pour ajouter des GIF, branche la barrette à un ordinateur : elle apparaît comme une clé USB. Copie tes `.gif` dans le dossier `gifs`, puis éjecte le disque.

- Taille conseillée : 410 x 160 pixels.
- 6 Mo maximum par GIF, 1024 pixels de large maximum.
- Carte microSD au format FAT32.

Après les GIF, le carrousel contient un panneau de métro parisien (16 lignes, sans internet). La barrette ne connaît pas l'heure : le panneau rejoue une journée type, les minutes affichées ne sont pas celles des vrais métros.

## Modifier

Le code est dans `code/` (projet PlatformIO). Les réglages sont dans `code/include/reglages.h` : orientation, luminosité, durées, signature. Relance ensuite le fichier d'installation.

## Licence

CC BY-NC-SA 4.0, voir `LICENSE`. Composants tiers dans `NOTICE.md`.

Projet non officiel, sans lien avec Île-de-France Mobilités ni la RATP.
