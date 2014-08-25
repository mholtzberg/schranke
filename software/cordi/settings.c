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
    int ret = flash_write(FLASH_LOCATION, (uint8_t*)&g_settings, sizeof(g_settings));
    dbg("written: %d\n", ret);
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
