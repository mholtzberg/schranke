#ifndef _RTC_H_
#define _RTC_H_

#include <time.h>

void rtc_init(void);
void rtc_set(time_t time);
time_t rtc_time(void);

#endif  /* _RTC_H_ */
