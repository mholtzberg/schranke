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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>

#include "boom.h"
#include "debug.h"
#include "ring.h"
#include "settings.h"
#include "rtc.h"
#include "timer.h"
#include "gsm.h"


#define GSM_PORT          GPIOA
#define GSM_PWR_PIN       GPIO12
#define GSM_RST_PIN       GPIO13

#define GSM_USART         USART2

#define GSM_TIMEOUT       2000       /* 2000 ms */
#define GSM_STATE_TIMEOUT 10000      /* 10 sec */

enum gsm_state {
    STATE_RESET,
    STATE_POWERUP,
    STATE_STARTUP,
    STATE_SETPIN,
    STATE_SETPINOK,
    STATE_READSMSNUMBER,
    STATE_READSMSTEXT,
    STATE_DELETESMS,
    STATE_SENDSMS,
    STATE_WAITOK,
    STATE_IDLE
};

struct gsm_sms {
    struct gsm_sms *next;
    char recipient[16];
    char msg[160];
};


static struct gsm_sms *g_sms_list = NULL;
static enum gsm_state g_state;
static struct timer g_timer;
static struct timer g_state_timer;

static struct ring g_outbuf;
static struct ring g_inbuf;

static int g_count = 0;

static void gsm_write(const char *fmt, ...)
{
    va_list args;
    char buf[64];
    const char *p = buf;
    size_t len;

    va_start(args, fmt);
    len = vsniprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;

    buf[len] = '\0';

    USART_CR1(USART2) &= ~USART_CR1_TXEIE;

    while (*p != '\0')
        ring_enq(&g_outbuf, *p++);

    USART_CR1(USART2) |= USART_CR1_TXEIE;
}

static const char *gsm_getline(void)
{
    static char line[256];
    static unsigned pos = 0;
    int c;

    USART_CR1(GSM_USART) &= ~USART_CR1_RXNEIE;

    while ((c = ring_deq(&g_inbuf)) >= 0 && c != '\n') {
        line[pos] = c;
        dbg("%c", c);
        if (++pos >= sizeof(line))
            pos = 0;
    }

    USART_CR1(GSM_USART) |= USART_CR1_RXNEIE;

    if (c == '\n') {
        dbg("\n");
        /* Overwrite CR */

        if (pos > 0)
            line[pos-1] = '\0';
        else
            line[0] = '\0';

        pos = 0;
        return line;
    } else {
        return NULL;
    }
}

static int gsm_getc(void)
{
    int ret;
    USART_CR1(GSM_USART) &= ~USART_CR1_RXNEIE;
    ret = ring_deq(&g_inbuf);
    USART_CR1(GSM_USART) |= USART_CR1_RXNEIE;
    return ret;
}

static void gsm_setsched(const char *line)
{
    struct tm open;
    struct tm close;

    memset(&open, 0, sizeof(open));
    memset(&close, 0, sizeof(close));

    if (siscanf(line, "!%d:%d-%d:%d",
                  &open.tm_hour, &open.tm_min,
                  &close.tm_hour, &close.tm_min) == 4) {
        settings_setopen(&open);
        settings_setclose(&close);
        settings_save();
    }
}

static void gsm_status(void)
{
    static struct gsm_sms sms;
    struct gsm_sms *s;
    struct tm open;
    struct tm close;

    settings_getopen(&open);
    settings_getclose(&close);

    memset(&sms, 0, sizeof(sms));

    sniprintf(sms.recipient, sizeof(sms.recipient), "+4917624347476");
    sniprintf(sms.msg, sizeof(sms.msg), "Status: %s\n"
              "Offen von: %02d:%02d bis %02d:%02d",
              boom_isopen() ? "Auf" : "Zu",
              open.tm_hour, open.tm_min,
              close.tm_hour, close.tm_min);

    if (g_sms_list == NULL) {
        g_sms_list = &sms;
    } else {
        s = g_sms_list;
        while (s->next != NULL)
            s = s->next;
        s->next = &sms;
    }
}

static void gsm_reset(void)
{
    g_state = STATE_RESET;
    gpio_clear(GSM_PORT, GSM_PWR_PIN);
    gpio_set(GSM_PORT, GSM_RST_PIN);

    timer_set(&g_timer, GSM_TIMEOUT);
    timer_set(&g_state_timer, GSM_STATE_TIMEOUT);
}

void gsm_init()
{
    /* Setup Reset and Power pins */

    gpio_set_mode(GSM_PORT, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, GSM_PWR_PIN | GSM_RST_PIN);

    ring_init(&g_outbuf);
    ring_init(&g_inbuf);

    /* Enable USART clocking */

    rcc_periph_clock_enable(RCC_USART2);

    /* Enable the USART2 interrupt. */

    nvic_enable_irq(NVIC_USART2_IRQ);

    /* Setup GPIO pin GPIO_USART2_TX on GPIO port A for transmit. */

    gpio_set_mode(GPIO_BANK_USART2_TX, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART2_TX);

    /* Setup GPIO pin GPIO_USART2_RX on GPIO port A for receive. */

    gpio_set_mode(GPIO_BANK_USART2_RX, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_FLOAT, GPIO_USART2_RX);

    /* Setup UART parameters. */

    usart_set_baudrate(GSM_USART, 115200);
    usart_set_databits(GSM_USART, 8);
    usart_set_stopbits(GSM_USART, USART_STOPBITS_1);
    usart_set_parity(GSM_USART, USART_PARITY_NONE);
    usart_set_flow_control(GSM_USART, USART_FLOWCONTROL_NONE);
    usart_set_mode(GSM_USART, USART_MODE_TX_RX);

    /* Enable USART2 Receive interrupt. */

    USART_CR1(GSM_USART) |= USART_CR1_RXNEIE;

    /* Finally enable the USART. */

    usart_enable(GSM_USART);

    gsm_reset();
}

