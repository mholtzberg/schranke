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

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>

#include "boom.h"
#include "debug.h"
#include "ring.h"
#include "timer.h"
#include "gsm.h"


#define GSM_PORT      GPIOA
#define GSM_PWR_PIN   GPIO12
#define GSM_RST_PIN   GPIO13

#define GSM_USART     USART2

#define GSM_TIMEOUT  1000       /* 1000 ms */

enum gsm_state {
    STATE_RESET,
    STATE_POWERUP,
    STATE_STARTUP,
    STATE_SETPIN,
    STATE_SETPINOK,
    STATE_READSMSNUMBER,
    STATE_READSMSTEXT,
    STATE_DELETESMS,
    STATE_IDLE
};

static enum gsm_state g_state;
static struct timer g_timer;

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
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;

    buf[len] = '\0';

    while (*p != '\0')
        ring_enq(&g_outbuf, *p++);

    USART_CR1(USART2) |= USART_CR1_TXEIE;
}

static const char *gsm_getline(void)
{
    static char line[256];
    static unsigned pos = 0;
    int c;

    while ((c = ring_deq(&g_inbuf)) >= 0 && c != '\n') {
        line[pos] = c;
        dbg("%c", c);
        if (++pos >= sizeof(line))
            pos = 0;
    }

    if (c == '\n') {
        dbg("\n");
        /* Overwrite CR */

        line[pos-1] = '\0';
        pos = 0;
        return line;
    } else {
        return NULL;
    }
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

    g_state = STATE_RESET;

    gpio_clear(GSM_PORT, GSM_PWR_PIN);
    gpio_set(GSM_PORT, GSM_RST_PIN);

    timer_set(&g_timer, GSM_TIMEOUT);
}

void gsm_process()
{
    const char *line = gsm_getline();
    static int smsid;

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
        if (line && strcmp(line, "OK") == 0) {
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
        if (line && strcmp(line, "OK") == 0) {
            g_state = STATE_IDLE;
        } else if (timer_expired(&g_timer)) {
            gpio_clear(GSM_PORT, GSM_PWR_PIN);
            gpio_set(GSM_PORT, GSM_RST_PIN);
            g_state = STATE_RESET;
        }
        break;

    case STATE_IDLE:
        if (line && sscanf(line, "+CMTI: \"SM\", %d", &smsid) == 1) {
            gsm_write("AT+CMGR=%d\r\n", smsid);
            g_state = STATE_READSMSNUMBER;
        }
        break;

    case STATE_READSMSNUMBER:
        if (line && strncmp(line, "+CMGR", 5) == 0) {
            g_state = STATE_READSMSTEXT;
        }
        break;

    case STATE_READSMSTEXT:
        if (line) {
            if (strcasecmp(line, "auf") == 0) {
                dbg("AUF\r\n");
                boom_open();
            } else if (strcasecmp(line, "zu") == 0) {
                dbg("ZU\r\n");
                boom_close();
            }


            g_state = STATE_DELETESMS;
        }

        break;

    case STATE_DELETESMS:
        if (line && strcmp(line, "OK") == 0) {
            gsm_write("AT+CMGD=%d\r\n", smsid);
            g_state = STATE_IDLE;
        }
        break;

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
