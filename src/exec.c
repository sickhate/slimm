#define _GNU_SOURCE
#include "exec.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

void exec_relaunch_slimm(void)
{
    char path[256];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n > 0) {
        path[n] = '\0';
        execl(path, "slimm", (char *)NULL);
    }
    execl("/usr/bin/slimm", "slimm", (char *)NULL);
    _exit(1);
}

int exec_try_command(char *cmd)
{
    exec_sanitize_desktop(cmd);

    char *argv[16];
    if (exec_build_argv(cmd, argv, 16) == 0)
        execvp(argv[0], argv);

    return -1;
}
