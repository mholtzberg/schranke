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
#include <unistd.h>

#include <libopencm3/stm32/gpio.h>
#include <liblcd/glib.h>
#include <liblcd/page.h>
#include <liblcd/page_menu.h>
#include <liblcd/page_action.h>

#include "boom.h"
#include "debug.h"
#include "fonts/fonts.h"
#include "timer.h"
#include "gui.h"

#define BUTTON_TIMEOUT    50

#define BUTTON_PORT       GPIOA
#define BUTTON_UP         GPIO7
#define BUTTON_DOWN       GPIO6
#define BUTTON_OK         GPIO1
#define BUTTON_BACK       GPIO15

static void gui_action_calibrate(struct page_ctx *ctx, void *priv,
                                 enum page_key key);

enum gui_action {
    GUI_ACTION_CALIBRATE,
    GUI_ACTION_OPEN,
    GUI_ACTION_CLOSE,
};

static const struct page_action g_action_calibrate = {
    .text = "OK zum starten",
    .keydown = gui_action_calibrate,
    .data = (void*)GUI_ACTION_CALIBRATE
};

static const struct page_action g_action_close = {
    .text = "OK zum schließen",
    .keydown = gui_action_calibrate,
    .data = (void*)GUI_ACTION_CLOSE
};

static const struct page_action g_action_open = {
    .text = "OK zum öffnen",
    .keydown = gui_action_calibrate,
    .data = (void*)GUI_ACTION_OPEN
};

static void gui_setfont(struct page_ctx *ctx, void *priv, enum page_font font);

static const struct page g_root_menu[] = {
    {
        .title = "Schranke auf",
        .type  = PAGE_TYPE_ACTION,
        .data  = &g_action_open
    }, {
        .title = "Schranke zu",
        .type  = PAGE_TYPE_ACTION,
        .data  = &g_action_close
    }, {
        .title = "Kalibrieren",
        .type  = PAGE_TYPE_ACTION,
        .data  = &g_action_calibrate
    }, {
        .type  = PAGE_TYPE_NONE
    }

};

static const struct page g_root_page = {
    .title = "Main Menu",
    .type  = PAGE_TYPE_MENU,
    .data  = g_root_menu
};

static struct page_ctx g_page_ctx;
static struct timer g_button_timer;

static const struct page_ops g_page_ops = {
    .setfont = gui_setfont
};



static void gui_setfont(struct page_ctx *ctx, void *priv, enum page_font font)
{
    (void)priv;
    (void)font;

    glib_font_set(ctx->glib, &font_courier10);
}

void gui_init(struct glib_ctx *glib)
{
    /* Buttons */

    gpio_set_mode(BUTTON_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN,
                  BUTTON_UP | BUTTON_DOWN | BUTTON_OK | BUTTON_BACK);
    gpio_set(BUTTON_PORT, BUTTON_UP | BUTTON_DOWN | BUTTON_OK | BUTTON_BACK);

    page_init(&g_page_ctx, glib, NULL, &g_page_ops, &g_root_page);
    timer_set(&g_button_timer, BUTTON_TIMEOUT);
}

void gui_process(void)
{
    if (timer_expired(&g_button_timer)) {
        uint32_t buttons = gpio_get(
            BUTTON_PORT, BUTTON_OK | BUTTON_BACK | BUTTON_UP | BUTTON_DOWN);

        if (!(buttons & BUTTON_BACK)) {
            page_keydown(&g_page_ctx, PAGE_KEY_BACK);
        }
        if (!(buttons & BUTTON_OK)) {
            page_keydown(&g_page_ctx, PAGE_KEY_OK);
        }
        if (!(buttons & BUTTON_UP)) {
            page_keydown(&g_page_ctx, PAGE_KEY_UP);
        }

        if (!(buttons & BUTTON_DOWN)) {
            page_keydown(&g_page_ctx, PAGE_KEY_DOWN);
        }

        timer_set(&g_button_timer, BUTTON_TIMEOUT);
    }
}

static void gui_action_calibrate(struct page_ctx *ctx, void *priv,
                                 enum page_key key)
{
    enum gui_action action = (enum gui_action)priv;

    if (key == PAGE_KEY_OK) {
        switch (action) {
        case GUI_ACTION_CALIBRATE:
            boom_calibrate();
            break;
        case GUI_ACTION_CLOSE:
            boom_close();
            break;
        case GUI_ACTION_OPEN:
            boom_open();
            break;
        }
    }
    page_pop(ctx);
}
