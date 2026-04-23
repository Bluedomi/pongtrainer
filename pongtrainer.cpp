/********************************************************************
 *  Source      : pongtrainer.cpp
 *  Version     : v3.3.14
 *  Date        : 2026‑04‑23
 *  Auteur      : Dominique (aka /b4sh 😎)
 *
 *  Objet :
 *      Implémentation didactique d’un Pong ASCII en mode maître/esclave,
 *      utilisant mémoire partagée, synchronisation fine via mutex, et un
 *      rendu terminal non bloquant basé sur write(). Conçu en hommage aux
 *      premières bornes d’arcade, ce petit jeu fonctionne entièrement dans
 *      un terminal TTY (notamment un TTY virtuel Linux).
 *
 *  Caractéristiques techniques :
 *      - Physique : 60 Hz
 *      - Rendu    : 20 Hz
 *      - Communication maître/esclave via shm + mutex
 *      - Rendu ASCII robuste (aucun freeze, aucun scroll)
 *
 *  Paramètres CLI :
 *      -m [left|right]   : lancer en mode maître
 *      -a [left|right]   : activer IA sur une raquette
 *      -b                : activer IA sur les deux raquettes
 *      -f                : paddles fixes
 *      -q                : mode silencieux (désactive le beep)
 *
 *  Contrôles clavier :
 *      Raquette gauche  : 'a' = haut,  'q' = bas
 *      Raquette droite  : 'p' = haut,  'm' = bas
 *      Pause            : barre d’espace (toggle)
 *      Quitter          : 'x'
 *
 *  ----------------------------------------------------------------
 *  1. Compilation : trois profils selon vos besoins
 *  ----------------------------------------------------------------
 *
 *  (A) Compilation optimisée pour VOTRE CPU
 *      → Performances maximales, mais binaire moins portable.
 *
 *      g++ -O3 -march=native -mtune=native pongtrainer.cpp \
 *          -o pongtrainer -lpthread
 *      strip pongtrainer
 *
 *
 *  (B) Compilation portable et robuste (recommandée débutants)
 *      → Fonctionne sur presque tous les CPU x86_64/ARM64.
 *      → Affiche les warnings utiles.
 *
 *      g++ -std=c++17 -O2 -Wall -Wextra -pthread \
 *          -o pongtrainer pongtrainer.cpp
 *      strip pongtrainer
 *
 *
 *  (C) Compilation pour un binaire le plus petit possible
 *      → Optimisation pour la taille, sections nettoyées.
 *
 *      g++ -std=c++17 -Os -s -ffunction-sections -fdata-sections \
 *          -Wl,--gc-sections -pthread -o pongtrainer pongtrainer.cpp
 *      strip --strip-all pongtrainer
 *
 *  ----------------------------------------------------------------
 *  Remarque :
 *      Ce fichier est monolithique et sert de démonstration pratique
 *      des techniques de programmation employées. Toutes les
 *      informations nécessaires à la compilation et à l’utilisation
 *      sont regroupées ici pour faciliter l’expérimentation.
 * 
 *
 ********************************************************************/

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <pthread.h>
#include <string.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

using namespace std::chrono;

/* ============================================================
 *  CONSTANTES
 * ============================================================ */

static std::atomic<bool> g_running{true};

void onSigInt(int) {
    g_running.store(false, std::memory_order_relaxed);
}

static constexpr int TARGET_FPS = 20;
static constexpr int PHYSICS_HZ = 60;
static constexpr float DT = 1.0f / PHYSICS_HZ;
static constexpr float BALL_SPEED_FACTOR = 1.0f;
static constexpr float BALL_VERTICAL_RATIO = 0.3f;
static constexpr float BALL_ACCEL_PER_RALLY = 1.05f;
static constexpr float BALL_MAX_SPEED = 3.5f * PHYSICS_HZ;
static constexpr float BALL_MAX_ANGLE = 0.75f;
static constexpr int DEFAULT_PADDLE_HEIGHT = 5;

static bool g_quiet = false;

/* ============================================================
 *  TERMINAL
 * ============================================================ */

struct Term {
    int rows = 24, cols = 80;
    termios orig{};

    void updateSize() {
        struct winsize ws{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row && ws.ws_col) {
            rows = ws.ws_row;
            cols = ws.ws_col;
        }
    }

    void init() {
        updateSize();

        tcgetattr(STDIN_FILENO, &orig);
        termios raw = orig;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);

        fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);

        int flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
        fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK);

        std::cout << "\033[?25l\033[2J" << std::flush;
    }

    void restore() const {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig);
        std::cout << "\033[?25h\033[2J\033[H" << std::flush;
    }
};

/* ============================================================
 *  BEEPER
 * ============================================================ */

