#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>
#include "auth.h"

struct auth_ctx {
    const char *username;
    const char *password;
};

static int conv_cb(int num_msg, const struct pam_message **msg,
                   struct pam_response **resp, void *appdata_ptr)
{
    struct auth_ctx *ctx = appdata_ptr;
    *resp = calloc(num_msg, sizeof(struct pam_response));
    if (!*resp) return PAM_CONV_ERR;

    for (int i = 0; i < num_msg; i++) {
        switch (msg[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF:
            resp[i]->resp = ctx ? strdup(ctx->password) : strdup("");
            if (!resp[i]->resp) { free(*resp); return PAM_CONV_ERR; }
            break;
        case PAM_PROMPT_ECHO_ON:
            resp[i]->resp = ctx ? strdup(ctx->username) : strdup("");
            if (!resp[i]->resp) { free(*resp); return PAM_CONV_ERR; }
            break;
        case PAM_ERROR_MSG:
        case PAM_TEXT_INFO:
            fprintf(stderr, "conv_cb: %s: %s\n",
                    msg[i]->msg_style == PAM_ERROR_MSG ? "ERROR" : "INFO",
                    msg[i]->msg);
            resp[i]->resp = NULL;
            break;
        }
    }
    return PAM_SUCCESS;
}

struct slimm_session *auth_login(const char *username, const char *password)
{
    struct auth_ctx ctx = { .username = username, .password = password };
    struct pam_conv conv = { conv_cb, &ctx };

    pam_handle_t *pamh = NULL;
    int ret = pam_start("slimm", username, &conv, &pamh);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "auth: pam_start failed: %s\n", pam_strerror(pamh, ret));
        return NULL;
    }

    ret = pam_authenticate(pamh, 0);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "auth: authenticate failed: %s\n", pam_strerror(pamh, ret));
        pam_end(pamh, ret);
        return NULL;
    }

    ret = pam_acct_mgmt(pamh, 0);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "auth: acct_mgmt failed: %s\n", pam_strerror(pamh, ret));
        pam_end(pamh, ret);
        return NULL;
    }

    ret = pam_setcred(pamh, PAM_ESTABLISH_CRED);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "auth: setcred failed: %s\n", pam_strerror(pamh, ret));
        pam_end(pamh, ret);
        return NULL;
    }

    ret = pam_open_session(pamh, 0);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "auth: open_session failed: %s\n", pam_strerror(pamh, ret));
        pam_end(pamh, ret);
        return NULL;
    }

    struct slimm_session *s = calloc(1, sizeof(*s));
    s->pamh = pamh;
    return s;
}

struct slimm_session *auth_autologin(const char *username)
{
    struct pam_conv conv = { conv_cb, NULL };

    pam_handle_t *pamh = NULL;
    int ret = pam_start("slimm", username, &conv, &pamh);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "auth: pam_start failed: %s\n", pam_strerror(pamh, ret));
        return NULL;
    }

    ret = pam_acct_mgmt(pamh, 0);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "auth: acct_mgmt failed: %s\n", pam_strerror(pamh, ret));
        pam_end(pamh, ret);
        return NULL;
    }

    ret = pam_setcred(pamh, PAM_ESTABLISH_CRED);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "auth: setcred failed: %s\n", pam_strerror(pamh, ret));
        pam_end(pamh, ret);
        return NULL;
    }

    ret = pam_open_session(pamh, 0);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "auth: open_session failed: %s\n", pam_strerror(pamh, ret));
        pam_end(pamh, ret);
        return NULL;
    }

    struct slimm_session *s = calloc(1, sizeof(*s));
    s->pamh = pamh;
    return s;
}

char **auth_get_env(struct slimm_session *s)
{
    if (!s) return NULL;
    return pam_getenvlist(s->pamh);
}

void auth_close(struct slimm_session *s)
{
    if (!s) return;
    pam_close_session(s->pamh, 0);
    pam_setcred(s->pamh, PAM_DELETE_CRED);
    pam_end(s->pamh, PAM_SUCCESS);
    free(s);
}
