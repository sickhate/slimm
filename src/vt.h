#ifndef SLIMM_VT_H
#define SLIMM_VT_H

int vt_open(void);
int vt_get_active_nr(int vt_fd);
void vt_activate(int vt_fd, int nr);
void vt_take_control(int vt_fd);
void vt_give_control(int vt_fd);
void vt_console_shield(int vt_fd);
void vt_scrub(int vt_fd);
void vt_set_graphics(int vt_fd);
void vt_set_text(int vt_fd);
void vt_clear(int vt_fd);

#endif