struct Beeper {
    static void beep() {
        if (!g_quiet) 
        if (!g_quiet) {
    [[maybe_unused]] ssize_t ignored = write(STDOUT_FILENO, "\a", 1);
}

    }
    static void scoreSound() {
        if (!g_quiet) {
            beep();
            std::this_thread::sleep_for(milliseconds(100));
            beep();
        }
    }
};

/* ============================================================
 *  PADDLE
 * ============================================================ */

struct Paddle {
    float y;
    float velocity = 0.0f;
    int height = DEFAULT_PADDLE_HEIGHT;
    int col;
    int virtRows;
    bool isAuto = false;

    Paddle(int vrows = 24, int column = 1)
        : y(1.0f), col(column), virtRows(vrows) {}

    void clampPos() {
        if (y < 1.0f) y = 1.0f;
        float maxY = (float)virtRows - height;
        if (y > maxY) y = maxY;
    }

    void moveUp()   { velocity -= 0.9f; }
    void moveDown() { velocity += 0.9f; }

    void updatePhysics() {
        y += velocity;
        velocity *= 0.60f;
        clampPos();
    }

    void aiPredict(float ballY) {
        if (!isAuto) return;
        float center = y + (height / 2.0f);
        if (ballY < center - 0.5f) moveUp();
        else if (ballY > center + 0.5f) moveDown();
    }

    bool hits(int bx, int by) const {
        int py = (int)roundf(y);
        return (bx == col && by >= py && by < py + height);
    }
};

/* ============================================================
 *  BALL
 * ============================================================ */

struct Ball {
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float prevX = 0.0f;
    float baseSpeed = 0.15f;
    int rallyCount = 0;

    Ball(int vrows = 24, int vcols = 80) {
        updateBaseSpeed(vcols);
        reset(vrows, vcols);
    }

    void updateBaseSpeed(int vcols) {
        baseSpeed = (float)vcols / 250.0f * PHYSICS_HZ * BALL_SPEED_FACTOR;
        if (baseSpeed < 0.15f) baseSpeed = 0.15f;
    }

    void reset(int vrows, int vcols) {
        x = prevX = vcols / 2.0f;
        y = vrows / 2.0f;
        rallyCount = 0;

        // Correction : alterne proprement gauche/droite
        vx = (vx >= 0.0f) ? -baseSpeed : baseSpeed;
        vy = baseSpeed * BALL_VERTICAL_RATIO;
    }

    int update(int vrows, int vcols, const Paddle &lp, const Paddle &rp) {
        prevX = x;
        x += vx * DT;
        y += vy * DT;

        if (y <= 1.0f) {
            y = 1.0f;
            vy = std::abs(vy);
            return 1;
        }
        if (y >= vrows - 1) {
            y = (float)vrows - 1;
            vy = -std::abs(vy);
            return 1;
        }

        if (vx > 0 && prevX < rp.col && x >= rp.col) {
            int iy = (int)roundf(y);
            if (rp.hits(rp.col, iy)) {
                float rel = (y - rp.y) / rp.height;
                rel = std::clamp(rel, 0.0f, 1.0f);
                float angle = (rel - 0.5f) * 2.0f;
                vy = angle * BALL_MAX_ANGLE * std::abs(vx);
                vx = -std::abs(vx);
                vx *= BALL_ACCEL_PER_RALLY;
                vy *= BALL_ACCEL_PER_RALLY;
                float speed = std::sqrt(vx * vx + vy * vy);
                if (speed > BALL_MAX_SPEED) {
                    float scale = BALL_MAX_SPEED / speed;
                    vx *= scale;
                    vy *= scale;
                }
                x = rp.col - 1;
                rallyCount++;
                return 2;
            }
        }

        if (vx < 0 && prevX > lp.col && x <= lp.col) {
            int iy = (int)roundf(y);
            if (lp.hits(lp.col, iy)) {
                float rel = (y - lp.y) / lp.height;
                rel = std::clamp(rel, 0.0f, 1.0f);
                float angle = (rel - 0.5f) * 2.0f;
                vy = angle * BALL_MAX_ANGLE * std::abs(vx);
                vx = std::abs(vx);
                vx *= BALL_ACCEL_PER_RALLY;
                vy *= BALL_ACCEL_PER_RALLY;
                float speed = std::sqrt(vx * vx + vy * vy);
                if (speed > BALL_MAX_SPEED) {
                    float scale = BALL_MAX_SPEED / speed;
                    vx *= scale;
                    vy *= scale;
                }
                x = lp.col + 1;
                rallyCount++;
                return 2;
            }
        }

        if (x <= 0) return 3;
        if (x >= vcols) return 4;

        return 0;
    }
};

