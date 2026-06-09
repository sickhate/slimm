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
        fprintf(stderr, "session: getpwnam(%s) failed\n", username);
        _exit(1);
    }

    char cmdbuf[256];
    strncpy(cmdbuf, cmd, sizeof(cmdbuf) - 1);
    cmdbuf[sizeof(cmdbuf) - 1] = '\0';

    /* Root reaper: one blocked waitpid (~minimal RSS), exec slimm on compositor exit */
    pid_t pid = fork();
    if (pid < 0)
        _exit(1);

    if (pid == 0) {
        if (setsid() < 0)
            fprintf(stderr, "session: setsid failed\n");

        if (initgroups(username, pw->pw_gid) < 0)
            _exit(1);
        if (setgid(pw->pw_gid) < 0)
            _exit(1);
        if (setuid(pw->pw_uid) < 0)
            _exit(1);

        if (chdir(pw->pw_dir) < 0) {
            int rc = chdir("/");
            (void)rc;
        }

        signal(SIGINT, SIG_DFL);
        signal(SIGUSR1, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);

        session_setup_user_env(username, pw, opts, opts->vt_nr);

        if (exec_try_command(cmdbuf) < 0) {
            fprintf(stderr, "session: exec '%s' failed: %s\n",
                    cmdbuf, strerror(errno));
            if (execl("/bin/sh", "sh", "-c", cmdbuf, (char *)NULL) < 0)
                fprintf(stderr, "session: sh -c failed: %s\n", strerror(errno));
        }

        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status))
        fprintf(stderr, "session: compositor exited with status %d\n",
                WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        fprintf(stderr, "session: compositor killed by signal %d\n",
                WTERMSIG(status));
    fflush(stderr);
    exec_relaunch_slimm();
}
