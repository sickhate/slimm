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
#include "auth.h"
#include "exec.h"

#define SESSION_WAIT_MS 15000

static void session_free_pam_env(char **pam_env)
{
    if (!pam_env) return;
    for (int i = 0; pam_env[i]; i++)
        free(pam_env[i]);
    free(pam_env);
}

static int session_wait_user_ready(uid_t uid)
{
    char bus[64], priv[72];

    snprintf(bus, sizeof(bus), "/run/user/%u/bus", uid);
    snprintf(priv, sizeof(priv), "/run/user/%u/systemd/private", uid);

    for (int ms = 0; ms < SESSION_WAIT_MS; ms += 100) {
        if (access(bus, F_OK) == 0 && access(priv, F_OK) == 0) {
            fprintf(stderr, "session: user@%u ready (%dms)\n", uid, ms);
            fflush(stderr);
            return 0;
        }
        usleep(100000);
    }

    fprintf(stderr, "session: timed out waiting for user@%u (bus/systemd)\n", uid);
    fflush(stderr);
    return -1;
}

static void session_setup_user_env(const char *username, struct passwd *pw,
                                   const struct session_opts *opts, int vt_nr)
{
    setenv("HOME", pw->pw_dir, 1);
    setenv("USER", username, 1);
    setenv("LOGNAME", username, 1);
    setenv("SHELL", pw->pw_shell, 1);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_SESSION_CLASS", "user", 1);
    setenv("XDG_SEAT", "seat0", 1);

    char vtn[16];
    snprintf(vtn, sizeof(vtn), "%d", vt_nr > 0 ? vt_nr : 1);
    setenv("XDG_VTNR", vtn, 1);

    if (opts->desktop_name && opts->desktop_name[0]) {
        setenv("XDG_CURRENT_DESKTOP", opts->desktop_name, 1);
        setenv("XDG_SESSION_DESKTOP", opts->desktop_name, 1);
    }

    char rt[64];
    snprintf(rt, sizeof(rt), "/run/user/%d", pw->pw_uid);
    setenv("XDG_RUNTIME_DIR", rt, 1);

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

static void session_run_compositor(const char *username, const char *cmd,
                                   const struct session_opts *opts)
{
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        fprintf(stderr, "session: getpwnam(%s) failed\n", username);
        return;
    }

    char cmdbuf[256];
    strncpy(cmdbuf, cmd, sizeof(cmdbuf) - 1);
    cmdbuf[sizeof(cmdbuf) - 1] = '\0';

    pid_t pid = fork();
    if (pid < 0)
        return;

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
}

void session_reaper(const struct session_launch_args *args)
{
    struct slimm_session *sess;

    if (args->password) {
        fprintf(stderr, "slimm: authenticating '%s'...\n", args->username);
        sess = auth_login(args->username, args->password);
    } else {
        sess = auth_autologin(args->username);
    }
    fflush(stderr);

    if (!sess) {
        fprintf(stderr, "slimm: login failed for '%s'\n", args->username);
        fflush(stderr);
        exec_relaunch_slimm();
        _exit(1);
    }

    fprintf(stderr, "slimm: login ok for '%s', launching '%s'\n",
            args->username, args->cmd);
    fflush(stderr);

    struct passwd *pw = getpwnam(args->username);
    if (!pw) {
        auth_close(sess);
        exec_relaunch_slimm();
        _exit(1);
    }

    session_wait_user_ready(pw->pw_uid);

    struct session_opts opts = {
        .desktop_name = args->desktop_name,
        .pam_env = auth_get_env(sess),
        .vt_nr = args->vt_nr,
    };

    session_run_compositor(args->username, args->cmd, &opts);

    session_free_pam_env(opts.pam_env);
    auth_close(sess);
    exec_relaunch_slimm();
    _exit(1);
}
