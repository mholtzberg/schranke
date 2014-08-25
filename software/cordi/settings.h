#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <time.h>

void settings_init(void);
void settings_save(void);
void settings_getopen(struct tm *time);
void settings_setopen(struct tm *time);
void settings_getclose(struct tm *time);
void settings_setclose(struct tm *time);
void settings_setcal(int up, int down);
void settings_getcal(int *up, int *down);

#endif  /* _SETTINGS_H_ */
