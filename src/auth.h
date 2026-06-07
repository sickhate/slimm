#ifndef SLIMM_AUTH_H
#define SLIMM_AUTH_H

#include <security/pam_appl.h>

struct slimm_session {
    pam_handle_t *pamh;
};

struct slimm_session *auth_login(const char *username, const char *password);
struct slimm_session *auth_autologin(const char *username);
char **auth_get_env(struct slimm_session *s);
void auth_close(struct slimm_session *s);

#endif
