#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <sys/wait.h>
#include "session.h"
#include "exec.h"

static void session_setup_user_env(const char *username, struct passwd *pw,
                                   const struct session_opts *opts, int vt_nr)
{
    setenv("HOME", pw->pw_dir, 1);
    setenv("USER", username, 1);
    setenv("LOGNAME", username, 1);
    setenv("SHELL", pw->pw_shell, 1);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_SEAT", "seat0", 1);

    char vtn[16];
    snprintf(vtn, sizeof(vtn), "%d", vt_nr > 0 ? vt_nr : 1);
    setenv("XDG_VTNR", vtn, 1);

    if (opts->desktop_name && opts->desktop_name[0])
        setenv("XDG_CURRENT_DESKTOP", opts->desktop_name, 1);

    char rt[64];
    snprintf(rt, sizeof(rt), "/run/user/%d", pw->pw_uid);
    setenv("XDG_RUNTIME_DIR", rt, 0);

    if (opts->cursor_theme && opts->cursor_theme[0])
        setenv("XCURSOR_THEME", opts->cursor_theme, 1);
    if (opts->cursor_size > 0) {
        char sz[16];
        snprintf(sz, sizeof(sz), "%d", opts->cursor_size);
        setenv("XCURSOR_SIZE", sz, 1);
    }

    if (opts->pam_env) {
        for (int i = 0; opts->pam_env[i]; i++)
            putenv(opts->pam_env[i]);
    }

    if (!getenv("DBUS_SESSION_BUS_ADDRESS")) {
        char dbus[128];
        snprintf(dbus, sizeof(dbus), "unix:path=/run/user/%d/bus", pw->pw_uid);
        setenv("DBUS_SESSION_BUS_ADDRESS", dbus, 1);
    }
}

void session_launch(const char *username, const char *cmd,
                    const struct session_opts *opts)
{
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        slimm_log("session: getpwnam(%s) failed", username);
        _exit(1);
    }

    char cmdbuf[256];
    strncpy(cmdbuf, cmd, sizeof(cmdbuf) - 1);
    cmdbuf[sizeof(cmdbuf) - 1] = '\0';

    slimm_log("session: pid %d exec '%s' as %s", getpid(), cmdbuf, username);

    /*
     * No fork, no reaper: the greeter process *becomes* the compositor. systemd
     * is the supervisor — slimm.service is Restart=always, so when the compositor
     * exits (logout) systemd starts a fresh greeter. Benefits over the old reaper:
     *   - zero lingering slimm process during the session (the point of slimm);
     *   - exec closes slimm's O_CLOEXEC DRM/input fds, so no leftover DRM client
     *     can stall the compositor's first modeset (the NVIDIA freeze).
     */
    if (setsid() < 0)
        slimm_log("session: setsid failed: %s", strerror(errno));
    signal(SIGHUP, SIG_IGN);

    if (initgroups(username, pw->pw_gid) < 0 ||
        setgid(pw->pw_gid) < 0 ||
        setuid(pw->pw_uid) < 0) {
        slimm_log("session: drop privileges failed: %s", strerror(errno));
        _exit(1);
    }

    if (chdir(pw->pw_dir) < 0) {
        int rc = chdir("/");
        (void)rc;
    }

    signal(SIGINT, SIG_DFL);
    signal(SIGUSR1, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);

    session_setup_user_env(username, pw, opts, opts->vt_nr);

    /* Guarantee no greeter fd (DRM/render/input/epoll/VT) survives into the
     * compositor, regardless of O_CLOEXEC. stdio (tty/journal) stays. */
    exec_close_inherited_fds();

    if (exec_try_command(cmdbuf) < 0)
        execl("/bin/sh", "sh", "-c", cmdbuf, (char *)NULL);

    /* Only reached if exec failed — exit non-zero so systemd respawns the greeter. */
    slimm_log("session: exec '%s' failed: %s", cmdbuf, strerror(errno));
    _exit(127);
}
