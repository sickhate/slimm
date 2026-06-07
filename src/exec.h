#ifndef SLIMM_EXEC_H
#define SLIMM_EXEC_H

void exec_sanitize_desktop(char *s);
int  exec_build_argv(char *cmd, char **argv, int max);
int  exec_try_command(char *cmd);
void exec_relaunch_slimm(void);

#endif
