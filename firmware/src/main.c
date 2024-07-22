/*
 * Controller Main
 * WHowe <github.com/whowechina>
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "bsp/board.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "tusb.h"
#include "usb_descriptors.h"

#include "aime.h"
#include "nfc.h"

#include "board_defs.h"

#include "touch.h"
#include "button.h"
#include "rgb.h"

#include "save.h"
#include "config.h"
#include "cli.h"
#include "commands.h"
#include "io.h"
#include "hid.h"

static void run_lights()
{
    uint64_t now = time_us_64();
    if (io_is_active() || aime_is_active())
    {
        return;
    }

    static uint16_t loop = 0;
    loop++;
    uint16_t buttons = button_read();
    for (int i = 0; i < 8; i++)
    {
        uint8_t phase = (i * 256 + loop) / 8;
        uint32_t color;
        if (buttons & (1 << i))
        {
            color = rgb32_from_hsv(phase, 64, 255);
        }
        else
        {
            color = rgb32_from_hsv(phase, 240, 20);
        }
        rgb_set_button(i, color, 0);
    }

    uint32_t aime_color = aime_led_color();
    uint8_t r = aime_color >> 16;
    uint8_t g = aime_color >> 8;
    uint8_t b = aime_color;

    aime_color = rgb32(r, g, b, false);

    //set_color(62, aime_color, 0);
    //set_color(63, aime_color, 0);
}

const int aime_intf = 3;
static void cdc_aime_putc(uint8_t byte)
{
    tud_cdc_n_write(aime_intf, &byte, 1);
    tud_cdc_n_write_flush(aime_intf);
}

static void aime_run()
{
    if (tud_cdc_n_available(aime_intf))
    {
        uint8_t buf[32];
        uint32_t count = tud_cdc_n_read(aime_intf, buf, sizeof(buf));

        for (int i = 0; i < count; i++)
        {
            aime_feed(buf[i]);
        }
    }
}
static mutex_t core1_io_lock;
static void core1_loop()
{
    while (1)
    {
        if (mutex_try_enter(&core1_io_lock, NULL))
        {
            run_lights();
            rgb_update();
            mutex_exit(&core1_io_lock);
        }
        cli_fps_count(1);
        sleep_ms(1);
    }
}

static void core0_loop()
{
    uint64_t next_frame = time_us_64();

    while (1)
    {
        tud_task();
        io_update();

        cli_run();
        aime_run();
        save_loop();
        cli_fps_count(0);

        sleep_until(from_us_since_boot(next_frame));
        next_frame += 1000; // 1KHz

        touch_update();
        button_update();

        hid_update();
    }
}

void init()
{
    sleep_ms(50);
    set_sys_clock_khz(150000, true);
    board_init();

    tusb_init();
    stdio_init_all();

    config_init();
    mutex_init(&core1_io_lock);

    save_init(board_id_32() ^ 0xcafe1111, &core1_io_lock);

    touch_init();
    button_init();
    rgb_init();

    nfc_attach_i2c(I2C_PORT);
    nfc_init();
    aime_init(cdc_aime_putc);
    aime_set_mode(mai_cfg->aime.mode);
    aime_virtual_aic(mai_cfg->aime.virtual_aic);

    cli_init("mai_pico>", "\n   << Mai Pico Controller >>\n"
                          " https://github.com/whowechina\n\n");
    commands_init();

    mai_runtime.key_stuck = button_is_stuck();
}

int main(void)
{
    init();
    multicore_launch_core1(core1_loop);
    core0_loop();
    return 0;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    printf("Get from USB %d-%d\n", report_id, report_type);
    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{
    hid_proc(buffer, bufsize);
}