void gsm_process()
{
    const char *line;
    static int smsid;
    static char sender[16];
    static struct tm tm;
    static enum gsm_state last_state = STATE_RESET;

    if (last_state != g_state || g_state == STATE_IDLE) {
        timer_set(&g_state_timer, GSM_STATE_TIMEOUT);
        last_state = g_state;
    }

    if (timer_expired(&g_state_timer)) {
        gsm_reset();
    }

    int c;

    switch (g_state) {
    case STATE_RESET:
        if (timer_expired(&g_timer)) {
            timer_set(&g_timer, GSM_TIMEOUT);

            gpio_clear(GSM_PORT, GSM_RST_PIN);
            gpio_set(GSM_PORT, GSM_PWR_PIN);
            g_state = STATE_POWERUP;
        }
        break;

    case STATE_POWERUP:
        if (timer_expired(&g_timer)) {
            timer_set(&g_timer, GSM_TIMEOUT);

            gpio_clear(GSM_PORT, GSM_PWR_PIN);
            g_state = STATE_STARTUP;
        }
        break;

    case STATE_STARTUP:

        if ((line = gsm_getline()) && strcmp(line, "OK") == 0) {
            g_state = STATE_SETPIN;
        } else if (timer_expired(&g_timer)) {
            gsm_write("AT\r");
            timer_set(&g_timer, GSM_TIMEOUT);
        }
        break;

    case STATE_SETPIN:
        if (timer_expired(&g_timer)) {
            gsm_write("AT+CPIN=8715\r\n");

            timer_set(&g_timer, GSM_TIMEOUT);
            g_state = STATE_SETPINOK;
        }
        break;

    case STATE_SETPINOK:
        if ((line = gsm_getline()) && strcmp(line, "OK") == 0) {
            g_state = STATE_IDLE;
        } else if (timer_expired(&g_timer)) {
            gpio_clear(GSM_PORT, GSM_PWR_PIN);
            gpio_set(GSM_PORT, GSM_RST_PIN);
            g_state = STATE_RESET;
        }
        break;

    case STATE_WAITOK:
        if ((line = gsm_getline()) && strcmp(line, "OK") == 0) {
            g_state = STATE_IDLE;
        }
        break;

    case STATE_IDLE:
        if ((line = gsm_getline()) &&
            siscanf(line, "+CMTI: \"SM\", %d", &smsid) == 1) {

            gsm_write("AT+CMGR=%d\r\n", smsid);
            g_state = STATE_READSMSNUMBER;
        } else if (g_sms_list != NULL) {

            gsm_write("AT+CMGS=\"%s\"\r", g_sms_list->recipient);

            g_state = STATE_SENDSMS;
        }
        break;

    case STATE_READSMSNUMBER:
        memset(&tm, 0, sizeof(tm));

        if ((line = gsm_getline()) &&
            siscanf(line, "+CMGR: \"REC UNREAD\",\"%16[0-9+]\",\"\",\"%d/%d/%d,%d:%d:%d",
                   sender,
                   &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                   &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 7) {

            tm.tm_year += 2000;
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;

            /* May extract sender number if needed */
            g_state = STATE_READSMSTEXT;
        }
        break;

    case STATE_READSMSTEXT:
        if ((line = gsm_getline())) {
            if (strncasecmp(line, "auf", 2) == 0) {
                boom_open();
            } else if (strncasecmp(line, "zu", 2) == 0) {
                boom_close();
            } else if (strncmp(line, "?", 1) == 0) {
                gsm_status();
            } else if (strncmp(line, "!", 1) == 0) {
                rtc_set(mktime(&tm));
                gsm_setsched(line);
            }

            g_state = STATE_DELETESMS;
        }

        break;

    case STATE_DELETESMS:
        if ((line = gsm_getline()) && strcmp(line, "OK") == 0) {
            gsm_write("AT+CMGD=1,4\r\n", smsid);
            g_state = STATE_WAITOK;
        }
        break;

    case STATE_SENDSMS:
        if ((c = gsm_getc()) >= 0 && c == '>') {
            gsm_write("%s\x1a", g_sms_list->msg);

            /* Don't forget to remove from list */
            g_sms_list = g_sms_list->next;

            g_state = STATE_WAITOK;
        }

    }
}

void usart2_isr()
{
    int c;

    /* Check if we were called because of RXNE. */

    if ((USART_CR1(GSM_USART) & USART_CR1_RXNEIE) != 0 &&
        (USART_SR(GSM_USART) & USART_SR_RXNE) != 0) {

        g_count++;
        /* Retrieve the data from the peripheral. */

        ring_enq(&g_inbuf, usart_recv(GSM_USART));
    }

    /* Check if we were called because of TXE. */
    if ((USART_CR1(GSM_USART) & USART_CR1_TXEIE) != 0 &&
        (USART_SR(GSM_USART) & USART_SR_TXE) != 0) {

        c = ring_deq(&g_outbuf);

        if (c >= 0) {
            /* Put data into the transmit register. */

            usart_send(GSM_USART, c);
        } else {
            /* Disable the TXE interrupt as we don't need it anymore. */

            USART_CR1(GSM_USART) &= ~USART_CR1_TXEIE;
        }
    }
}
