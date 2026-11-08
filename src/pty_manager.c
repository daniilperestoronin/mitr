/**
 * pty_manager.c - PTY Manager Implementation (POSIX)
 *
 * Implements pseudo-terminal management for Unix-like systems.
 * Uses POSIX PTY APIs: posix_openpt, grantpt, unlockpt, etc.
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* ptsname_r (Linux glibc) */
#endif
#define _XOPEN_SOURCE 700 /* posix_openpt, grantpt, unlockpt */
#define _DEFAULT_SOURCE   /* ECHOCTL, ECHOKE, dprintf (Linux glibc) */
#ifdef __APPLE__
#define _DARWIN_C_SOURCE /* BSD termios flags + dprintf (macOS) */
#endif

#include "pty_manager.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#ifdef __APPLE__
#include <util.h>
#elif defined(__linux__)
#include <pty.h>
#endif

/* ========================================================================
 * Internal Structure
 * ======================================================================== */

struct PtyManager {
  int master_fd;
  pid_t child_pid;
  bool initialized;

  size_t rows;
  size_t cols;

  char *out_buf;
  size_t out_capacity;
  size_t out_head;
  size_t out_count;
  bool write_failed;
};

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

/**
 * Set file descriptor to non-blocking mode.
 * macOS PTY masters often reject fcntl(O_NONBLOCK) with ENOTTY; fall back
 * to ioctl(FIONBIO), which is the portable PTY path there.
 */
static bool set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags != -1) {
    if ((flags & O_NONBLOCK) != 0) {
      return true;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0) {
      return true;
    }
    log_debug("fcntl F_SETFL O_NONBLOCK failed: %s — trying FIONBIO",
              strerror(errno));
  }

  int on = 1;
  if (ioctl(fd, FIONBIO, &on) == 0) {
    return true;
  }

  log_warn("Failed to enable non-blocking PTY I/O: %s", strerror(errno));
  return false;
}

/** Bytes of free space in the output ring buffer. */
static size_t out_free(const PtyManager *pty) {
  return pty->out_capacity - pty->out_count;
}

/**
 * Append up to len bytes to the ring buffer.
 * @return Number of bytes actually queued
 */
static size_t out_push(PtyManager *pty, const char *data, size_t len) {
  size_t free_space = out_free(pty);
  size_t n = (len < free_space) ? len : free_space;
  size_t tail = (pty->out_head + pty->out_count) % pty->out_capacity;

  if (n == 0) {
    return 0;
  }

  size_t first = pty->out_capacity - tail;
  if (first > n) {
    first = n;
  }
  memcpy(pty->out_buf + tail, data, first);

  if (n > first) {
    memcpy(pty->out_buf, data + first, n - first);
  }

  pty->out_count += n;
  return n;
}

/**
 * Write to the master fd with EINTR retry.
 * @return bytes written, 0 on EAGAIN, -1 on hard error
 */
static ssize_t pty_write_fd(PtyManager *pty, const char *data, size_t len) {
  ssize_t n;

  do {
    n = write(pty->master_fd, data, len);
  } while (n < 0 && errno == EINTR);

  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    }
    log_warn("PTY write failed: %s", strerror(errno));
    pty->write_failed = true;
    return -1;
  }

  return n;
}

/**
 * Drain as much of the output queue as the kernel will accept.
 * Never blocks.
 */
static ssize_t pty_flush_queue(PtyManager *pty) {
  ssize_t total = 0;

  while (pty->out_count > 0) {
    size_t contiguous = pty->out_capacity - pty->out_head;
    if (contiguous > pty->out_count) {
      contiguous = pty->out_count;
    }

    ssize_t n = pty_write_fd(pty, pty->out_buf + pty->out_head, contiguous);
    if (n < 0) {
      return -1;
    }
    if (n == 0) {
      return total; /* EAGAIN */
    }

    pty->out_head = (pty->out_head + (size_t)n) % pty->out_capacity;
    pty->out_count -= (size_t)n;
    total += n;
  }

  /* Reset head when empty to keep pushes contiguous more often */
  if (pty->out_count == 0) {
    pty->out_head = 0;
  }

  return total;
}

