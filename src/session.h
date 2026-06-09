#ifndef SLIMM_SESSION_H
#define SLIMM_SESSION_H

struct session_opts {
    const char *desktop_name;
    char **pam_env;
    const char *cursor_theme;
    int cursor_size;
    int vt_nr;
};

void session_launch(const char *username, const char *cmd,
                    const struct session_opts *opts);

#endif
