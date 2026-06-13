#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <errno.h>
#include <linux/input.h>

#include <gbm.h>
#include <xf86drm.h>
#include "config.h"
#include "auth.h"
#include "session.h"
#include "vt.h"
#include "drm.h"
#include "renderer.h"
#include "font.h"
#include "input.h"
#include "ui.h"
#include "ste2.h"
#ifndef SLIMM_MINIMAL
#include <wayland-client.h>
#include "theme.h"
#include "wayland.h"
#endif

#define MAX_EVENTS 8

/* Exit status meaning "the user pressed Escape to drop to a console". The
 * service file lists this in RestartPreventExitStatus= so systemd does NOT
 * respawn the greeter — ESC truly stops slimm and leaves the tty free for a
 * console login. Every other exit (logout = compositor exit code, or a crash)
 * still triggers Restart=always. 42 is outside the normal 0/1/127 codes slimm
 * uses and well clear of systemd's own range. */
#define SLIMM_EXIT_CONSOLE 42

static int running = 1;
/* Set to SLIMM_EXIT_CONSOLE by the Escape (UI_QUIT) path; stays 0 for SIGTERM
 * (systemctl stop/restart) so those behave normally. Returned from main(). */
static int exit_code = 0;
static int timer_fd = -1;
static int vt_fd = -1;
static int drm_fd = -1;

struct backend {
    int is_drm;
    union {
        struct slimm_gbm *gbm;
#ifndef SLIMM_MINIMAL
        struct slim_wayland *wl;
#endif
    };
};

static void sigusr1_handler(int sig)
{
    (void)sig;
}

static void sigterm_handler(int sig)
{
    (void)sig;
    running = 0;
}

static void set_cursor_timer(void)
{
    if (timer_fd < 0) return;
    struct itimerspec its = {
        .it_value = { .tv_sec = 0, .tv_nsec = 500000000 },
        .it_interval = { .tv_sec = 0, .tv_nsec = 500000000 },
    };
    timerfd_settime(timer_fd, 0, &its, NULL);
}

static void set_autologin_timer(void)
{
    if (timer_fd < 0) return;
    struct itimerspec its = {
        .it_value = { .tv_sec = 1, .tv_nsec = 0 },
        .it_interval = { .tv_sec = 1, .tv_nsec = 0 },
    };
    timerfd_settime(timer_fd, 0, &its, NULL);
}

static void clear_timer(void)
{
    if (timer_fd < 0) return;
    struct itimerspec its = {0};
    timerfd_settime(timer_fd, 0, &its, NULL);
}

static void do_render(struct slim_renderer *r, struct ui_state *ui,
                      struct backend *backend)
{
    static int last_input_mode = -1;

    if (vt_fd >= 0 && ui->input_mode == INPUT_PASSWORD) {
        if (last_input_mode != INPUT_PASSWORD)
            vt_scrub(vt_fd);
        else
            vt_console_shield(vt_fd);
    }
    last_input_mode = ui->input_mode;

    ui_render(ui, r);
    if (!renderer_swap(r)) return;

    if (backend->is_drm) {
        struct slimm_gbm *gbm = backend->gbm;
        struct gbm_bo *bo = renderer_lock_front(r);
        if (bo) {
            drm_flip(gbm, bo);
            if (gbm->previous_bo)
                gbm_surface_release_buffer(gbm->surface, gbm->previous_bo);
            gbm->previous_bo = bo;
        }
    }
#ifndef SLIMM_MINIMAL
    else {
        wayland_flush(backend->wl);
    }
#endif
}

static void session_pre_fork(struct backend *backend)
{
    if (!backend->is_drm) return;
    drmDropMaster(drm_fd);
    vt_give_control(vt_fd);
    vt_clear(vt_fd);
    vt_set_text(vt_fd);
}

static void session_free_opts(struct session_opts *opts)
{
    if (!opts->pam_env) return;
    for (int i = 0; opts->pam_env[i]; i++)
        free(opts->pam_env[i]);
    free(opts->pam_env);
    opts->pam_env = NULL;
}

