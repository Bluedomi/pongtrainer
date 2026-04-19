/*
 Auteur : Dominique (aka /b4sh)
 Version: 3.2 (Maître/Esclave + espace virtuel + projection proportionnelle +
 option -q)

 Compilation :
    g++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -o pongtrainer pongtrainer.cpp && strip pongtrainer
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

using namespace std::chrono;

// Framerate cible
static constexpr int TARGET_FPS = 30; // 60 ou 30 suffisant

// Physique des raquettes
static constexpr float PADDLE_THRUST = 0.9f;
static constexpr float PADDLE_FRICTION = 0.60f;
static constexpr int DEFAULT_PADDLE_HEIGHT = 11; // 9, 7, 5

// ========================= PHYSIQUE =========================
//
// Fréquence de simulation (physique), indépendante du rendu
// static constexpr int PHYSICS_HZ = 30;     // 30 ou 60 Hz conseillé
// static constexpr float DT = 1.0f / PHYSICS_HZ;
//
// Coefficients de vitesse de la balle
// static constexpr float BALL_SPEED_FACTOR = 2.0f;     // 1.0 = normal, 1.5 =
// rapide static constexpr float BALL_VERTICAL_RATIO = 0.3f;   // proportion de
// vitesse verticale

// ========================= PHYSIQUE =========================

// Fréquence de simulation (physique)
static constexpr int PHYSICS_HZ = 60;
static constexpr float DT = 1.0f / PHYSICS_HZ;

// Vitesse de base de la balle
static constexpr float BALL_SPEED_FACTOR = 1.0f;

// Vitesse verticale initiale (proportion)
static constexpr float BALL_VERTICAL_RATIO = 0.3f;

// Accélération progressive après chaque rebond
static constexpr float BALL_ACCEL_PER_RALLY = 1.05f; // +5% par rebond

// Vitesse maximale (pour éviter les traversées)
static constexpr float BALL_MAX_SPEED = 3.5f * PHYSICS_HZ;

// Angle maximal du rebond (en proportion de la vitesse)
static constexpr float BALL_MAX_ANGLE =
    0.75f; // 0.75 = 75% de la vitesse en vertical

// ========================= TTY / Rendu =========================

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
    std::cout << "\033[?25l\033[2J" << std::flush;
  }
  void restore() const {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig);
    std::cout << "\033[?25h\033[2J\033[H" << std::flush;
  }
};

// ========================= Beeper (option -q) =========================

static bool g_quiet = false;

struct Beeper {
  static void beep() {
    if (g_quiet)
      return;
    [[maybe_unused]] ssize_t n = write(STDOUT_FILENO, "\a", 1);
  }
  static void scoreSound() {
    if (g_quiet)
      return;
    beep();
    std::this_thread::sleep_for(milliseconds(100));
    beep();
  }
};

// ========================= Physique (espace virtuel) =========================

struct Paddle {
  float y;
  float velocity = 0.0f;
  int height = DEFAULT_PADDLE_HEIGHT;
  int col;
  int virtRows;
  bool isAuto = false;

  static constexpr float FRICTION = 0.60f;
  static constexpr float THRUST = 0.9f;

  Paddle(int vrows = 24, int column = 1)
      : y(1.0f), col(column), virtRows(vrows) {}

  void updateLayout(int newVirtRows, int newCol) {
    virtRows = newVirtRows;
    col = newCol;
    clampPos();
  }
  void clampPos() {
    if (y < 1.0f)
      y = 1.0f;
    float maxY = (float)virtRows - height;
    if (y > maxY)
      y = maxY;
  }
  void moveUp() { velocity -= THRUST; }
  void moveDown() { velocity += THRUST; }
  void updatePhysics() {
    y += velocity;
    velocity *= FRICTION;
    clampPos();
  }
  void aiPredict(float ballY) {
    if (!isAuto)
      return;
    float center = y + (height / 2.0f);
    if (ballY < center - 0.5f)
      moveUp();
    else if (ballY > center + 0.5f)
      moveDown();
  }
  bool hits(int bx, int by) const {
    int py = (int)roundf(y);
    return (bx == col && by >= py && by < py + height);
  }
};

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
    baseSpeed = (static_cast<float>(vcols) / 250.0f) *
                PHYSICS_HZ           // compensation framerate
                * BALL_SPEED_FACTOR; // coefficient optionnel

    if (baseSpeed < 0.15f)
      baseSpeed = 0.15f;
  }

  void reset(int vrows, int vcols) {
    x = prevX = vcols / 2.0f;
    y = vrows / 2.0f;
    rallyCount = 0;

    // vx dépend du côté précédent
    vx = (vx > 0) ? -baseSpeed : baseSpeed;

    // vitesse verticale initiale
    vy = baseSpeed * BALL_VERTICAL_RATIO;
  }

  int update(int vrows, int vcols, const Paddle &lp, const Paddle &rp) {
    prevX = x;

    // --- PHYSIQUE TIME-BASED ---
    x += vx * DT;
    y += vy * DT;

    // --- COLLISION HAUT/BAS ---
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

    // --- COLLISION CONTINUE RAQUETTE DROITE ---
    if (vx > 0 && prevX < rp.col && x >= rp.col) {
      int iy = (int)roundf(y);
      if (rp.hits(rp.col, iy)) {

        // Position relative du point d'impact (0 = haut, 1 = bas)
        float rel = (y - rp.y) / rp.height;
        rel = std::clamp(rel, 0.0f, 1.0f);

        // Converti en angle vertical (-1 à +1)
        float angle = (rel - 0.5f) * 2.0f;

        // Nouvelle vitesse verticale
        vy = angle * BALL_MAX_ANGLE * std::abs(vx);

        // Inversion horizontale
        vx = -std::abs(vx);

        // Accélération progressive
        vx *= BALL_ACCEL_PER_RALLY;
        vy *= BALL_ACCEL_PER_RALLY;

        // Clamp vitesse max
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

    // --- COLLISION CONTINUE RAQUETTE GAUCHE ---
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

    // --- SORTIE DU TERRAIN ---
    if (x <= 0)
      return 3;
    if (x >= vcols)
      return 4;

    return 0;
  }
};

// ========================= Renderer avec projection =========================

struct Renderer {
  std::string buf;

  static int mapX(float vx, int virtCols, int screenCols) {
    float fx = vx / (float)(virtCols - 1);
    fx = std::clamp(fx, 0.0f, 1.0f);
    return (int)std::round(fx * (screenCols - 1));
  }

  static int mapY(float vy, int virtRows, int screenRows) {
    float fy = (vy - 1.0f) / (float)(virtRows - 2);
    fy = std::clamp(fy, 0.0f, 1.0f);
    return std::clamp(1 + (int)std::round(fy * (screenRows - 3)), 1,
                      screenRows - 2);
  }

  static int mapHeight(int virtHeight, int virtRows, int screenRows) {
    float ratio = (float)virtHeight / (float)(virtRows - 2);
    int sh = (int)std::round(ratio * (screenRows - 2));
    return std::clamp(sh, 1, screenRows - 2);
  }

  void draw(const Ball &ball, const Paddle &lp, const Paddle &rp, int sL,
            int sR, int virtRows, int virtCols, int screenRows, int screenCols,
            bool paused, bool isMaster, bool isSlave, bool slaveHasControl) {

    if (screenRows < 4 || screenCols < 20) {
      std::cout << "\033[H\033[2JTerminal trop petit.\n";
      std::cout.flush();
      return;
    }

    buf = "\033[H\033[7m";
    std::string bar(screenCols, ' ');
    std::string info = paused ? " [ PAUSE ] " : "PONG v3.2 | [x] Quitter";

    auto copy = [&](int p, const std::string &s) {
      for (int i = 0; i < (int)s.size() && p + i < screenCols; ++i)
        bar[p + i] = s[i];
    };

    copy(2, "P1: " + std::to_string(sL) + (lp.isAuto ? " (AI)" : ""));
    copy((screenCols - (int)info.size()) / 2, info);
    copy(screenCols - 15,
         "P2: " + std::to_string(sR) + (rp.isAuto ? " (AI)" : ""));
    buf += bar + "\033[0m\n";

    int bx = mapX(ball.x, virtCols, screenCols);
    int by = mapY(ball.y, virtRows, screenRows);

    int lpCol = mapX(lp.col, virtCols, screenCols);
    int rpCol = mapX(rp.col, virtCols, screenCols);
    // DEBUG: TEST forcing temporaire
    // rpCol = std::min(rpCol + 10, screenCols - 1);

    int lpY = mapY(lp.y, virtRows, screenRows);
    int rpY = mapY(rp.y, virtRows, screenRows);

    int lpH = mapHeight(lp.height, virtRows, screenRows);
    int rpH = mapHeight(rp.height, virtRows, screenRows);

    for (int r = 1; r < screenRows - 1; ++r) {
      for (int c = 0; c < screenCols; ++c) {
        if (c == lpCol && r >= lpY && r < lpY + lpH)
          buf += '#';
        else if (c == rpCol && r >= rpY && r < rpY + rpH)
          buf += '#'; // décommenté

        else if (c == bx && r == by) {
          //          buf += 'O';
          buf += "😎"; // colonne 1
          //          buf += ' ';    // colonne 2 (remplie explicitement)
          c++; // on saute la colonne suivante (la boucle fera le reste)
               //}

          continue;
        } else if (c == screenCols / 2 && r % 2 == 0)
          buf += ':';
        else
          buf += ' ';
      }
      buf += '\n';
    }

    buf += "\033[0m";
    if (isMaster && !isSlave)
      buf += "[MASTER]";
    else if (isSlave && slaveHasControl)
      buf += "[SLAVE: joueur]";
    else if (isSlave && !slaveHasControl)
      buf += "[SLAVE: spectateur]";
    else
      buf += " ";

    std::cout.write(buf.data(), buf.size());
    std::cout.flush();
  }
};

// ========================= Signal global =========================

static volatile bool g_running = true;
void onSigInt(int) { g_running = false; }

// ========================= Mémoire partagée =========================

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
  if (pthread_mutexattr_init(&attr) != 0)
    return false;
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

// ========================= Modes de fonctionnement =========================

struct GameConfig {
  bool bothAI = false;
  bool autoLeft = false;
  bool autoRight = false;
  bool useAuto = false;
  bool isMaster = false;
  Side masterSide = Side::LEFT;
};

// ========================= Boucle SOLO =========================

struct GameSolo {
  Term term;
  Ball ball{term.rows, term.cols};
  Paddle lp{term.rows, 1}, rp{term.rows, term.cols - 1};
  Renderer ren;
  int sL = 0, sR = 0;
  bool paused = false;

  void applyConfig(const GameConfig &cfg) {
    if (cfg.bothAI) {
      lp.isAuto = true;
      rp.isAuto = true;
    } else if (cfg.useAuto) {
      if (cfg.autoLeft)
        lp.isAuto = true;
      if (cfg.autoRight)
        rp.isAuto = true;
    }
  }

  void run(const GameConfig &cfg) {
    term.init();
    std::signal(SIGINT, onSigInt);

    int virtRows = term.rows;
    int virtCols = term.cols;

    ball = Ball(virtRows, virtCols);
    lp = Paddle(virtRows, 1);
    rp = Paddle(virtRows, virtCols - 1);
    applyConfig(cfg);

    auto frameTime = microseconds(1000000 / TARGET_FPS);
    auto next = steady_clock::now() + frameTime;

    while (g_running) {
      term.updateSize();

      char c = 0;
      while (read(STDIN_FILENO, &c, 1) > 0) {
        if (c == 'x')
          g_running = false;
        if (c == ' ')
          paused = !paused;
        if (!paused) {
          if (!lp.isAuto) {
            if (c == 'a')
              lp.moveUp();
            if (c == 'q')
              lp.moveDown();
          }
          if (!rp.isAuto) {
            if (c == 'p')
              rp.moveUp();
            if (c == 'm')
              rp.moveDown();
          }
        }
      }

      if (!paused && g_running) {
        if (lp.isAuto)
          lp.aiPredict(ball.y);
        if (rp.isAuto)
          rp.aiPredict(ball.y);

        lp.updatePhysics();
        rp.updatePhysics();

        int ev = ball.update(virtRows, virtCols, lp, rp);
        if (ev == 1 || ev == 2)
          Beeper::beep();
        else if (ev >= 3) {
          if (ev == 3)
            sR++;
          else
            sL++;
          ball.reset(virtRows, virtCols);
          ren.draw(ball, lp, rp, sL, sR, virtRows, virtCols, term.rows,
                   term.cols, paused, false, false, false);
          Beeper::scoreSound();
          std::this_thread::sleep_for(milliseconds(400));
          next = steady_clock::now() + frameTime;
        }
      }

      if (g_running) {
        ren.draw(ball, lp, rp, sL, sR, virtRows, virtCols, term.rows, term.cols,
                 paused, false, false, false);
        std::this_thread::sleep_until(next);
        next += frameTime;
      }
    }
    term.restore();
  }
};

// ========================= Boucle MAÎTRE =========================

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

  void applyConfig() {
    if (cfg.bothAI) {
      lp.isAuto = true;
      rp.isAuto = true;
    } else if (cfg.useAuto) {
      if (cfg.autoLeft)
        lp.isAuto = true;
      if (cfg.autoRight)
        rp.isAuto = true;
    }
  }

  void updateSharedState() {
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

  void handleSlaveInput() {
    pthread_mutex_lock(&shared->lock);
    bool up = shared->slave_up;
    bool down = shared->slave_down;
    shared->slave_up = false;
    shared->slave_down = false;

    bool slaveHasControl = false;
    if (cfg.masterSide == Side::LEFT) {
      if (!rp.isAuto)
        slaveHasControl = true;
      if (slaveHasControl) {
        if (up)
          rp.moveUp();
        if (down)
          rp.moveDown();
      }
    } else {
      if (!lp.isAuto)
        slaveHasControl = true;
      if (slaveHasControl) {
        if (up)
          lp.moveUp();
        if (down)
          lp.moveDown();
      }
    }
    pthread_mutex_unlock(&shared->lock);
  }

  void run() {
    term.init();
    std::signal(SIGINT, onSigInt);

    virtRows = term.rows;
    virtCols = term.cols;

    ball = Ball(virtRows, virtCols);
    lp = Paddle(virtRows, 1);
    rp = Paddle(virtRows, virtCols - 1);
    applyConfig();

    pthread_mutex_lock(&shared->lock);
    shared->master_alive = true;
    shared->master_is_master = true;
    shared->master_side = cfg.masterSide;
    pthread_mutex_unlock(&shared->lock);

    auto frameTime = microseconds(1000000 / PHYSICS_HZ);

    auto next = steady_clock::now() + frameTime;

    while (g_running) {
      term.updateSize();

      char c = 0;
      while (read(STDIN_FILENO, &c, 1) > 0) {
        if (c == 'x')
          g_running = false;
        if (c == ' ')
          paused = !paused;

        if (!paused) {
          if (cfg.masterSide == Side::LEFT && !lp.isAuto) {
            if (c == 'a')
              lp.moveUp();
            if (c == 'q')
              lp.moveDown();
          }
          if (cfg.masterSide == Side::RIGHT && !rp.isAuto) {
            if (c == 'p')
              rp.moveUp();
            if (c == 'm')
              rp.moveDown();
          }
        }
      }

      pthread_mutex_lock(&shared->lock);
      bool slaveConnected = shared->slave_connected;
      pthread_mutex_unlock(&shared->lock);

      if (!slaveConnected && !paused) {
        if (cfg.masterSide == Side::LEFT) {
          if (!rp.isAuto) {
            if (c == 'p')
              rp.moveUp();
            if (c == 'm')
              rp.moveDown();
          }
        } else {
          if (!lp.isAuto) {
            if (c == 'a')
              lp.moveUp();
            if (c == 'q')
              lp.moveDown();
          }
        }
      }

      if (!paused && g_running) {
        if (lp.isAuto)
          lp.aiPredict(ball.y);
        if (rp.isAuto)
          rp.aiPredict(ball.y);

        handleSlaveInput();

        lp.updatePhysics();
        rp.updatePhysics();

        int ev = ball.update(virtRows, virtCols, lp, rp);
        if (ev == 1 || ev == 2)
          Beeper::beep();
        else if (ev >= 3) {
          if (ev == 3)
            sR++;
          else
            sL++;
          ball.reset(virtRows, virtCols);
          updateSharedState();
          ren.draw(ball, lp, rp, sL, sR, virtRows, virtCols, term.rows,
                   term.cols, paused, true, false, false);
          Beeper::scoreSound();
          std::this_thread::sleep_for(milliseconds(400));
          next = steady_clock::now() + frameTime;
        }
      }

      if (g_running) {
        updateSharedState();

        bool slaveHasControl = false;
        if (cfg.masterSide == Side::LEFT && !rp.isAuto)
          slaveHasControl = true;
        if (cfg.masterSide == Side::RIGHT && !lp.isAuto)
          slaveHasControl = true;

        ren.draw(ball, lp, rp, sL, sR, virtRows, virtCols, term.rows, term.cols,
                 paused, true, false, slaveHasControl);

        std::this_thread::sleep_until(next);
        next += frameTime;
      }
    }

    pthread_mutex_lock(&shared->lock);
    shared->master_alive = false;
    pthread_mutex_unlock(&shared->lock);

    term.restore();
  }
};

// ========================= Boucle ESCLAVE =========================

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

    auto frameTime = microseconds(1000000 / PHYSICS_HZ);
    auto next = steady_clock::now() + frameTime;

    while (g_running) {
      pthread_mutex_lock(&shared->lock);
      bool masterAlive = shared->master_alive;
      pthread_mutex_unlock(&shared->lock);

      if (!masterAlive) {
        term.restore();
        std::cout << "[INFO] Le maître s’est arrêté.\n";
        return;
      }

      term.updateSize();

      char c = 0;
      bool up = false, down = false;
      while (read(STDIN_FILENO, &c, 1) > 0) {
        if (c == 'x')
          g_running = false;
        if (c == 'a' || c == 'p')
          up = true;
        if (c == 'q' || c == 'm')
          down = true;
      }

      pthread_mutex_lock(&shared->lock);

      bool slaveHasControl = false;
      if (shared->master_side == Side::LEFT) {
        if (!shared->rp_isAI)
          slaveHasControl = true;
      } else {
        if (!shared->lp_isAI)
          slaveHasControl = true;
      }

      if (slaveHasControl) {
        if (up)
          shared->slave_up = true;
        if (down)
          shared->slave_down = true;
      }

      int virtRows = shared->virt_rows;
      int virtCols = shared->virt_cols;

      Ball ball;
      ball.x = shared->ball_x;
      ball.y = shared->ball_y;

      Paddle lp(virtRows, shared->lp_col);
      Paddle rp(virtRows, shared->rp_col);

      lp.y = shared->lp_y;
      rp.y = shared->rp_y;

      lp.height = shared->lp_height;
      rp.height = shared->rp_height;

      lp.isAuto = shared->lp_isAI;
      rp.isAuto = shared->rp_isAI;

      int sL = shared->scoreL;
      int sR = shared->scoreR;
      bool paused = shared->paused;

      pthread_mutex_unlock(&shared->lock);

      if (!g_running)
        break;

      ren.draw(ball, lp, rp, sL, sR, virtRows, virtCols, term.rows, term.cols,
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

// ========================= main =========================

int main(int argc, char **argv) {
  GameConfig cfg;
  std::string autoSide = "";
  bool bothAuto = false;
  bool masterRequested = false;
  Side masterSide = Side::LEFT;
  bool autoPresent = false; // Indique si l'option -a a été rencontrée

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-q" || arg == "--quiet") {
      g_quiet = true;
      continue;
    }

    if (arg == "-b" || arg == "--both") {
      bothAuto = true;
    } else if (arg == "-a" || arg == "--auto") {
      autoPresent = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        autoSide = argv[i + 1];
        ++i; // consommer l'argument suivant
      } else {
        autoSide = ""; // -a seul, pas de côté précisé
      }
    } else if (arg == "-m" || arg == "--master") {
      masterRequested = true;
      std::string side =
          (i + 1 < argc && argv[i + 1][0] != '-') ? argv[i + 1] : "left";
      if (side == "right")
        masterSide = Side::RIGHT;
      else
        masterSide = Side::LEFT;
    }
  }

  // Construction de la configuration
  cfg.bothAI = bothAuto;
  cfg.isMaster = masterRequested;
  cfg.masterSide = masterSide;
  cfg.useAuto = false;
  cfg.autoLeft = false;
  cfg.autoRight = false;

  if (masterRequested) {
    // Mode maître
    if (bothAuto) {
      cfg.autoLeft = true;
      cfg.autoRight = true;
      cfg.useAuto = true; // ← indispensable
    } else if (autoPresent) {
      cfg.useAuto = true; // ← indispensable
      if (autoSide.empty()) {
        // -a seul : IA sur le côté du maître
        if (masterSide == Side::LEFT)
          cfg.autoLeft = true;
        else
          cfg.autoRight = true;
      } else if (autoSide == "left") {
        cfg.autoLeft = true;
      } else if (autoSide == "right") {
        cfg.autoRight = true;
      }
      // Si autoSide est autre chose, on ignore (pas d'IA)
    }
    // Si ni bothAuto ni autoPresent, aucune IA
  } else {
    // Mode solo (inchangé)
    if (bothAuto) {
      cfg.autoLeft = true;
      cfg.autoRight = true;
      cfg.useAuto = true;
    } else if (autoPresent) {
      cfg.useAuto = true;
      if (autoSide.empty() || autoSide == "left") {
        cfg.autoLeft = true;
      } else if (autoSide == "right") {
        cfg.autoRight = true;
      }
    }
  }

  // Mode esclave (si pas maître)
  if (!cfg.isMaster) {
    int fd = shm_open(SHM_NAME, O_RDWR, 0600);
    if (fd < 0) {
      GameSolo solo;
      solo.run(cfg);
      return 0;
    }
    void *addr = mmap(nullptr, sizeof(SharedState), PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
      std::cerr << "[ERROR] mmap esclave : " << strerror(errno) << "\n";
      close(fd);
      return 1;
    }
    SharedState *shared = (SharedState *)addr;

    pthread_mutex_lock(&shared->lock);
    bool masterAlive = shared->master_alive;
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
    slave.run();

    munmap(addr, sizeof(SharedState));
    close(fd);
    return 0;
  }

  // Mode maître (suite)
  int fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0600);
  if (fd < 0) {
    std::cerr << "[ERROR] shm_open maître : " << strerror(errno) << "\n";
    return 1;
  }
  if (ftruncate(fd, sizeof(SharedState)) != 0) {
    std::cerr << "[ERROR] ftruncate : " << strerror(errno) << "\n";
    close(fd);
    return 1;
  }
  void *addr = mmap(nullptr, sizeof(SharedState), PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    std::cerr << "[ERROR] mmap maître : " << strerror(errno) << "\n";
    close(fd);
    return 1;
  }
  SharedState *shared = (SharedState *)addr;

  static bool mutexInitialized = false;
  if (!mutexInitialized) {
    init_shared_mutex(&shared->lock);
    mutexInitialized = true;
  }

  pthread_mutex_lock(&shared->lock);
  shared->master_alive = true;
  shared->slave_alive = false;
  shared->slave_connected = false;
  shared->master_is_master = true;
  shared->master_side = cfg.masterSide;
  shared->lp_isAI = cfg.autoLeft;
  shared->rp_isAI = cfg.autoRight;
  shared->slave_up = false;
  shared->slave_down = false;
  pthread_mutex_unlock(&shared->lock);

  GameMaster master;
  master.shared = shared;
  master.cfg = cfg;
  master.run();

  munmap(addr, sizeof(SharedState));
  close(fd);
  return 0;
}