/* ============================================================
 *  RENDERER — write() NON‑BLOQUANT
 * ============================================================ */

struct Renderer {
    std::string buf;
    bool fixedPaddles = false;

    static int mapX(float vx, int virtCols, int screenCols) {
        float fx = vx / (float)(virtCols - 1);
        fx = std::clamp(fx, 0.0f, 1.0f);
        return (int)std::round(fx * (screenCols - 1));
    }

    static int mapY(float vy, int virtRows, int screenRows) {
        float fy = (vy - 1.0f) / (float)(virtRows - 2);
        fy = std::clamp(fy, 0.0f, 1.0f);
        return std::clamp(1 + (int)std::round(fy * (screenRows - 3)), 1,
                          screenRows - 1);
    }

    static int mapHeight(int virtHeight, int virtRows, int screenRows) {
        float ratio = (float)virtHeight / (float)(virtRows - 2);
        int sh = (int)std::round(ratio * (screenRows - 2));
        return std::clamp(sh, 1, screenRows - 2);
    }

    void draw(const Ball &ball, const Paddle &lp, const Paddle &rp,
              int sL, int sR,
              int virtRows, int virtCols,
              int screenRows, int screenCols,
              bool paused, bool isMaster, bool isSlave, bool slaveHasControl)
    {
        if (screenRows < 4 || screenCols < 20) {
            const char *msg = "\033[H\033[2JTerminal trop petit.\n";
            [[maybe_unused]] ssize_t ignored = write(STDOUT_FILENO, msg, strlen(msg));
            return;
        }

        buf.clear();
        buf.reserve(screenRows * screenCols * 4);

        buf += "\033[H\033[7m";
        std::string bar(screenCols, ' ');

        std::string info;
        if (paused) info = "[PAUSE]";
        else if (isMaster) info = "[MASTER]";
        else if (isSlave && slaveHasControl) info = "[SLAVE: player]";
        else if (isSlave) info = "[SLAVE: watcher]";
        else info = " ";

        std::string left   = "P1:" + std::to_string(sL) + (lp.isAuto ? "(AI)" : "");
        std::string center = info;
        std::string right  = "P2:" + std::to_string(sR) + (rp.isAuto ? "(AI)" : "");

        int posLeft   = 1;
        int posCenter = (screenCols - (int)center.size()) / 2;
        int posRight  = screenCols - 1 - (int)right.size();

        auto copy = [&](int p, const std::string &s) {
            if (p < 0 || p >= screenCols) return;
            for (int i = 0; i < (int)s.size() && p + i < screenCols; ++i)
                bar[p + i] = s[i];
        };

        copy(posLeft, left);
        copy(posCenter, center);
        copy(posRight, right);

        buf += bar + "\033[0m\n";

        int bx = mapX(ball.x, virtCols, screenCols);
        int by = mapY(ball.y, virtRows, screenRows);

        int lpCol = mapX(lp.col, virtCols, screenCols);
        int rpCol = mapX(rp.col, virtCols, screenCols);

        int lpY = mapY(lp.y, virtRows, screenRows);
        int rpY = mapY(rp.y, virtRows, screenRows);

        int lpH = fixedPaddles ? std::min(lp.height, screenRows - 2)
                               : mapHeight(lp.height, virtRows, screenRows);
        int rpH = fixedPaddles ? std::min(rp.height, screenRows - 2)
                               : mapHeight(rp.height, virtRows, screenRows);

        for (int r = 1; r < screenRows - 1; ++r) {
            for (int c = 0; c < screenCols; ++c) {

                if (c == lpCol && r >= lpY && r < lpY + lpH) {
                    buf += "█";
                    continue;
                }

                if (c == rpCol && r >= rpY && r < rpY + rpH) {
                    buf += "█";
                    continue;
                }

                if (c == bx && r == by) {
                    buf += "😎";
                    c++;
                    continue;
                }

                if (c == screenCols / 2) {
                    buf += "│";
                    continue;
                }

                buf += ' ';
            }
            buf += '\n';
        }

        buf += "\033[7m";
        std::string bar2(screenCols, ' ');

        int padReal = lpH;

        std::string info2 =
            " virt=" + std::to_string(virtRows) + "x" + std::to_string(virtCols) +
            "  real=" + std::to_string(screenRows) + "x" + std::to_string(screenCols) +
            "  pad=" + std::to_string(padReal) + "/" + std::to_string(DEFAULT_PADDLE_HEIGHT) +
            "  mode=" + std::string(fixedPaddles ? "F" : "A");

        for (int i = 0; i < (int)info2.size() && i < screenCols; ++i)
            bar2[i] = info2[i];

        buf += bar2 + "\033[0m";

        ssize_t n = write(STDOUT_FILENO, buf.data(), buf.size());
        if (n < 0 && errno == EAGAIN) {
            return;
        }
    }
};