static void do_login(struct ui_state *ui, struct backend *backend)
{
    if (ui->username_len <= 0) {
        fprintf(stderr, "slimm: login skipped (empty username)\n");
        fflush(stderr);
        return;
    }

    fprintf(stderr, "slimm: authenticating '%s'...\n", ui->username);
    fflush(stderr);

    struct slimm_session *sess = auth_login(ui->username, ui->password);
    ui->password_len = 0;
    memset(ui->password, 0, sizeof(ui->password));

    if (!sess) {
        fprintf(stderr, "slimm: login failed for '%s'\n", ui->username);
        fflush(stderr);
        snprintf(ui->message, sizeof(ui->message), "Authentication failed");
        ui->input_mode = INPUT_PASSWORD;
        ui->field_dirty = 1;
        return;
    }

    const char *cmd = ui->sessions[ui->selected_session].exec;
    fprintf(stderr, "slimm: login ok for '%s', launching '%s'\n",
            ui->username, cmd);
    fflush(stderr);
    struct session_opts opts = {
        .desktop_name = ui->sessions[ui->selected_session].name,
        .pam_env = auth_get_env(sess),
        .vt_nr = vt_fd >= 0 ? vt_get_active_nr(vt_fd) : 1,
    };

    /* Release DRM master + restore the VT, then exec the compositor in place
     * (no fork). This process becomes the compositor; on its exit, systemd
     * (Restart=always) respawns the greeter. session_launch only returns if
     * exec failed entirely. */
    session_pre_fork(backend);
    session_launch(ui->username, cmd, &opts);

    fprintf(stderr, "slimm: session launch failed\n");
    session_free_opts(&opts);
    _exit(1);
}

static void drm_cleanup_vt(void)
{
    if (vt_fd >= 0) {
        vt_set_text(vt_fd);
        vt_give_control(vt_fd);
    }
}

static int init_drm(struct slim_renderer **renderer, struct backend *backend,
                    int *screen_w, int *screen_h)
{
    vt_fd = vt_open();
    if (vt_fd < 0) {
        fprintf(stderr, "main: DRM mode needs root on a bare TTY (e.g. via systemd on tty1)\n");
        return -1;
    }
    vt_take_control(vt_fd);
    vt_set_graphics(vt_fd);
    vt_scrub(vt_fd);

    drm_fd = drm_find_device();
    if (drm_fd < 0) {
        drm_cleanup_vt();
        fprintf(stderr, "main: no DRM device (need video group or root)\n");
        return -1;
    }

    struct slimm_output *output = drm_find_output(drm_fd);
    if (!output) {
        close(drm_fd);
        drm_fd = -1;
        drm_cleanup_vt();
        fprintf(stderr, "main: no connected display\n");
        return -1;
    }

    struct slimm_gbm *gbm = drm_init_gbm(output);
    if (!gbm) {
        close(drm_fd);
        drm_fd = -1;
        free(output);
        drm_cleanup_vt();
        return -1;
    }

    *screen_w = output->width;
    *screen_h = output->height;

    *renderer = renderer_create(gbm->dev, gbm->surface, *screen_w, *screen_h);
    if (!*renderer) {
        drm_destroy(gbm);
        close(drm_fd);
        drm_fd = -1;
        drm_cleanup_vt();
        return -1;
    }

    backend->is_drm = 1;
    backend->gbm = gbm;
    return 0;
}

#ifndef SLIMM_MINIMAL
static void config_load_mode(struct slim_config *cfg, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[64];
        if (sscanf(line, " %63s = %63s", key, val) != 2 ||
            strcmp(key, "mode") != 0)
            continue;
        size_t len = strlen(val);
        if (len >= 2 && val[0] == '"' && val[len - 1] == '"') {
            val[len - 1] = '\0';
            memmove(val, val + 1, len - 1);
        }
        strncpy(cfg->mode, val, sizeof(cfg->mode) - 1);
        cfg->mode[sizeof(cfg->mode) - 1] = '\0';
        break;
    }
    fclose(f);
}

static int init_wayland(struct slim_renderer **renderer, struct backend *backend,
                        int *screen_w, int *screen_h)
{
    struct slim_wayland *wl = calloc(1, sizeof(struct slim_wayland));
    if (!wl) return -1;

    if (wayland_connect(wl) < 0) {
        free(wl);
        return -1;
    }

    if (wayland_create_surface(wl) < 0) {
        wayland_destroy(wl);
        free(wl);
        return -1;
    }

    *screen_w = wl->width;
    *screen_h = wl->height;

    *renderer = renderer_create_wl(wl->display, wl->egl_window,
                                   *screen_w, *screen_h);
    if (!*renderer) {
        wayland_destroy(wl);
        free(wl);
        return -1;
    }

    backend->is_drm = 0;
    backend->wl = wl;
    return 0;
}
#endif

