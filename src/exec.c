#define _GNU_SOURCE
#include "exec.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <linux/vt.h>

static int slimm_log_open;

void slimm_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    fprintf(stderr, "%s\n", buf);
    fflush(stderr);

    if (!slimm_log_open) {
        openlog("slimm", LOG_PID | LOG_NDELAY, LOG_DAEMON);
        slimm_log_open = 1;
    }
    syslog(LOG_INFO, "%s", buf);
}

void exec_sanitize_desktop(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (r[0] == '%' && r[1]) {
            if (r[1] == '%') {
                *w++ = '%';
                r++;
                continue;
            }
            r++;
            while (*r == ' ' || *r == '\t')
                r++;
            while (*r && *r != ' ' && *r != '\t')
                r++;
            r--;
            continue;
        }
        *w++ = *r;
    }
    *w = '\0';

    while (w > s && (w[-1] == ' ' || w[-1] == '\t'))
        *--w = '\0';
}

int exec_build_argv(char *cmd, char **argv, int max)
{
    int n = 0;
    char *p = cmd;

    while (*p && n < max - 1) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        argv[n++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p)
            *p++ = '\0';
    }
    argv[n] = NULL;
    return n > 0 ? 0 : -1;
}

void exec_close_inherited_fds(void)
{
    for (int fd = 3; fd < 256; fd++)
        (void)close(fd);
}

int exec_try_command(char *cmd)
{
    exec_sanitize_desktop(cmd);

    char *argv[16];
    if (exec_build_argv(cmd, argv, 16) == 0)
        execvp(argv[0], argv);

    return -1;
}
