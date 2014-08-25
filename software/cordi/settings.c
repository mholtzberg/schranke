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

#include <stdbool.h>
#include <string.h>

#include <libopencm3/stm32/flash.h>

#include "debug.h"
#include "settings.h"

#define FLASH_LOCATION ((uint32_t)0x0800fc00)
#define FLASH_PAGE_NUM_MAX 63
#define FLASH_PAGE_SIZE 0x400   /* 1kb */

static struct settings {
    bool invalid;
    struct tm open;
    struct tm close;
    int up;
    int down;
} g_settings;

static int flash_write(uint32_t start_address, uint8_t *input_data, uint16_t num_elements)
{
    uint16_t iter;
    uint32_t current_address = start_address;
    uint32_t page_address = start_address;
    uint32_t flash_status = 0;

    /*check if start_address is in proper range*/
    if((start_address - FLASH_BASE) >= (FLASH_PAGE_SIZE * (FLASH_PAGE_NUM_MAX+1)))
        return 1;

    /*calculate current page address*/
    if(start_address % FLASH_PAGE_SIZE)
        page_address -= (start_address % FLASH_PAGE_SIZE);

    flash_unlock();

    /*Erasing page*/
    flash_erase_page(page_address);
    flash_status = flash_get_status_flags();
    if(flash_status != FLASH_SR_EOP)
        return flash_status;

    /*programming flash memory*/
    for(iter=0; iter<num_elements; iter += 4)
    {
        /*programming word data*/
        flash_program_word(current_address+iter, *((uint32_t*)(input_data + iter)));
        flash_status = flash_get_status_flags();
        if(flash_status != FLASH_SR_EOP)
            return flash_status;

        /*verify if correct data is programmed*/
        if(*((uint32_t*)(current_address+iter)) != *((uint32_t*)(input_data + iter)))
            return -1;
    }

    return 0;
}

static void flash_read(uint32_t start_address, uint8_t *output_data, uint16_t num_elements)
{
    uint16_t iter;
    uint32_t *memory_ptr= (uint32_t*)start_address;

    for(iter=0; iter<num_elements/4; iter++)
    {
        *(uint32_t*)output_data = *(memory_ptr + iter);
        output_data += 4;
    }
}

void settings_init()
{
    flash_read(FLASH_LOCATION, (uint8_t*)&g_settings, sizeof(g_settings));
    if (g_settings.invalid) {
        memset(&g_settings, 0, sizeof(g_settings));
        settings_save();
    }
}

void settings_save()
{
    flash_write(FLASH_LOCATION, (uint8_t*)&g_settings, sizeof(g_settings));
}

void settings_setopen(struct tm *time)
{
    memcpy(&g_settings.open, time, sizeof(*time));
}

void settings_setclose(struct tm *time)
{
    memcpy(&g_settings.close, time, sizeof(*time));
}

void settings_getopen(struct tm *time)
{
    memcpy(time, &g_settings.open, sizeof(*time));
}

void settings_getclose(struct tm *time)
{
    memcpy(time, &g_settings.close, sizeof(*time));
}

void settings_setcal(int up, int down)
{
    g_settings.up   = up;
    g_settings.down = down;
}

void settings_getcal(int *up, int *down)
{
    if (g_settings.invalid) {
        *up   = -1;
        *down = -1;
    } else {
        *up   = g_settings.up;
        *down = g_settings.down;
    }
}