/**
 * Read once from the master fd with EINTR retry.
 * @return bytes read, 0 on EAGAIN/EOF-wait, -1 on error
 */
static ssize_t pty_read_once(PtyManager *pty, char *buffer, size_t size) {
  ssize_t bytes;

  do {
    bytes = read(pty->master_fd, buffer, size);
  } while (bytes < 0 && errno == EINTR);

  if (bytes < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    }
    return -1;
  }

  return bytes;
}

/**
 * Setup terminal attributes for the slave PTY.
 * Uses standard cooked-mode defaults so the shell gets proper
 * line editing, echo, signal handling, and \n → \r\n translation.
 */
static void setup_terminal(int fd) {
  struct termios tios;

  if (tcgetattr(fd, &tios) < 0) {
    log_warn("tcgetattr failed: %s", strerror(errno));
    return;
  }

  /* Portable cooked-mode baseline; optional flags gated for non-BSD Unix */
  tios.c_iflag = ICRNL | IXON | IXANY;
#ifdef IMAXBEL
  tios.c_iflag |= IMAXBEL;
#endif
#ifdef IUTF8
  tios.c_iflag |= IUTF8;
#endif
  tios.c_oflag = OPOST | ONLCR;
  tios.c_lflag = ECHO | ECHOE | ECHOK | ICANON | ISIG | IEXTEN;
#ifdef ECHOCTL
  tios.c_lflag |= ECHOCTL;
#endif
#ifdef ECHOKE
  tios.c_lflag |= ECHOKE;
#endif
  tios.c_cflag &= (unsigned int)~(CSIZE | PARENB);
  tios.c_cflag |= CS8 | CREAD;

  tios.c_cc[VMIN] = 1;
  tios.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSANOW, &tios) < 0) {
    log_warn("tcsetattr failed: %s", strerror(errno));
  }
}

/**
 * True if path contains a ".." component (not merely the substring).
 */
static bool path_has_dotdot_component(const char *path) {
  const char *p = path;

  while (*p) {
    if (p[0] == '.' && p[1] == '.' && (p[2] == '\0' || p[2] == '/') &&
        (p == path || p[-1] == '/')) {
      return true;
    }
    p++;
  }
  return false;
}

bool pty_manager_shell_path_valid(const char *path) {
  struct stat st;

  if (!path || path[0] != '/' || path_has_dotdot_component(path)) {
    return false;
  }

  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
    return false;
  }

  if ((st.st_mode & S_IWOTH) != 0) {
    return false;
  }

  if (access(path, X_OK) != 0) {
    return false;
  }

  return true;
}

static const char *resolve_shell_path(const char *shell_path) {
  if (shell_path && shell_path[0] != '\0') {
    return shell_path;
  }

  const char *env = getenv("SHELL");
  if (env && env[0] != '\0') {
    return env;
  }

  return "/bin/sh";
}

#ifdef MITR_PLATFORM_MACOS
/*
 * Launch Services starts a GUI app with cwd "/". A directory the user
 * already chose (for example ./build/mitr from a project) stays put.
 * $HOME must be absolute and a directory; otherwise the child remains
 * at "/".
 */
static void macos_chdir_home_if_launchd_root(const char *home) {
  char cwd[PATH_MAX];
  struct stat st;

  if (!home || home[0] != '/') {
    return;
  }
  if (!getcwd(cwd, sizeof(cwd)) || strcmp(cwd, "/") != 0) {
    return;
  }
  if (stat(home, &st) != 0 || !S_ISDIR(st.st_mode)) {
    return;
  }
  (void)chdir(home);
}

/* "-" plus the basename ("-zsh", "-sh") is what marks a login shell. */
static bool macos_login_argv0(const char *shell, char *out, size_t out_size) {
  const char *slash = strrchr(shell, '/');
  const char *base = (slash && slash[1] != '\0') ? slash + 1 : nullptr;
  int n;

  if (!base || out_size < 3) {
    return false;
  }
  n = snprintf(out, out_size, "-%s", base);
  return n > 1 && (size_t)n < out_size;
}
#endif

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

static void pty_manager_free(PtyManager *pty) {
  if (!pty) {
    return;
  }
  if (pty->master_fd >= 0) {
    close(pty->master_fd);
    pty->master_fd = -1;
  }
  SAFE_FREE(pty->out_buf);
  free(pty);
}

