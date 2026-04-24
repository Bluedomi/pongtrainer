# PongTrainer 😎 — Jeu Pong TTY avancé en C++ (v3.3.14)

**PongTrainer** est une implémentation moderne et didactique du jeu *Pong* ASCII en mode maître/esclave, écrite en C++17 et conçue pour fonctionner exclusivement dans un **terminal Linux**, utilisant mémoire partagée, synchronisation fine via mutex et un rendu terminal non bloquant basé sur write(). Conçu en hommage aux premières bornes d’arcade, ce petit jeu s’exécute entièrement dans un TTY (y compris un TTY virtuel Linux).
 
Au‑delà du jeu lui‑même, ce programme illustre plusieurs mécanismes fondamentaux de la programmation système :

- **communication inter‑processus (IPC)**,
- **mémoire partagée POSIX** (`shm_open`, `mmap`),
- **mutex partagés entre processus** (`pthread_mutex_t` + `PTHREAD_PROCESS_SHARED`),
- **synchronisation maître/esclave**,
- **rendu terminal proportionnel** basé sur un espace virtuel indépendant de la taille du terminal.

Ce projet peut servir :
- d’exemple pédagogique pour des cours de systèmes ou de programmation avancée,
- de base pour des travaux pratiques dans l'enseignement,
- ou simplement de jeu rétro amusant et techniquement élégant.

---

## 🚀 Compilation

Compiler avec `g++` :

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -o pongtrainer pongtrainer.cpp && strip pongtrainer
```

Dépendances :
- Linux / BSD / macOS
- Terminal compatible UTF‑8
- POSIX threads
- POSIX shared memory (`shm_open`)

---

## 🎮 Utilisation

### Commandes de base

| Action | Touche(s) |
| :--- | :--- |
| **Raquette Gauche** | `a` (Haut) / `q` (Bas) |
| **Raquette Droite** | `p` (Haut) / `m` (Bas) |
| **Pause** | `Espace` |
| **Quitter** | `x` |

---

## 🎮 Modes de Jeu

Le programme adapte automatiquement son comportement selon les arguments fournis.

---

### 🕹️ Mode Solo (Humain vs Humain)

```bash
./pongtrainer
```

---

### 🤖 Mode Solo avec IA (Entraînement)

```bash
./pongtrainer -a        # IA à gauche
./pongtrainer -a right  # IA à droite
./pongtrainer -b        # IA vs IA (mode spectateur)
```

---

## 🧩 Mode Multiterminal (Maître / Esclave)

PongTrainer v3.2 utilise la **mémoire partagée POSIX** pour synchroniser deux terminaux.  
Le maître exécute la physique et le rendu principal ; l’esclave affiche l’état du jeu en temps réel et peut contrôler une raquette.

### Terminal 1 — Le Maître

```bash
./pongtrainer -m left
```

ou

```bash
./pongtrainer -m right
```

### Terminal 2 — L’Esclave

```bash
./pongtrainer
```

L’esclave détecte automatiquement la session du maître et :

- se connecte à la mémoire partagée,
- affiche le jeu en temps réel,
- prend le contrôle de la raquette opposée **si l’IA n’est pas activée**.

---

## 🔊 Mode silencieux

Désactiver les bips :

```bash
./pongtrainer -q
```

---

## ⚙️ Architecture interne

### Principales composantes

- **Term** : gestion du terminal (taille, mode raw, curseur)
- **Ball** : physique de la balle (rebonds, accélération progressive)
- **Paddle** : physique des raquettes (friction, inertie, IA simple)
- **Renderer** : projection espace virtuel → terminal
- **SharedState** : structure partagée via `mmap` + mutex POSIX
- **GameSolo / GameMaster / GameSlave** : trois boucles de jeu distinctes

### Synchronisation

- `pthread_mutex_t` partagé entre processus  
- Mise à jour continue de l’état du jeu par le maître  
- L’esclave lit uniquement, sauf pour transmettre les commandes joueur  

---

## 📦 Structure du dépôt

```
pongtrainer/
 ├── pongtrainer.cpp   # Code source complet
 ├── README.md         # Documentation
 └── LICENSE           # MIT
```

---

## 📄 Licence

```
MIT License
Copyright (c) 2026 Dominique
Document LICENSE ci-joint.
```

---

## 🧠 Objectif didactique

Ce projet illustre :

- la séparation **physique / rendu**,
- la gestion d’un **espace virtuel** indépendant du terminal,
- la **projection proportionnelle** (mapping X/Y),
- la **synchronisation inter‑processus** (mutex partagé),
- la **communication maître/esclave** via mémoire partagée,
- la gestion du **terminal en mode raw**,
- la **physique simple mais stable** (DT constant, accélération progressive).

Il constitue une excellente base pour :

- des travaux pratiques en C/C++,
- des démonstrations de systèmes concurrents,
- des projets d’architecture logicielle,
- des expérimentations graphiques en terminal,
- 🎉 offrir une pause ludique entre deux compilations à ceux qui vivent littéralement dans leur TTY.

---

## 🧭 Auteur

**Dominique (aka /b4sh 😎)**  
Belgique — 2026
