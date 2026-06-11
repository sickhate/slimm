#ifndef SLIMM_EXEC_H
#define SLIMM_EXEC_H

void exec_sanitize_desktop(char *s);
int  exec_build_argv(char *cmd, char **argv, int max);
int  exec_try_command(char *cmd);

/* Close every fd >= 3 (DRM, render, EVIOCGRAB'd input, epoll, …) before the
 * greeter exec()s the compositor, so no slimm handle survives into the session
 * — a leftover DRM client stalls the compositor's modeset (frozen display,
 * esp. on NVIDIA). */
void exec_close_inherited_fds(void);

/* stderr + syslog(LOG_DAEMON, "slimm") — reaper logs after unit deactivates */
void slimm_log(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#endif