PtyManager *pty_manager_init(size_t rows, size_t cols, const char *shell_path,
                             const char *term_env) {
  log_trace("Initializing PTY manager");

  const char *shell = resolve_shell_path(shell_path);
  if (!pty_manager_shell_path_valid(shell)) {
    log_error("Refusing to exec unsafe shell path: '%s'", shell);
    return nullptr;
  }

  PtyManager *pty = checked_calloc(1, sizeof(PtyManager));
  if (!pty) {
    return nullptr;
  }

  const char *term = (term_env && term_env[0]) ? term_env : "xterm-256color";

  pty->rows = rows;
  pty->cols = cols;
  pty->master_fd = -1;
  pty->child_pid = -1;
  pty->out_capacity = PTY_OUT_QUEUE_CAPACITY;
  pty->out_buf = checked_malloc(pty->out_capacity);
  if (!pty->out_buf) {
    free(pty);
    return nullptr;
  }

  /* Open PTY master; request O_CLOEXEC and O_NONBLOCK when supported */
  pty->master_fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (pty->master_fd < 0) {
    /* Some platforms reject O_NONBLOCK in posix_openpt — retry without it */
    pty->master_fd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  }
  if (pty->master_fd < 0) {
    log_warn("Failed to open PTY master");
    pty_manager_free(pty);
    return nullptr;
  }

  if (fcntl(pty->master_fd, F_SETFD, FD_CLOEXEC) < 0) {
    log_warn("Failed to set FD_CLOEXEC on PTY master");
    pty_manager_free(pty);
    return nullptr;
  }

  /* Grant access to slave */
  if (grantpt(pty->master_fd) < 0) {
    log_warn("grantpt failed");
    pty_manager_free(pty);
    return nullptr;
  }

  /* Unlock slave */
  if (unlockpt(pty->master_fd) < 0) {
    log_warn("unlockpt failed");
    pty_manager_free(pty);
    return nullptr;
  }

  /* Get slave name (thread-safe; ptsname() uses a static buffer) */
  char slave_name[PATH_MAX];
  if (ptsname_r(pty->master_fd, slave_name, sizeof(slave_name)) != 0) {
    log_warn("ptsname_r failed");
    pty_manager_free(pty);
    return nullptr;
  }

  /* Non-blocking I/O is required for the output queue design */
  if (!set_nonblocking(pty->master_fd)) {
    log_warn("Failed to set PTY master non-blocking");
    pty_manager_free(pty);
    return nullptr;
  }

  /* Fork child process */
  pty->child_pid = fork();

  if (pty->child_pid < 0) {
    log_warn("Fork failed");
    pty_manager_free(pty);
    return nullptr;
  }

  if (pty->child_pid == 0) {
    /* Child process - use _exit() not exit() to avoid running parent's
     * atexit handlers (SDL, GPU drivers, etc.) which can crash in fork context
     */

    /* Create new session */
    if (setsid() < 0) {
      _exit(1);
    }

    /* Open slave PTY */
    int slave_fd = open(slave_name, O_RDWR);
    if (slave_fd < 0) {
      _exit(1);
    }

    /* Acquire controlling terminal explicitly (not all kernels
     * do this automatically on open after setsid) */
    if (ioctl(slave_fd, TIOCSCTTY, 0) < 0) {
      /* Non-fatal on some platforms; continue */
    }

    /* Setup terminal */
    setup_terminal(slave_fd);

    /* Set window size */
    struct winsize ws = {.ws_row = (unsigned short)rows,
                         .ws_col = (unsigned short)cols,
                         .ws_xpixel = 0,
                         .ws_ypixel = 0};
    if (ioctl(slave_fd, TIOCSWINSZ, &ws) < 0) {
      dprintf(STDERR_FILENO, "mitr: TIOCSWINSZ: %s\r\n", strerror(errno));
      _exit(1);
    }

    /* Redirect stdin, stdout, stderr to slave */
    if (dup2(slave_fd, STDIN_FILENO) < 0 || dup2(slave_fd, STDOUT_FILENO) < 0 ||
        dup2(slave_fd, STDERR_FILENO) < 0) {
      _exit(1);
    }

    /* Close every FD above stderr.  The parent had SDL, X11, DRM,
     * GPU driver handles open — the child must not inherit them. */
    long max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd < 0 || max_fd > 65536)
      max_fd = 1024;
    for (int fd = STDERR_FILENO + 1; fd < (int)max_fd; fd++) {
      close(fd);
    }

    /* Execute the path validated before fork. On macOS argv[0] is
     * "-<basename>" so the shell is a login shell (/etc/zprofile and
     * ~/.zprofile, including path_helper). Elsewhere argv[0] is the
     * real path and profile files are not sourced. */
#ifdef MITR_PLATFORM_MACOS
    char argv0_buf[NAME_MAX + 2];
    const char *argv0 = shell;
    if (macos_login_argv0(shell, argv0_buf, sizeof(argv0_buf))) {
      argv0 = argv0_buf;
    }
#else
    const char *argv0 = shell;
#endif
    char *const argv[] = {(char *)argv0, nullptr};

    /* Get current locale or use UTF-8 default */
    const char *lang = getenv("LANG");
    if (!lang || strstr(lang, "UTF-8") == nullptr) {
      lang = "en_US.UTF-8"; /* Default to UTF-8 */
    }

    char lang_buf[128];
    char lc_all_buf[128];
    char lc_ctype_buf[128];
    char path_buf[1024];
    char home_buf[256];
    char user_buf[128];
    char shell_buf[256];
    char term_buf[128];

    if (snprintf(lang_buf, sizeof(lang_buf), "LANG=%s", lang) < 0 ||
        snprintf(lc_all_buf, sizeof(lc_all_buf), "LC_ALL=%s", lang) < 0 ||
        snprintf(lc_ctype_buf, sizeof(lc_ctype_buf), "LC_CTYPE=%s", lang) < 0 ||
        snprintf(term_buf, sizeof(term_buf), "TERM=%s", term) < 0) {
      _exit(1);
    }

    /* Get important environment variables from parent */
    const char *path = getenv("PATH");
    const char *home = getenv("HOME");
    const char *user = getenv("USER");
    const char *shell_env = getenv("SHELL");

#ifdef MITR_PLATFORM_MACOS
    /* Use the real HOME only. The "/tmp" fallback below is for the
     * environment block, not a directory to enter. */
    macos_chdir_home_if_launchd_root(home);
#endif

    /* Set defaults if not found */
    if (!path)
      path = "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";
    if (!home)
      home = "/tmp";
    if (!user)
      user = "user";
    if (!shell_env)
      shell_env = shell;

    if (snprintf(path_buf, sizeof(path_buf), "PATH=%s", path) < 0 ||
        snprintf(home_buf, sizeof(home_buf), "HOME=%s", home) < 0 ||
        snprintf(user_buf, sizeof(user_buf), "USER=%s", user) < 0 ||
        snprintf(shell_buf, sizeof(shell_buf), "SHELL=%s", shell_env) < 0) {
      _exit(1);
    }

    char ssh_buf[512];
    char display_buf[256];
    const char *ssh_auth = getenv("SSH_AUTH_SOCK");
    const char *display = getenv("DISPLAY");
    bool have_ssh =
        ssh_auth && ssh_auth[0] &&
        snprintf(ssh_buf, sizeof(ssh_buf), "SSH_AUTH_SOCK=%s", ssh_auth) > 0;
    bool have_display =
        display && display[0] &&
        snprintf(display_buf, sizeof(display_buf), "DISPLAY=%s", display) > 0;

    /* Whitelist only: TERM, locale, PATH, HOME, USER, SHELL, plus
     * SSH_AUTH_SOCK / DISPLAY when present so ssh-agent and X11 forwarding
     * keep working. Other parent env is not inherited. */
    char *envp[16];
    size_t ei = 0;
    envp[ei++] = term_buf;
    envp[ei++] = "COLORTERM=truecolor";
    envp[ei++] = "PROMPT_EOL_MARK=";
    envp[ei++] = path_buf;
    envp[ei++] = home_buf;
    envp[ei++] = user_buf;
    envp[ei++] = shell_buf;
    envp[ei++] = lang_buf;
    envp[ei++] = lc_all_buf;
    envp[ei++] = lc_ctype_buf;
    if (have_ssh)
      envp[ei++] = ssh_buf;
    if (have_display)
      envp[ei++] = display_buf;
    envp[ei] = nullptr;

    execve(shell, argv, envp);

    /* execve failed — write diagnostic to stderr (slave PTY) so the
     * parent can read it from the master side for debugging. */
    dprintf(STDERR_FILENO, "mitr: execve(%s): %s\r\n", shell, strerror(errno));
    _exit(127);
  }

  /* Parent process */
  pty->initialized = true;

  log_debug("PTY manager initialized: master_fd=%d, child_pid=%d",
            pty->master_fd, pty->child_pid);
  log_debug("Shell started: %zux%zu", rows, cols);

  return pty;
}

