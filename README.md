# DUODRONE — firmware drone pour M-VAVE FM-1

Firmware alternatif qui transforme le FM-1 en synthé drone analogique à deux voix, sur le modèle du Stylophone CPM DS-2 : deux voix DRONE, un ULTRA LFO à deux sorties, un délai sombre et une réverbération à 8 algorithmes. Projet indépendant, sans lien avec Dübreq ni M-VAVE ; le son est une modélisation numérique, pas une copie du circuit.

Bâti sur [foma](https://github.com/konsumer/foma) (plateforme Felucca) : installeur web, mode secours et retour au firmware d'origine inclus.

## Architecture sonore

| Section | Contenu |
|---|---|
| DRONE I et II | oscillateur X-WAVE (triangle → dent de scie → carré → impulsion étroite, anti-aliasé), sub-oscillateur carré −1 ou −2 octaves, OSC MIX, filtre 4 pôles LP/HP résonant (auto-oscillation), VOLUME qui sature au-delà de ~60 % |
| Couplage | HARD SYNC (II calé sur I, I coupé du mix comme sur l'original), PITCH LINK (II suit I avec son intervalle) |
| ULTRA LFO | une vitesse (0,02–70 Hz), deux sorties WAVE I / WAVE II, 13 formes + OFF chacune ; routage par voix vers X-WAVE ou PITCH, et vers la coupure (atténuverteurs bipolaires) |
| MIXER | AUX IN (audio USB depuis l'ordinateur), délai type PT2399 (MIX, TIME, REPEATS, modulation LFO), réverb CH/HALL, HALL 1, HALL 2, PLATE, ROOM, CATHED, SPRING, ENDLESS, MAIN OUT |

## Commandes

| Commande FM-1 | Fonction |
|---|---|
| HOME | page PERFORM : CUTOFF I, CUTOFF II, LFO RATE, REVERB MIX |
| SCL | DRONE I (re-presser : OSC ↔ FILTER) |
| ENV | DRONE II (re-presser : OSC ↔ FILTER) |
| LFO | ULTRA LFO (re-presser : routage WAVE I / WAVE II) |
| EDIT | interrupteurs : SUB, routage LFO, SYNC, LINK, LP/HP |
| FX | DELAY (re-presser : REVERB) |
| GLO | MIXER : VOLUME I, VOLUME II, AUX IN, MAIN |
| KNOB 1–4 | les quatre réglages de la page |
| SELECT / ALGORITHM | hauteur DRONE I / DRONE II (demi-tons) |
| REC maintenu | pas fins (5 cents pour la hauteur) |
| Touches | règlent la hauteur ; SEQ choisit la cible : I, II, I+II ou GATE (le son ne joue que touche enfoncée) |
| OCT− / OCT+ | octave du clavier (les deux : remise à zéro) |
| PLAY | drones ON/OFF (fondu) |
| ARP | remise à zéro du LFO ; ARP et SEQ clignotent avec WAVE I et WAVE II |
| PRESETS | F1–F8 usine, U1–U8 utilisateur |
| SAVE | enregistrer : PRESETS choisit l'emplacement, SAVE confirme, HOME annule |
| MASTER | volume |
| MIDI | note on = hauteur, CC 20–59 = les 40 paramètres, CC 123 = all notes off |

Presets usine : FIFTHS, DEEP SPACE, PWM CHOIR, SYNC SCREAM, CATHEDRAL, PULSE TRAIN, SPRING TWANG, INFINITE.

## Obtenir le fichier .fwsc

Le compilateur JieLi du FM-1 ne tourne que sous Linux x86-64 ; deux façons sans rien installer de compliqué :

**A. GitHub (rien à installer)**
1. Crée un dépôt GitHub et pousse ce dossier sur la branche `main`.
2. Onglet Actions : le workflow `build` compile tout. Le `.fwsc` est dans l'artefact `duodrone-fwsc` du run.
3. Optionnel : Settings → Pages → Source : GitHub Actions publie l'émulateur et l'installeur web.

**B. Docker Desktop (Mac/Windows/Linux)**
```sh
./fm1 build     # → build/duodrone-0.1.fwsc
./fm1 dev       # émulateur + installeur sur http://localhost:8000
```

## Installer sur le FM-1

Avec Chrome ou Edge, page Install de l'installeur (via `./fm1 dev` ou GitHub Pages) : brancher le FM-1 en USB (câble data, sans hub), choisir le `.fwsc`, INSTALL. En ligne de commande : `python3 tools/fm1_install.py build/duodrone-0.1.fwsc` (nécessite `pip install mido python-rtmidi`).

**Secours** : si le FM-1 ne démarre plus, maintenir OCT− à l'allumage (mode secours USB) puis réinstaller. L'onglet Rescue réinstalle aussi le firmware M-VAVE V15 d'origine.

Installer un firmware non officiel se fait à vos risques.

## Développement

- `src/dsp.c` : moteur sonore (entiers uniquement, le FM-1 n'a pas de FPU)
- `src/main.c` : interface, pages, presets, MIDI
- `src/presets.c` : sons d'usine
- `tests/run.sh` : test hôte, rend les 8 presets en WAV dans `build/render-out/` et fuzz tous les paramètres
- Identité du paquet : `FM-1_92` (dans `firmware.json`)

## Licence

GPL-3.0-only. Crédits : [LICENSING.md](LICENSING.md).
