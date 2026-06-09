#ifndef SLIMM_SESSION_H
#define SLIMM_SESSION_H

struct session_opts {
    const char *desktop_name;
    char **pam_env;
    const char *cursor_theme;
    int cursor_size;
    int vt_nr;
};

struct session_launch_args {
    const char *username;
    const char *password; /* NULL for autologin */
    const char *cmd;
    const char *desktop_name;
    int vt_nr;
};

/* Greeter forks, child runs this: PAM → wait user session → compositor → exec slimm */
void session_reaper(const struct session_launch_args *args);

#endif