void pty_manager_destroy(PtyManager *pty) {
  if (!pty) {
    return;
  }

  log_trace("Destroying PTY manager");

  /* Best-effort flush before teardown (non-blocking) */
  if (pty->master_fd >= 0 && pty->out_count > 0) {
    (void)pty_flush_queue(pty);
  }

  /* Close master first — this sends SIGHUP to the child's session,
   * which is the normal way a terminal emulator signals "window closed". */
  if (pty->master_fd >= 0) {
    close(pty->master_fd);
    pty->master_fd = -1;
  }

  /* Reap child if not already reaped by pty_manager_is_alive */
  if (pty->child_pid > 0) {
    int status;
    for (int i = 0; i < 10; i++) {
      pid_t r = waitpid(pty->child_pid, &status, WNOHANG);
      if (r != 0)
        break;
      usleep(10000); /* 10ms */
    }

    /* Force kill if still alive */
    if (waitpid(pty->child_pid, &status, WNOHANG) == 0) {
      kill(pty->child_pid, SIGKILL);
      waitpid(pty->child_pid, &status, 0);
    }
  }

  SAFE_FREE(pty->out_buf);
  free(pty);
  log_debug("PTY manager destroyed");
}

ssize_t pty_manager_flush(PtyManager *pty) {
  if (!pty || pty->master_fd < 0) {
    return -1;
  }
  if (pty->write_failed) {
    return -1;
  }
  if (pty->out_count == 0) {
    return 0;
  }
  return pty_flush_queue(pty);
}

