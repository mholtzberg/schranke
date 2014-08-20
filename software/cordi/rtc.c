#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/rtc.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/cm3/nvic.h>

#include "boom.h"
#include "settings.h"
#include "rtc.h"

static bool timcmp(struct tm *a, struct tm *b)
{
    return a->tm_hour == b->tm_hour &&
        a->tm_min == b->tm_min &&
        a->tm_sec == b->tm_sec;
}

void rtc_isr(void)
{
    struct tm open;
    struct tm close;
    struct tm *now;

    time_t time = rtc_time();
    now = gmtime(&time);

    settings_getopen(&open);
    settings_getclose(&close);

    if (timcmp(now, &open)) {
        boom_open();
    } else if (timcmp(now, &close)) {
        boom_close();
    }

    /* The interrupt flag isn't cleared by hardware, we have to do it. */
    rtc_clear_flag(RTC_SEC);
}

void rtc_init(void)
{
    /*
     * If the RTC is pre-configured just allow access, don't reconfigure.
     * Otherwise enable it with the LSE as clock source and 0x7fff as
     * prescale value.
     */
    rtc_auto_awake(LSE, 0x7fff);

    /* Without this the RTC interrupt routine will never be called. */
    nvic_enable_irq(NVIC_RTC_IRQ);
    nvic_set_priority(NVIC_RTC_IRQ, 1);

    /* Enable the RTC interrupt to occur off the SEC flag. */
    rtc_interrupt_enable(RTC_SEC);
}

void rtc_set(time_t time)
{
    rtc_set_counter_val(time);
}

time_t rtc_time(void)
{
    return rtc_get_counter_val();
}