static const char *ste2_resolve(const char *explicit)
{
    static const char *defaults[] = {
        "/etc/slimm/theme.slimt",
        "/etc/slimm/theme.ste2",
        NULL,
    };

    if (explicit && access(explicit, R_OK) == 0)
        return explicit;
    for (int i = 0; defaults[i]; i++) {
        if (access(defaults[i], R_OK) == 0)
            return defaults[i];
    }
    return NULL;
}

static int load_theme(struct slim_config *config, const char *config_path,
                      const char *ste2_explicit,
                      struct slim_images *images, struct slim_font **font,
                      int screen_w, int screen_h)
{
    const char *ste2 = ste2_resolve(ste2_explicit);

#ifdef SLIMM_MINIMAL
    (void)config_path;
    if (!ste2) {
        fprintf(stderr, "main: STE2 theme required — run: slimc theme.toml -o theme.slimt\n");
        return -1;
    }
    config_init_defaults(config);
    if (ste2_load(ste2, config, images, font, screen_w, screen_h) < 0)
        return -1;
    config_load_sessions(config);
    return 0;
#else
    if (ste2) {
        config_init_defaults(config);
        if (ste2_load(ste2, config, images, font, screen_w, screen_h) == 0) {
            config_load_mode(config, config_path);
            config_load_sessions(config);
            return 0;
        }
        fprintf(stderr, "main: STE2 load failed, falling back to TOML\n");
    }

    config_load(config, config_path);
    const char *font_spec = config->theme.font_path[0]
                              ? config->theme.font_path
                              : config->theme.font_name;
    *font = font_load(font_spec, config->theme.font_size);
    if (!*font)
        return -1;
    theme_load_images(images, &config->theme, screen_w, screen_h);
    return 0;
#endif
}

int main(int argc, char *argv[])
{
    const char *config_path = "/etc/slimm/theme.toml";
    const char *ste2_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("SLiMM %s — stateless login bootstrapper\n", SLIMM_VERSION);
            printf("Successor to SLiM2; faithful to classic SLiM minimalism.\n");
            printf("See README.md for origins, testing, and configuration.\n\n");
            printf("Usage: slimm [options]\n\n");
            printf("Options:\n");
            printf("  --ste2 PATH    STE2 theme blob (default: /etc/slimm/theme.slimt)\n");
#ifndef SLIMM_MINIMAL
            printf("  --config PATH  TOML fallback (default: /etc/slimm/theme.toml)\n");
            printf("  --help         Show this help and exit\n\n");
            printf("Dev backend (theme.toml mode = \"wayland\"):\n");
            printf("  wayland  Overlay via zwlr_layer_shell_v1\n");
#else
            printf("  --help         Show this help and exit\n");
#endif
            printf("\nProduction: DRM/KMS + precompiled STE2 theme (slimc)\n");
            return 0;
        }
        if (strcmp(argv[i], "--ste2") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "slimm: --ste2 requires a path\n");
                return 1;
            }
            ste2_path = argv[++i];
            continue;
        }
#ifndef SLIMM_MINIMAL
        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "slimm: --config requires a path\n");
                return 1;
            }
            config_path = argv[++i];
            continue;
        }
#endif
        fprintf(stderr, "slimm: unknown option '%s' (try --help)\n", argv[i]);
        return 1;
    }

    signal(SIGUSR1, sigusr1_handler);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, sigterm_handler);
    setlinebuf(stderr);

    struct slim_config config;
    config_init_defaults(&config);

#ifndef SLIMM_MINIMAL
    config_load_mode(&config, config_path);
    int is_wayland = strcmp(config.mode, "wayland") == 0;
#else
    int is_wayland = 0;
#endif

    struct slim_renderer *renderer = NULL;
    struct backend backend = {0};
    int screen_w = 0, screen_h = 0;

#ifndef SLIMM_MINIMAL
    if (is_wayland) {
        if (init_wayland(&renderer, &backend, &screen_w, &screen_h) < 0) {
            fprintf(stderr, "main: Wayland init failed, falling back to DRM\n");
            is_wayland = 0;
        }
    }
