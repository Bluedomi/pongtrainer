# PongTrainer — Jeu Pong avancé en C++ (v3.2)

**PongTrainer** est une implémentation moderne et didactique du jeu *Pong*, écrite en C++17 et conçue pour fonctionner exclusivement dans un **terminal Linux**.  
Au‑delà du jeu lui‑même, ce programme illustre plusieurs mécanismes fondamentaux de la programmation système :

- **communication inter‑processus (IPC)**,
- **mémoire partagée POSIX** (`shm_open`, `mmap`),
- **mutex partagés entre processus** (`pthread_mutex_t` + `PTHREAD_PROCESS_SHARED`),
- **synchronisation maître/esclave**,
- **rendu terminal proportionnel** basé sur un espace virtuel indépendant de la taille du terminal.

Ce projet peut servir :
- d’exemple pédagogique pour des cours de systèmes ou de programmation avancée,
- de base pour des travaux pratiques universitaires,
- ou simplement de jeu rétro amusant et techniquement élégant.

---

## 🚀 Compilation

Compiler avec `g++` :

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -o pongtrainer pongtrainer.cpp && strip pongtrainer