/* ============================================================
 *  SHARED MEMORY
 * ============================================================ */

enum class Side : int { LEFT = 0, RIGHT = 1 };

struct SharedState {
    pthread_mutex_t lock;

    bool master_alive;
    bool slave_alive;
    bool slave_connected;

    bool master_is_master;
    Side master_side;
    bool lp_isAI;
    bool rp_isAI;
    bool slave_up;
    bool slave_down;

    int virt_rows;
    int virt_cols;

    int slave_rows;
    int slave_cols;

    float ball_x, ball_y;
    float lp_y, rp_y;
    int lp_col, rp_col;
    int lp_height, rp_height;

    int scoreL, scoreR;
    bool paused;
};

static const char *SHM_NAME = "/pongtrainer_shm_v3";

bool init_shared_mutex(pthread_mutex_t *mtx) {
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0) return false;
    if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
        pthread_mutexattr_destroy(&attr);
        return false;
    }
    if (pthread_mutex_init(mtx, &attr) != 0) {
        pthread_mutexattr_destroy(&attr);
        return false;
    }
    pthread_mutexattr_destroy(&attr);
    return true;
}

/* ============================================================
 *  GameConfig
 * ============================================================ */

struct GameConfig {
    bool bothAI = false;
    bool autoLeft = false;
    bool autoRight = false;
    bool useAuto = false;
    bool isMaster = false;
    Side masterSide = Side::LEFT;
    bool fixedPaddles = false;
};

/* ============================================================
 *  GameSolo
 * ============================================================ */

struct GameSolo {
    Term term;
    Ball ball{term.rows, term.cols};
    Paddle lp{term.rows, 1}, rp{term.rows, term.cols - 1};
    Renderer ren;
    int sL = 0, sR = 0;
    bool paused = false;

    std::mutex mtx;
    bool physicsRunning = false;

    void applyConfig(const GameConfig &cfg) {
        ren.fixedPaddles = cfg.fixedPaddles;

        if (cfg.bothAI) {
            lp.isAuto = true;
            rp.isAuto = true;
        } else if (cfg.useAuto) {
            if (cfg.autoLeft)  lp.isAuto = true;
            if (cfg.autoRight) rp.isAuto = true;
        }

        if (cfg.fixedPaddles) {
            lp.height = DEFAULT_PADDLE_HEIGHT;
            rp.height = DEFAULT_PADDLE_HEIGHT;
        }
    }

    void physicsLoop(int virtRows, int virtCols) {
        auto frameTime = microseconds(1000000 / PHYSICS_HZ);
        auto next = steady_clock::now() + frameTime;

        while (g_running.load(std::memory_order_relaxed) && physicsRunning) {
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (!paused) {
                    if (lp.isAuto) lp.aiPredict(ball.y);
                    if (rp.isAuto) rp.aiPredict(ball.y);

                    lp.updatePhysics();
                    rp.updatePhysics();

                    int ev = ball.update(virtRows, virtCols, lp, rp);
                    if (ev == 1 || ev == 2) {
                        Beeper::beep();
                    } else if (ev >= 3) {
                        if (ev == 3) sR++;
                        else         sL++;
                        ball.reset(virtRows, virtCols);
                        Beeper::scoreSound();
                    }
                }
            }

            std::this_thread::sleep_until(next);
            next += frameTime;
        }
    }

    void run(const GameConfig &cfg) {
        term.init();
        std::signal(SIGINT, onSigInt);

        int virtRows = term.rows;
        int virtCols = term.cols;

        {
            std::lock_guard<std::mutex> lock(mtx);
            ball = Ball(virtRows, virtCols);
            lp = Paddle(virtRows, 0);
            rp = Paddle(virtRows, virtCols - 1);
            applyConfig(cfg);

            if (!cfg.fixedPaddles) {
                lp.height = DEFAULT_PADDLE_HEIGHT;
                rp.height = DEFAULT_PADDLE_HEIGHT;
            }

            sL = sR = 0;
            paused = false;
        }

        physicsRunning = true;
        std::thread physThread(&GameSolo::physicsLoop, this, virtRows, virtCols);

        auto frameTime = microseconds(1000000 / TARGET_FPS);
        auto next = steady_clock::now() + frameTime;

        while (g_running.load(std::memory_order_relaxed)) {
            term.updateSize();

            if (virtRows != term.rows || virtCols != term.cols) {
                std::lock_guard<std::mutex> lock(mtx);

                virtRows = term.rows;
                virtCols = term.cols;

                lp.virtRows = virtRows;
                rp.virtRows = virtRows;

                lp.col = 0;
                rp.col = virtCols - 1;

                ball.updateBaseSpeed(virtCols);
            }

            ren.fixedPaddles = cfg.fixedPaddles;

            char c = 0;
            while (read(STDIN_FILENO, &c, 1) > 0) {
                std::lock_guard<std::mutex> lock(mtx);
                if (c == 'x') g_running.store(false, std::memory_order_relaxed);
                if (c == ' ') paused = !paused;

                if (!paused) {
                    if (!lp.isAuto) {
                        if (c == 'a') lp.moveUp();
                        if (c == 'q') lp.moveDown();
                    }
                    if (!rp.isAuto) {
                        if (c == 'p') rp.moveUp();
                        if (c == 'm') rp.moveDown();
                    }
                }
            }

            if (!g_running.load(std::memory_order_relaxed)) break;

            Ball ballCopy;
            Paddle lpCopy(virtRows, 1), rpCopy(virtRows, virtCols - 1);
            int sLCopy, sRCopy;
            bool pausedCopy;

            {
                std::lock_guard<std::mutex> lock(mtx);
                ballCopy = ball;
                lpCopy = lp;
                rpCopy = rp;
                sLCopy = sL;
                sRCopy = sR;
                pausedCopy = paused;
            }

            ren.draw(ballCopy, lpCopy, rpCopy,
                     sLCopy, sRCopy,
                     virtRows, virtCols,
                     term.rows, term.cols,
                     pausedCopy, false, false, false);

            std::this_thread::sleep_until(next);
            next += frameTime;
        }

        physicsRunning = false;
        if (physThread.joinable()) physThread.join();

        term.restore();
    }
};