#endif

    if (!is_wayland) {
        if (init_drm(&renderer, &backend, &screen_w, &screen_h) < 0) {
            fprintf(stderr, "main: DRM init failed\n");
#ifndef SLIMM_MINIMAL
            fprintf(stderr, "hint: dev overlay — mode = \"wayland\" in %s\n",
                    config_path);
#endif
            return 1;
        }
    }

    struct slim_font *font   = NULL;
    struct slim_images images = {0};

    if (load_theme(&config, config_path, ste2_path, &images, &font,
                   screen_w, screen_h) < 0) {
        slim_images_free(&images);
        renderer_destroy(renderer);
        if (backend.is_drm) drm_destroy(backend.gbm);
#ifndef SLIMM_MINIMAL
        else { wayland_destroy(backend.wl); free(backend.wl); }
#endif
        if (!is_wayland) { vt_set_text(vt_fd); vt_give_control(vt_fd); }
        return 1;
    }

    struct ui_state ui;
    ui_init(&ui, &config, &images, font);
    ui.screen_w = screen_w;
    ui.screen_h = screen_h;

    fprintf(stderr, "slimm: %s %dx%d\n",
            is_wayland ? "wayland" : "drm", screen_w, screen_h);

    ui_init_fbo(&ui, renderer);

    int input_fd = -1;
    struct slimm_input input;
#ifndef SLIMM_MINIMAL
    if (is_wayland) {
        input_fd = wayland_get_fd(backend.wl);
    } else
#endif
    {
        if (input_init(&input, config.enable_numlock) < 0) {
            fprintf(stderr, "main: input init failed\n");
            slim_images_free(&images);
            font_destroy(font);
            renderer_destroy(renderer);
            drm_destroy(backend.gbm);
            vt_set_text(vt_fd); vt_give_control(vt_fd);
            return 1;
        }
        input_fd = input_get_fd(&input);
    }

    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (timer_fd < 0) {
        fprintf(stderr, "main: timerfd_create failed\n");
        if (!is_wayland) input_destroy(&input);
        font_destroy(font);
        renderer_destroy(renderer);
        if (backend.is_drm) drm_destroy(backend.gbm);
#ifndef SLIMM_MINIMAL
        else { wayland_destroy(backend.wl); free(backend.wl); }
#endif
        if (!is_wayland) { vt_set_text(vt_fd); vt_give_control(vt_fd); }
        return 1;
    }

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        fprintf(stderr, "main: epoll_create1 failed\n");
        close(timer_fd);
        if (!is_wayland) input_destroy(&input);
        font_destroy(font);
        renderer_destroy(renderer);
        if (backend.is_drm) drm_destroy(backend.gbm);
#ifndef SLIMM_MINIMAL
        else { wayland_destroy(backend.wl); free(backend.wl); }
