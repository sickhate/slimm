#define _GNU_SOURCE
#include "vt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <signal.h>
#include <termios.h>

static struct termios vt_saved_termios;
static int vt_termios_saved;

int vt_get_active_nr(int vt_fd)
{
    struct vt_stat vs = {0};
    if (ioctl(vt_fd, VT_GETSTATE, &vs) == 0)
        return (int)vs.v_active;
    return 1;
}

int vt_open(void)
{
    int fd = -1;

    if (isatty(STDIN_FILENO)) {
        char path[64];
        if (ttyname_r(STDIN_FILENO, path, sizeof(path)) == 0)
            fd = open(path, O_RDWR | O_CLOEXEC);
    }
    if (fd < 0)
        fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        fd = open("/dev/tty0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "vt_open: no VT device (%s)\n", strerror(errno));
        return -1;
    }
    return fd;
}

void vt_take_control(int vt_fd)
{
    struct vt_stat vs = {0};
    if (ioctl(vt_fd, VT_GETSTATE, &vs) == 0) {
        ioctl(vt_fd, VT_ACTIVATE, vs.v_active);
        ioctl(vt_fd, VT_WAITACTIVE, vs.v_active);
    }
    signal(SIGUSR1, SIG_IGN);
    ioctl(vt_fd, VT_SETMODE, &(struct vt_mode){
        .mode = VT_PROCESS,
        .waitv = 0,
        .relsig = SIGUSR1,
        .acqsig = SIGUSR1,
        .frsig = 0,
    });

    struct termios t;
    if (tcgetattr(vt_fd, &t) == 0) {
        vt_saved_termios = t;
        vt_termios_saved = 1;
        t.c_lflag &= ~(ECHO | ICANON);
        tcsetattr(vt_fd, TCSANOW, &t);
    }
}

void vt_give_control(int vt_fd)
{
    if (vt_termios_saved) {
        tcsetattr(vt_fd, TCSANOW, &vt_saved_termios);
        vt_termios_saved = 0;
    }
    ioctl(vt_fd, VT_SETMODE, &(struct vt_mode){
        .mode = VT_AUTO,
        .waitv = 0,
    });
}

void vt_set_graphics(int vt_fd)
{
    ioctl(vt_fd, KDSETMODE, KD_GRAPHICS);
}

void vt_set_text(int vt_fd)
{
    ioctl(vt_fd, KDSETMODE, KD_TEXT);
}

void vt_clear(int vt_fd)
{
    write(vt_fd, "\033[2J\033[3J\033[H", 11);
}