bool pty_manager_has_pending_write(const PtyManager *pty) {
  return pty && pty->out_count > 0;
}

ssize_t pty_manager_read(PtyManager *pty, char *buffer, size_t size) {
  if (!pty || !buffer || size == 0 || pty->master_fd < 0) {
    return -1;
  }

  /* Opportunistic flush before waiting */
  if (pty->out_count > 0) {
    if (pty_flush_queue(pty) < 0) {
      return -1;
    }
  }

  struct pollfd pfd = {
      .fd = pty->master_fd,
      .events = POLLIN,
  };
  if (pty->out_count > 0) {
    pfd.events |= POLLOUT;
  }

  int result;
  do {
    result = poll(&pfd, 1, 10); /* 10 ms */
  } while (result < 0 && errno == EINTR);

  if (result < 0) {
    return -1;
  }

  if (result == 0) {
    return 0;
  }

  if (pfd.revents & (POLLOUT | POLLERR | POLLHUP)) {
    if (pty->out_count > 0 && pty_flush_queue(pty) < 0) {
      return -1;
    }
  }

  if (pfd.revents & POLLIN) {
    return pty_read_once(pty, buffer, size);
  }

  return 0;
}

ssize_t pty_manager_read_available(PtyManager *pty, char *buffer, size_t size) {
  if (!pty || !buffer || size == 0 || pty->master_fd < 0) {
    return -1;
  }

  if (pty->out_count > 0) {
    if (pty_flush_queue(pty) < 0) {
      return -1;
    }
  }

  return pty_read_once(pty, buffer, size);
}