/* ============================================================
 *  GameMaster
 * ============================================================ */

struct GameMaster {
    Term term;
    Ball ball{term.rows, term.cols};
    Paddle lp{term.rows, 1}, rp{term.rows, term.cols - 1};
    Renderer ren;
    int sL = 0, sR = 0;
    bool paused = false;

    SharedState *shared = nullptr;
    GameConfig cfg;

    int virtRows = 0;
    int virtCols = 0;

    std::mutex mtx;
    bool physicsRunning = false;

    void applyConfig() {
        ren.fixedPaddles = cfg.fixedPaddles;

        if (cfg.bothAI) {
            lp.isAuto = true;
            rp.isAuto = true;
        } else if (cfg.useAuto) {
            if (cfg.autoLeft)  lp.isAuto = true;
            if (cfg.autoRight) rp.isAuto = true;
        }

        if (cfg.fixedPaddles) {
            lp.height = DEFAULT_PADDLE_HEIGHT;
            rp.height = DEFAULT_PADDLE_HEIGHT;
        }
    }

    void updateSharedStateLocked() {
        pthread_mutex_lock(&shared->lock);

        shared->virt_rows = virtRows;
        shared->virt_cols = virtCols;

        shared->ball_x = ball.x;
        shared->ball_y = ball.y;

        shared->lp_y = lp.y;
        shared->rp_y = rp.y;

        shared->lp_col = lp.col;
        shared->rp_col = rp.col;

        shared->lp_height = lp.height;
        shared->rp_height = rp.height;

        shared->scoreL = sL;
        shared->scoreR = sR;
        shared->paused = paused;

        shared->lp_isAI = lp.isAuto;
        shared->rp_isAI = rp.isAuto;

        pthread_mutex_unlock(&shared->lock);
    }

    void handleSlaveInputLocked() {
        pthread_mutex_lock(&shared->lock);
        bool up = shared->slave_up;
        bool down = shared->slave_down;
        shared->slave_up = false;
        shared->slave_down = false;

        bool slaveHasControl = false;
        if (cfg.masterSide == Side::LEFT) {
            if (!rp.isAuto) slaveHasControl = true;
            if (slaveHasControl) {
                if (up)   rp.moveUp();
                if (down) rp.moveDown();
            }
        } else {
            if (!lp.isAuto) slaveHasControl = true;
            if (slaveHasControl) {
                if (up)   lp.moveUp();
                if (down) lp.moveDown();
            }
        }
        pthread_mutex_unlock(&shared->lock);
    }

