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

#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

#include "debug.h"
#include "timer.h"
#include "boom.h"

#define BOOM_INTERVAL       500    /* 500 ms */
#define BOOM_TIMEOUT        10000  /* Move boom for maximum 10 seconds */

#define BOOM_PWR_PORT      GPIOA
#define BOOM_PWR_PIN       GPIO5
#define BOOM_DIR_PORT      GPIOA
#define BOOM_DIR_PIN       GPIO4
#define BOOM_SIG_PORT      GPIOA
#define BOOM_SIG_PIN       GPIO11

#define BOOM_CAL_DELTA      10

#ifndef ABS
#  define ABS(x) (((x) < 0) ? (-(x)) : (x))
#endif

enum state {
    STATE_OPENING,
    STATE_CLOSING,
    STATE_OPENED,
    STATE_CLOSED,
    STATE_CALUP,
    STATE_CALSETDIR,
    STATE_CALDOWN,
    STATE_EMERGENCY
};

static int g_adc_opened = 0;
static int g_adc_closed = 0;

static enum state g_dest = STATE_CLOSED;
static enum state g_state = STATE_CLOSED;

static struct timer g_timer;
static struct timer g_timeout;

static void adc_setup(void)
{
    rcc_periph_clock_enable(RCC_ADC1);
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, GPIO0);

    /* Make sure the ADC doesn't run during config. */
    adc_off(ADC1);

    /* We configure everything for one single conversion. */
    adc_disable_scan_mode(ADC1);
    adc_set_single_conversion_mode(ADC1);
    adc_disable_external_trigger_regular(ADC1);
    adc_set_right_aligned(ADC1);
    adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_28DOT5CYC);

    adc_power_on(ADC1);

    /* Wait for ADC starting up. */
    int i;
    for (i = 0; i < 800000; i++) /* Wait a bit. */
        __asm__("nop");

    adc_reset_calibration(ADC1);
    adc_calibration(ADC1);
}

static uint16_t adc_read(void)
{
    uint8_t channel_array[16];
    channel_array[0] = 0;       /* channel */
    adc_set_regular_sequence(ADC1, 1, channel_array);
    adc_start_conversion_direct(ADC1);
    while (!adc_eoc(ADC1));
    uint16_t reg16 = adc_read_regular(ADC1);
    return reg16;
}

static void boom_signal_set(bool on)
{
    if (on)
        gpio_set(BOOM_SIG_PORT, BOOM_SIG_PIN);
    else
        gpio_clear(BOOM_SIG_PORT, BOOM_SIG_PIN);
}

static void motor_pwr_set(bool on)
{
    boom_signal_set(on);

    if (on)
        gpio_set(BOOM_PWR_PORT, BOOM_PWR_PIN);
    else
        gpio_clear(BOOM_PWR_PORT, BOOM_PWR_PIN);
}

static void motor_dir_set(bool down)
{
    if (down)
        gpio_set(BOOM_DIR_PORT, BOOM_DIR_PIN);
    else
        gpio_clear(BOOM_DIR_PORT, BOOM_DIR_PIN);
}

void boom_init()
{
    adc_setup();

    /* The relays are open collector */
    gpio_set_mode(BOOM_PWR_PORT, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, BOOM_PWR_PIN);
    gpio_set_mode(BOOM_DIR_PORT, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, BOOM_DIR_PIN);
    gpio_set_mode(BOOM_SIG_PORT, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, BOOM_SIG_PIN);


    timer_set(&g_timer, BOOM_INTERVAL);
}

void boom_open()
{
    g_dest = STATE_OPENED;
}

bool boom_isopen()
{
    return g_state == STATE_OPENED;
}

void boom_close()
{
    g_dest = STATE_CLOSED;
}

void boom_emergency()
{
    motor_pwr_set(false);
    motor_dir_set(false);
    g_state = STATE_EMERGENCY;
}

void boom_calibrate()
{
    g_dest = STATE_CALUP;
}

void boom_process()
{
    static int last_adc = 0;
    int adc;

    if (timer_expired(&g_timer)) {
        timer_set(&g_timer, BOOM_INTERVAL);

        switch (g_state) {
        case STATE_OPENING:
            boom_signal_set(true);
            if (adc_read() > g_adc_opened || timer_expired(&g_timeout)) {
                motor_pwr_set(false);
                g_state = STATE_OPENED;
            } else {
                motor_pwr_set(true);
            }
            break;

        case STATE_OPENED:
            if (g_dest == STATE_CLOSED) {
                timer_set(&g_timeout, BOOM_TIMEOUT);
                motor_dir_set(true);
                g_state = STATE_CLOSING;
            } else if (g_dest == STATE_CALUP) {
                g_state = STATE_CALUP;
                last_adc = 0;
                motor_dir_set(false);
                timer_set(&g_timeout, BOOM_TIMEOUT);
            } else {
                motor_dir_set(false);
            }
            break;

        case STATE_CLOSING:
            if (adc_read() < g_adc_closed || timer_expired(&g_timeout)) {
                g_state = STATE_CLOSED;
                motor_pwr_set(false);
            } else {
                motor_pwr_set(true);
            }
            break;

        case STATE_CLOSED:
            if (g_dest == STATE_OPENED) {
                timer_set(&g_timeout, BOOM_TIMEOUT);
                motor_dir_set(false);
                g_state = STATE_OPENING;
            } else if (g_dest == STATE_CALUP) {
                g_state = STATE_CALUP;
                last_adc = 0;
                motor_dir_set(false);
                timer_set(&g_timeout, BOOM_TIMEOUT);
            } else {
                motor_dir_set(false);
            }
            break;

        case STATE_CALUP:
            g_dest  = STATE_CLOSED;

            motor_pwr_set(true);
            adc = adc_read();
            if (ABS(last_adc - adc) < BOOM_CAL_DELTA ||
                timer_expired(&g_timeout)) {

                g_adc_opened = adc - BOOM_CAL_DELTA;
                motor_pwr_set(false);
                g_state = STATE_CALSETDIR;
            }
            last_adc = adc;

            break;

        case STATE_CALSETDIR:
            motor_dir_set(true);
            timer_set(&g_timeout, BOOM_TIMEOUT);
            last_adc = 0;
            g_state = STATE_CALDOWN;
            break;

        case STATE_CALDOWN:
            motor_pwr_set(true);
            adc = adc_read();
            if (ABS(last_adc - adc) < BOOM_CAL_DELTA
                || timer_expired(&g_timeout)) {

                g_adc_closed = adc + BOOM_CAL_DELTA;
                motor_pwr_set(false);
                g_state = STATE_CLOSED;
            }
            last_adc = adc;

            break;

        case STATE_EMERGENCY:
            motor_pwr_set(false);
            motor_dir_set(false);
            break;
        }
    }
}