ssize_t pty_manager_write(PtyManager *pty, const char *data, size_t size) {
  if (!pty || !data || size == 0 || pty->master_fd < 0) {
    return -1;
  }
  if (pty->write_failed) {
    return -1;
  }

  /* Drain any backlog first so new data can go straight to the fd */
  if (pty->out_count > 0) {
    if (pty_flush_queue(pty) < 0) {
      return -1;
    }
  }

  size_t accepted = 0;

  /* Fast path: queue empty — write directly to the kernel buffer */
  if (pty->out_count == 0) {
    while (accepted < size) {
      ssize_t n = pty_write_fd(pty, data + accepted, size - accepted);
      if (n < 0) {
        return -1;
      }
      if (n == 0) {
        break; /* EAGAIN — fall through to queue */
      }
      accepted += (size_t)n;
    }
  }

  /* Queue whatever remains (may be partial if the ring is full) */
  if (accepted < size) {
    accepted += out_push(pty, data + accepted, size - accepted);
  }

  return (ssize_t)accepted;
}

ssize_t pty_manager_write_all(PtyManager *pty, const char *data, size_t size) {
  if (!pty || !data || size == 0 || pty->master_fd < 0) {
    return -1;
  }
  if (pty->write_failed) {
    return -1;
  }

  size_t off = 0;
  int idle_polls = 0;

  while (off < size) {
    ssize_t n = pty_manager_write(pty, data + off, size - off);
    if (n < 0) {
      return -1;
    }
    if (n > 0) {
      off += (size_t)n;
      idle_polls = 0;
      continue;
    }

    /* Queue full — wait for POLLOUT, then flush */
    struct pollfd pfd = {
        .fd = pty->master_fd,
        .events = POLLOUT,
    };
    int result;
    do {
      result = poll(&pfd, 1, 50);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
      return -1;
    }
    if (result == 0) {
      idle_polls++;
      if (idle_polls >= 40) {
        log_warn("PTY write_all timed out after %zu of %zu bytes", off, size);
        break;
      }
      continue;
    }

    if (pty_flush_queue(pty) < 0) {
      return -1;
    }
  }

  return (ssize_t)off;
}

MitrError pty_manager_resize(PtyManager *pty, size_t rows, size_t cols) {
  if (!pty || !mitr_grid_dims_valid(rows, cols)) {
    return MITR_ERROR_INVALID_PARAM;
  }

  /* Update dimensions first */
  pty->rows = rows;
  pty->cols = cols;

  struct winsize ws = {.ws_row = (unsigned short)rows,
                       .ws_col = (unsigned short)cols,
                       .ws_xpixel = 0,
                       .ws_ypixel = 0};

  if (ioctl(pty->master_fd, TIOCSWINSZ, &ws) < 0) {
    log_debug("Failed to send TIOCSWINSZ (not critical)");
    /* Continue anyway - dimensions are updated in our struct */
  }

  log_debug("PTY resized to %zux%zu", rows, cols);
  return MITR_OK;
}

bool pty_manager_is_alive(PtyManager *pty) {
  if (!pty || pty->child_pid <= 0) {
    return false;
  }

  int status;
  pid_t result = waitpid(pty->child_pid, &status, WNOHANG);

  if (result == 0) {
    return true;
  }

  if (result < 0) {
    log_debug("waitpid(%d) failed: errno=%d (%s)", pty->child_pid, errno,
              strerror(errno));
    pty->child_pid = -1;
    return false;
  }

  /* Child has exited — log why for diagnostics */
  if (WIFEXITED(status)) {
    log_debug("Child exited with status %d", WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    log_debug("Child killed by signal %d", WTERMSIG(status));
  }
  pty->child_pid = -1;
  return false;
}

bool pty_manager_get_initialized(const PtyManager *pty) {
  return pty && pty->initialized;
}

int pty_manager_get_master_fd(const PtyManager *pty) {
  return pty ? pty->master_fd : -1;
}

pid_t pty_manager_get_child_pid(const PtyManager *pty) {
  return pty ? pty->child_pid : -1;
}

size_t pty_manager_get_rows(const PtyManager *pty) {
  return pty ? pty->rows : 0;
}

size_t pty_manager_get_cols(const PtyManager *pty) {
  return pty ? pty->cols : 0;
}

void pty_manager_get_dimensions(const PtyManager *pty, size_t *rows,
                                size_t *cols) {
  if (!pty)
    return;
  if (rows)
    *rows = pty->rows;
  if (cols)
    *cols = pty->cols;
}