    void physicsLoop() {
        auto frameTime = microseconds(1000000 / PHYSICS_HZ);
        auto next = steady_clock::now() + frameTime;

        while (g_running.load(std::memory_order_relaxed) && physicsRunning) {
            {
                std::lock_guard<std::mutex> lock(mtx);

                if (!paused) {
                    if (lp.isAuto) lp.aiPredict(ball.y);
                    if (rp.isAuto) rp.aiPredict(ball.y);

                    handleSlaveInputLocked();

                    lp.updatePhysics();
                    rp.updatePhysics();

                    int ev = ball.update(virtRows, virtCols, lp, rp);
                    if (ev == 1 || ev == 2) {
                        Beeper::beep();
                    } else if (ev >= 3) {
                        if (ev == 3) sR++;
                        else         sL++;
                        ball.reset(virtRows, virtCols);
                        Beeper::scoreSound();
                    }
                }

                updateSharedStateLocked();
            }

            std::this_thread::sleep_until(next);
            next += frameTime;
        }
    }

    void run() {
        term.init();
        std::signal(SIGINT, onSigInt);

        term.updateSize();
        int master_rows = term.rows;
        int master_cols = term.cols;

        int slave_rows = master_rows;
        int slave_cols = master_cols;

        pthread_mutex_lock(&shared->lock);
        bool slaveAliveInit = shared->slave_alive;
        if (slaveAliveInit) {
            slave_rows = shared->slave_rows;
            slave_cols = shared->slave_cols;
        }
        pthread_mutex_unlock(&shared->lock);

        virtRows = std::max(master_rows, slave_rows);
        virtCols = std::max(master_cols, slave_cols);

        pthread_mutex_lock(&shared->lock);
        shared->virt_rows = virtRows;
        shared->virt_cols = virtCols;
        pthread_mutex_unlock(&shared->lock);

        {
            std::lock_guard<std::mutex> lock(mtx);
            ball = Ball(virtRows, virtCols);
            lp = Paddle(virtRows, 0);
            rp = Paddle(virtRows, virtCols - 1);
            applyConfig();

            if (!cfg.fixedPaddles) {
                lp.height = DEFAULT_PADDLE_HEIGHT;
                rp.height = DEFAULT_PADDLE_HEIGHT;
            }

            sL = sR = 0;
            paused = false;
        }

        pthread_mutex_lock(&shared->lock);
        shared->master_alive = true;
        shared->master_is_master = true;
        shared->master_side = cfg.masterSide;
        pthread_mutex_unlock(&shared->lock);

        physicsRunning = true;
        std::thread physThread(&GameMaster::physicsLoop, this);

        auto frameTime = microseconds(1000000 / TARGET_FPS);
        auto next = steady_clock::now() + frameTime;

        while (g_running.load(std::memory_order_relaxed)) {
            term.updateSize();
            master_rows = term.rows;
            master_cols = term.cols;

            SharedState snap;
            pthread_mutex_lock(&shared->lock);
            snap = *shared;
            pthread_mutex_unlock(&shared->lock);

            bool slaveConnected = snap.slave_alive;
            slave_rows = slaveConnected ? snap.slave_rows : master_rows;
            slave_cols = slaveConnected ? snap.slave_cols : master_cols;

            int newVirtRows = std::max(master_rows, slave_rows);
            int newVirtCols = std::max(master_cols, slave_cols);

            if (newVirtRows != virtRows || newVirtCols != virtCols) {
                std::lock_guard<std::mutex> lock(mtx);
                virtRows = newVirtRows;
                virtCols = newVirtCols;

                lp.virtRows = virtRows;
                rp.virtRows = virtRows;

                lp.col = 0;
                rp.col = virtCols - 1;

                ball.updateBaseSpeed(virtCols);

                if (!cfg.fixedPaddles) {
                    lp.height = DEFAULT_PADDLE_HEIGHT;
                    rp.height = DEFAULT_PADDLE_HEIGHT;
                }
            }

            ren.fixedPaddles = cfg.fixedPaddles;

            char c = 0;
            while (read(STDIN_FILENO, &c, 1) > 0) {
                std::lock_guard<std::mutex> lock(mtx);
                if (c == 'x') g_running.store(false, std::memory_order_relaxed);
                if (c == ' ') paused = !paused;

                if (!paused) {
                    if (!slaveConnected) {
                        if (!lp.isAuto) {
                            if (c == 'a') lp.moveUp();
                            if (c == 'q') lp.moveDown();
                        }
                        if (!rp.isAuto) {
                            if (c == 'p') rp.moveUp();
                            if (c == 'm') rp.moveDown();
                        }
                    } else {
                        if (cfg.masterSide == Side::LEFT && !lp.isAuto) {
                            if (c == 'a') lp.moveUp();
                            if (c == 'q') lp.moveDown();
                        }
                        if (cfg.masterSide == Side::RIGHT && !rp.isAuto) {
                            if (c == 'p') rp.moveUp();
                            if (c == 'm') rp.moveDown();
                        }
                    }
                }
            }

            if (!g_running.load(std::memory_order_relaxed)) break;

            Ball ballCopy;
            Paddle lpCopy(virtRows, 1), rpCopy(virtRows, virtCols - 1);
            int sLCopy, sRCopy;
            bool pausedCopy;
            bool slaveHasControl = false;

            {
                std::lock_guard<std::mutex> lock(mtx);
                ballCopy = ball;
                lpCopy = lp;
                rpCopy = rp;
                sLCopy = sL;
                sRCopy = sR;
                pausedCopy = paused;

                if (cfg.masterSide == Side::LEFT && !rp.isAuto)
                    slaveHasControl = true;
                if (cfg.masterSide == Side::RIGHT && !lp.isAuto)
                    slaveHasControl = true;
            }

            ren.draw(ballCopy, lpCopy, rpCopy,
                     sLCopy, sRCopy,
                     virtRows, virtCols,
                     term.rows, term.cols,
                     pausedCopy, true, false, slaveHasControl);

            std::this_thread::sleep_until(next);
            next += frameTime;
        }

        physicsRunning = false;
        if (physThread.joinable()) physThread.join();

        pthread_mutex_lock(&shared->lock);
        shared->master_alive = false;
        pthread_mutex_unlock(&shared->lock);

        term.restore();
    }
};

