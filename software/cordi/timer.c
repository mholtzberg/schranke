/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 UVC Ingenieure http://uvc.de/
 * Author: Max Holtzberg <mh@uvc.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>

#include "debug.h"
#include "timer.h"

static struct timer volatile * volatile g_timer = NULL;

void sys_tick_handler(void)
{
    struct timer volatile * volatile * timer = &g_timer;

    timer = &g_timer;
    while (*timer != NULL) {
        (*timer)->value--;

        if ((*timer)->value > 0) {
            timer = &(*timer)->next;
        } else {
            /* remove from list */
            *timer = (*timer)->next;
        }
    }

}

void timer_set(struct timer *timer, int timeout)
{
    struct timer volatile *t = g_timer;

    timer->value = timeout;

    /* check if timer is already in list */
    while (t != NULL) {
        if (t == timer)
            return;
        t = t->next;
    }

    systick_interrupt_disable();
    timer->next = g_timer;
    g_timer = timer;
    systick_interrupt_enable();
}

bool timer_expired(struct timer *timer)
{
    return timer->value <= 0;
}

void timer_init(void)
{
    /* 24MHz  => 24000000 counts per second */
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);

    /* 24000000/24000 = 1000 overflows per second - every 1ms one interrupt */
    /* SysTick interrupt every N clock pulses: set reload to N-1 */
    systick_set_reload(23999);

    systick_interrupt_enable();

    /* Start counting. */
    systick_counter_enable();
}
