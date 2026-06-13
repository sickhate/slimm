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
static int vt_saved_kbd_mode = -1;

static void tty_no_echo(int fd)
{
    struct termios t;

    if (fd < 0 || !isatty(fd))
        return;
    if (tcgetattr(fd, &t) != 0)
        return;

    t.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | ICANON);
    t.c_iflag &= ~(ICRNL | IXON);
    tcsetattr(fd, TCSANOW, &t);
}

static void kbd_off_on_fd(int fd)
{
    if (fd < 0)
        return;
    if (ioctl(fd, KDSKBMODE, K_OFF) != 0 && errno != EINVAL)
        fprintf(stderr, "vt: KDSKBMODE K_OFF failed on fd %d: %s\n",
                fd, strerror(errno));
}

static void kbd_save_and_off(int fd)
{
    int mode;

    if (fd < 0)
        return;
    if (ioctl(fd, KDGKBMODE, &mode) == 0) {
        vt_saved_kbd_mode = mode;
        kbd_off_on_fd(fd);
    }
}

int vt_get_active_nr(int vt_fd)
{
    struct vt_stat vs = {0};
    if (ioctl(vt_fd, VT_GETSTATE, &vs) == 0)
        return (int)vs.v_active;
    return 1;
}

void vt_activate(int vt_fd, int nr)
{
    if (vt_fd < 0 || nr < 1)
        return;
    /* Switch the active console to `nr` and wait for the switch to complete.
     * Used on the Escape path to drop onto a getty VT (logind starts the getty
     * on demand when the VT is activated). Best-effort — ignore failures. */
    if (ioctl(vt_fd, VT_ACTIVATE, nr) == 0)
        ioctl(vt_fd, VT_WAITACTIVE, nr);
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

void vt_console_shield(int vt_fd)
{
    char path[32];
    int nr;

    if (vt_fd >= 0) {
        tty_no_echo(vt_fd);
        kbd_off_on_fd(vt_fd);
    }
    if (isatty(STDIN_FILENO))
        tty_no_echo(STDIN_FILENO);

    /* KDSKBMODE on /dev/tty0 applies to the active virtual console */
    int t0 = open("/dev/tty0", O_RDWR | O_CLOEXEC);
    if (t0 >= 0) {
        kbd_off_on_fd(t0);
        close(t0);
    }

    if (vt_fd >= 0) {
        nr = vt_get_active_nr(vt_fd);
        snprintf(path, sizeof(path), "/dev/tty%d", nr);
        int tfd = open(path, O_RDWR | O_CLOEXEC);
        if (tfd >= 0) {
            tty_no_echo(tfd);
            kbd_off_on_fd(tfd);
            close(tfd);
        }
    }
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

    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &vt_saved_termios) == 0)
        vt_termios_saved = 1;
    else if (tcgetattr(vt_fd, &vt_saved_termios) == 0)
        vt_termios_saved = 1;

    {
        int t0 = open("/dev/tty0", O_RDWR | O_CLOEXEC);
        if (t0 >= 0) {
            kbd_save_and_off(t0);
            close(t0);
        } else {
            kbd_save_and_off(vt_fd);
        }
    }
    vt_console_shield(vt_fd);
}

void vt_scrub(int vt_fd)
{
    vt_console_shield(vt_fd);
    vt_clear(vt_fd);
}

void vt_give_control(int vt_fd)
{
    if (vt_saved_kbd_mode >= 0) {
        int t0 = open("/dev/tty0", O_RDWR | O_CLOEXEC);
        if (t0 >= 0) {
            ioctl(t0, KDSKBMODE, vt_saved_kbd_mode);
            close(t0);
        } else if (vt_fd >= 0) {
            ioctl(vt_fd, KDSKBMODE, vt_saved_kbd_mode);
        }
        vt_saved_kbd_mode = -1;
    }
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
    ssize_t n = write(vt_fd, "\033[2J\033[3J\033[H", 11);
    (void)n;
}