/* ============================================================
 *  GameSlave
 * ============================================================ */

struct GameSlave {
    Term term;
    Renderer ren;
    SharedState *shared = nullptr;

    void run() {
        term.init();
        std::signal(SIGINT, onSigInt);

        pthread_mutex_lock(&shared->lock);
        shared->slave_alive = true;
        shared->slave_connected = true;
        pthread_mutex_unlock(&shared->lock);

        auto frameTime = microseconds(1000000 / TARGET_FPS);
        auto next = steady_clock::now() + frameTime;

        while (g_running.load(std::memory_order_relaxed)) {
            SharedState snap;

            pthread_mutex_lock(&shared->lock);
            bool masterAlive = shared->master_alive;
            pthread_mutex_unlock(&shared->lock);

            if (!masterAlive) {
                term.restore();
                std::cout << "[INFO] Le maître s’est arrêté.\n";
                return;
            }

            term.updateSize();

            pthread_mutex_lock(&shared->lock);
            shared->slave_rows = term.rows;
            shared->slave_cols = term.cols;
            snap = *shared;
            pthread_mutex_unlock(&shared->lock);

            char c = 0;
            bool up = false, down = false;
            while (read(STDIN_FILENO, &c, 1) > 0) {
                if (c == 'x') g_running.store(false, std::memory_order_relaxed);
                if (c == 'a' || c == 'p') up = true;
                if (c == 'q' || c == 'm') down = true;
            }

            bool slaveHasControl = false;
            if (snap.master_side == Side::LEFT) {
                if (!snap.rp_isAI) slaveHasControl = true;
            } else {
                if (!snap.lp_isAI) slaveHasControl = true;
            }

            if (slaveHasControl) {
                pthread_mutex_lock(&shared->lock);
                if (up)   shared->slave_up = true;
                if (down) shared->slave_down = true;
                pthread_mutex_unlock(&shared->lock);
            }

            if (!g_running.load(std::memory_order_relaxed)) break;

            int virtRows = snap.virt_rows;
            int virtCols = snap.virt_cols;

            Ball ball;
            ball.x = snap.ball_x;
            ball.y = snap.ball_y;

            Paddle lp(virtRows, snap.lp_col);
            Paddle rp(virtRows, snap.rp_col);

            lp.y = snap.lp_y;
            rp.y = snap.rp_y;

            lp.height = snap.lp_height;
            rp.height = snap.rp_height;

            lp.isAuto = snap.lp_isAI;
            rp.isAuto = snap.rp_isAI;

            int sL = snap.scoreL;
            int sR = snap.scoreR;
            bool paused = snap.paused;

            ren.fixedPaddles = false;

            ren.draw(ball, lp, rp,
                     sL, sR,
                     virtRows, virtCols,
                     term.rows, term.cols,
                     paused, false, true, slaveHasControl);

            std::this_thread::sleep_until(next);
            next += frameTime;
        }

        pthread_mutex_lock(&shared->lock);
        shared->slave_alive = false;
        shared->slave_connected = false;
        pthread_mutex_unlock(&shared->lock);

        term.restore();
    }
};

/* ============================================================
 *  main()
 * ============================================================ */

