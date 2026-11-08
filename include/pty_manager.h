/**
 * pty_manager.h - PTY (Pseudo-Terminal) Manager
 *
 * Manages pseudo-terminal for spawning and interacting with shell processes.
 * This is the core component that allows mitr to run actual shell commands.
 *
 * POSIX implementation (Unix/Linux/macOS) using posix_openpt, grantpt, etc.
 *
 * Output is non-blocking: writes that would block are queued and flushed
 * when the master fd becomes writable (poll POLLOUT).
 */

#ifndef MITR_PTY_MANAGER_H
#define MITR_PTY_MANAGER_H

#include "common.h"
#include <stdbool.h>
#include <sys/types.h>

/* ========================================================================
 * Types
 * ======================================================================== */

/** Maximum bytes held in the non-blocking output queue. */
#define PTY_OUT_QUEUE_CAPACITY ((size_t)256 * 1024)

/**
 * Opaque PTY manager. Created by pty_manager_init(); valid until
 * pty_manager_destroy(). The output queue and write-failure state are
 * internal — use pty_manager_write(), pty_manager_flush(), and
 * pty_manager_has_pending_write() only.
 */
typedef struct PtyManager PtyManager;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * True if @p path is safe to exec as a shell: absolute, no ".." components,
 * a regular executable file, and not world-writable.
 */
bool pty_manager_shell_path_valid(const char *path);

/**
 * Initialize PTY manager and spawn shell
 *
 * @param rows Terminal rows
 * @param cols Terminal columns
 * @param shell_path Path to shell (nullptr or empty for $SHELL or /bin/sh)
 * @param term_env TERM value (nullptr for "xterm-256color")
 * @return Pointer to PtyManager or nullptr on failure
 *
 * On macOS, argv[0] is "-<basename>" so the shell is a login shell, and a
 * process cwd of "/" (Finder, Dock, Spotlight) makes the child start in
 * $HOME. Any other cwd is kept. Other platforms pass the real path as
 * argv[0] and keep the process cwd.
 */
PtyManager *pty_manager_init(size_t rows, size_t cols, const char *shell_path,
                             const char *term_env);

/**
 * Destroy PTY manager and cleanup
 *
 * @param pty PtyManager to destroy
 */
void pty_manager_destroy(PtyManager *pty);

/**
 * Read data from PTY (waits up to 10ms for readability).
 * Also flushes pending queued output when the fd is writable.
 *
 * @param pty PtyManager
 * @param buffer Buffer to read into
 * @param size Buffer size
 * @return Number of bytes read, -1 on error, 0 if no data
 */
ssize_t pty_manager_read(PtyManager *pty, char *buffer, size_t size);

/**
 * Read data from PTY without waiting (non-blocking check).
 * Flushes pending queued output first.
 *
 * @param pty PtyManager
 * @param buffer Buffer to read into
 * @param size Buffer size
 * @return Number of bytes read, -1 on error, 0 if no data
 */
ssize_t pty_manager_read_available(PtyManager *pty, char *buffer, size_t size);

/**
 * Queue data for the PTY without blocking the caller.
 *
 * Writes immediately when the kernel buffer accepts data; any remainder
 * is stored in an internal ring buffer and flushed later via
 * pty_manager_flush() / poll POLLOUT.
 *
 * @param pty PtyManager
 * @param data Data to write
 * @param size Number of bytes to write
 * @return Number of bytes accepted (written and/or queued), 0 if the queue
 *         is full (backpressure), or -1 on hard I/O error
 */
ssize_t pty_manager_write(PtyManager *pty, const char *data, size_t size);

/**
 * Write all of @p data, flushing and waiting briefly on POLLOUT when the
 * queue is full. Never busy-spins; gives up after ~2s of backpressure.
 *
 * @return Bytes accepted (size on full success), or -1 on hard I/O error
 */
ssize_t pty_manager_write_all(PtyManager *pty, const char *data, size_t size);

/**
 * Attempt to flush pending queued output (never blocks).
 *
 * @param pty PtyManager
 * @return Bytes flushed this call, 0 if nothing pending or EAGAIN, -1 on error
 */
ssize_t pty_manager_flush(PtyManager *pty);

/**
 * @return true if queued output is waiting for the PTY to accept more data
 */
bool pty_manager_has_pending_write(const PtyManager *pty);

/**
 * Resize PTY
 *
 * @param pty PtyManager
 * @param rows New number of rows
 * @param cols New number of columns
 * @return MITR_OK on success, MITR_ERROR_INVALID_PARAM on bad arguments
 */
MitrError pty_manager_resize(PtyManager *pty, size_t rows, size_t cols);

/**
 * Check if child process is still alive
 *
 * @param pty PtyManager
 * @return true if child is alive, false otherwise
 */
bool pty_manager_is_alive(PtyManager *pty);

bool pty_manager_get_initialized(const PtyManager *pty);
int pty_manager_get_master_fd(const PtyManager *pty);
pid_t pty_manager_get_child_pid(const PtyManager *pty);
size_t pty_manager_get_rows(const PtyManager *pty);
size_t pty_manager_get_cols(const PtyManager *pty);
void pty_manager_get_dimensions(const PtyManager *pty, size_t *rows,
                                size_t *cols);

#endif /* MITR_PTY_MANAGER_H */