#endif
        if (!is_wayland) { vt_set_text(vt_fd); vt_give_control(vt_fd); }
        return 1;
    }

    struct epoll_event ev_input = {
        .events = EPOLLIN,
        .data = { .fd = input_fd },
    };
    struct epoll_event ev_timer = {
        .events = EPOLLIN,
        .data = { .fd = timer_fd },
    };

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, input_fd, &ev_input);
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &ev_timer);

    if (ui.autologin_active)
        set_autologin_timer();

    do_render(renderer, &ui, &backend);
    set_cursor_timer();

    while (running) {
        struct epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == input_fd) {
#ifndef SLIMM_MINIMAL
                if (is_wayland) {
                    wl_display_dispatch(backend.wl->display);
                    struct slim_wayland_event wev;
                    while (wayland_dispatch(backend.wl, &wev)) {
                        enum ui_action act = ui_key(&ui, wev.key_sym,
                                                     wev.key_pressed,
                                                     wev.utf8);
                        switch (act) {
                        case UI_LOGIN:    do_login(&ui, &backend); break;
                        case UI_QUIT:     exit_code = SLIMM_EXIT_CONSOLE; running = 0; break;
                        case UI_POWEROFF:
                            if (fork() == 0) { execl("/usr/bin/systemctl", "systemctl", "poweroff", NULL); _exit(1); }
                            break;
                        case UI_REBOOT:
                            if (fork() == 0) { execl("/usr/bin/systemctl", "systemctl", "reboot", NULL); _exit(1); }
                            break;
                        case UI_SUSPEND:
                            if (fork() == 0) { execl("/usr/bin/systemctl", "systemctl", "suspend", NULL); _exit(1); }
                            break;
                        default: break;
                        }
                        set_cursor_timer();
                        do_render(renderer, &ui, &backend);
                    }
                } else
#endif
                {
                    struct slimm_input_event ev;
                    while (input_dispatch(&input, &ev)) {
                        if (ev.type == 0) {
                            enum ui_action act = ui_key(&ui, ev.key_sym, ev.key_pressed, ev.utf8);
                            switch (act) {
                            case UI_LOGIN:    do_login(&ui, &backend); break;
                            case UI_QUIT:     exit_code = SLIMM_EXIT_CONSOLE; running = 0; break;
                            case UI_POWEROFF:
                                if (fork() == 0) { execl("/usr/bin/systemctl", "systemctl", "poweroff", NULL); _exit(1); }
                                break;
                            case UI_REBOOT:
                                if (fork() == 0) { execl("/usr/bin/systemctl", "systemctl", "reboot", NULL); _exit(1); }
                                break;
                            case UI_SUSPEND:
                                if (fork() == 0) { execl("/usr/bin/systemctl", "systemctl", "suspend", NULL); _exit(1); }
                                break;
                            default: break;
                            }
                            set_cursor_timer();
                            do_render(renderer, &ui, &backend);
                        }
                    }
                }
            }

            if (events[i].data.fd == timer_fd) {
                uint64_t exp;
                ssize_t nr = read(timer_fd, &exp, sizeof(exp));
                (void)nr;

                ui.cursor_visible = !ui.cursor_visible;
                ui.cursor_toggle++;

                if (ui.autologin_active) {
                    struct timespec now;
                    clock_gettime(CLOCK_MONOTONIC, &now);
                    long elapsed = now.tv_sec - ui.autologin_start.tv_sec;
                    if (elapsed >= ui.autologin_delay) {
                        ui.autologin_active = 0;
                        clear_timer();

                        struct slimm_session *sess = auth_autologin(ui.username);
                        if (!sess) continue;

                        const char *cmd = ui.sessions[ui.selected_session].exec;
                        struct session_opts opts = {
                            .desktop_name = ui.sessions[ui.selected_session].name,
                            .pam_env = auth_get_env(sess),
                            .vt_nr = vt_fd >= 0 ? vt_get_active_nr(vt_fd) : 1,
                        };

                        /* Release DRM/VT, then exec the compositor in place
                         * (no fork). systemd Restart=always respawns the
                         * greeter on logout. Returns only if exec failed. */
                        session_pre_fork(&backend);
                        session_launch(ui.username, cmd, &opts);

                        auth_close(sess);
                        session_free_opts(&opts);
                        _exit(1);
                    }
                }

                do_render(renderer, &ui, &backend);
            }
        }
    }

    close(epoll_fd);
    close(timer_fd);
    if (!is_wayland) input_destroy(&input);
    ui_destroy_fbo(&ui);
    slim_images_free(&images);
    font_destroy(font);
    renderer_destroy(renderer);
    if (backend.is_drm) {
        /* On Escape, drop onto the next console (a getty) so the user lands on a
         * login prompt instead of slimm's stale tty. Capture the target while we
         * still own the VT; activate it after releasing DRM + restoring text mode. */
        int console_vt = (exit_code == SLIMM_EXIT_CONSOLE && vt_fd >= 0)
                             ? vt_get_active_nr(vt_fd) + 1
                             : 0;
        drm_destroy(backend.gbm);
        if (drm_fd >= 0)
            close(drm_fd);
        drm_cleanup_vt();
        if (console_vt > 0)
            vt_activate(vt_fd, console_vt);
        close(vt_fd);
        vt_fd = -1;
        drm_fd = -1;
    }
#ifndef SLIMM_MINIMAL
    else {
        wayland_destroy(backend.wl);
        free(backend.wl);
    }
#endif

    /* SLIMM_EXIT_CONSOLE here (Escape) tells systemd not to respawn; 0 otherwise
     * (SIGTERM/stop). The compositor-exec path never reaches this return. */
    return exit_code;
}