int main(int argc, char **argv) {
    GameConfig cfg;
    std::string autoSide = "";
    bool bothAuto = false;
    bool masterRequested = false;
    Side masterSide = Side::LEFT;
    bool autoPresent = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-q" || arg == "--quiet") {
            g_quiet = true;
            continue;
        }

        if (arg == "-f" || arg == "--fixed-paddles") {
            cfg.fixedPaddles = true;
            continue;
        }

        if (arg == "-b" || arg == "--both") {
            bothAuto = true;
        } else if (arg == "-a" || arg == "--auto") {
            autoPresent = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                autoSide = argv[i + 1];
                ++i;
            } else {
                autoSide = "";
            }
        } else if (arg == "-m" || arg == "--master") {
            masterRequested = true;
            std::string side =
                (i + 1 < argc && argv[i + 1][0] != '-') ? argv[i + 1] : "left";
            if (side == "right") masterSide = Side::RIGHT;
            else                 masterSide = Side::LEFT;
        }
    }

    cfg.bothAI = bothAuto;
    cfg.isMaster = masterRequested;
    cfg.masterSide = masterSide;
    cfg.useAuto = false;
    cfg.autoLeft = false;
    cfg.autoRight = false;

    if (masterRequested) {
        if (bothAuto) {
            cfg.autoLeft = true;
            cfg.autoRight = true;
            cfg.useAuto = true;
        } else if (autoPresent) {
            cfg.useAuto = true;
            if (autoSide.empty()) {
                if (masterSide == Side::LEFT) cfg.autoLeft = true;
                else                          cfg.autoRight = true;
            } else if (autoSide == "left")  cfg.autoLeft = true;
            else if (autoSide == "right")   cfg.autoRight = true;
        }
    } else {
        if (bothAuto) {
            cfg.autoLeft = true;
            cfg.autoRight = true;
            cfg.useAuto = true;
        } else if (autoPresent) {
            cfg.useAuto = true;
            if (autoSide.empty() || autoSide == "left")
                cfg.autoLeft = true;
            else if (autoSide == "right")
                cfg.autoRight = true;
        }
    }

    /* ========================= Mode ESCLAVE ========================= */
    if (!cfg.isMaster) {
        int fd = shm_open(SHM_NAME, O_RDWR, 0600);
        if (fd < 0) {
            GameSolo solo;
            solo.run(cfg);
            return 0;
        }

        void *addr = mmap(nullptr, sizeof(SharedState),
                          PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (addr == MAP_FAILED) {
            std::cerr << "[ERROR] mmap esclave : " << strerror(errno) << "\n";
            close(fd);
            return 1;
        }

        SharedState *shared = (SharedState *)addr;

        pthread_mutex_lock(&shared->lock);
        bool masterAlive    = shared->master_alive;
        bool slaveConnected = shared->slave_connected;
        pthread_mutex_unlock(&shared->lock);

        if (!masterAlive) {
            munmap(addr, sizeof(SharedState));
            close(fd);
            GameSolo solo;
            solo.run(cfg);
            return 0;
        }

        if (slaveConnected) {
            std::cerr << "[ERROR] Un maître et un esclave sont déjà actifs.\n";
            munmap(addr, sizeof(SharedState));
            close(fd);
            return 1;
        }

        GameSlave slave;
        slave.shared = shared;
        slave.ren.fixedPaddles = cfg.fixedPaddles;
        slave.run();

        munmap(addr, sizeof(SharedState));
        close(fd);
        return 0;
    }

    /* ========================= Mode MAÎTRE ========================= */

    // Nettoyage best-effort du segment précédent
    shm_unlink(SHM_NAME);

    int fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0600);
    if (fd < 0) {
        std::cerr << "[ERROR] shm_open maître : " << strerror(errno) << "\n";
        return 1;
    }

    if (ftruncate(fd, sizeof(SharedState)) != 0) {
        std::cerr << "[ERROR] ftruncate : " << strerror(errno) << "\n";
        close(fd);
        shm_unlink(SHM_NAME);
        return 1;
    }

    void *addr = mmap(nullptr, sizeof(SharedState),
                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        std::cerr << "[ERROR] mmap maître : " << strerror(errno) << "\n";
        close(fd);
        shm_unlink(SHM_NAME);
        return 1;
    }

    SharedState *shared = (SharedState *)addr;

    if (!init_shared_mutex(&shared->lock)) {
        std::cerr << "[ERROR] init_shared_mutex\n";
        munmap(addr, sizeof(SharedState));
        close(fd);
        shm_unlink(SHM_NAME);
        return 1;
    }

    pthread_mutex_lock(&shared->lock);
    shared->master_alive    = true;
    shared->slave_alive     = false;
    shared->slave_connected = false;
    pthread_mutex_unlock(&shared->lock);

    GameMaster master;
    master.shared = shared;
    master.cfg = cfg;
    master.ren.fixedPaddles = cfg.fixedPaddles;
    master.run();

    munmap(addr, sizeof(SharedState));
    close(fd);
    shm_unlink(SHM_NAME);

    return 0;
}

