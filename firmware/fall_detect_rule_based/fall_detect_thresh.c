#include <pico/time.h>
#include <stdio.h>
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "acc.h"
#include "common.h"



int main()
{   
    watchdog_enable(WATCHDOG_DELAY_MS, false);
//#ifndef  NDEBUG
    stdio_init_all();
//#endif
    gpio_init(ACC_PWR_CTRL_PIN);
    gpio_set_dir(ACC_PWR_CTRL_PIN, GPIO_OUT);
    gpio_put(ACC_PWR_CTRL_PIN, ACC_PWR_OFF);
    init_leds();
    set_single_led(RED_LED);
    if(acc_init()) {
        clear_leds();
        sleep_ms(100);
        sys_reset();
    }
    set_single_led(GREEN_LED);
    int err = 0;
    bool blue_led_state = false;
    while (true) {
        watchdog_update(); 
        sleep_ms(1000);
        if(fall_detected) {
            clear_leds();
            gpio_put(BLUE_LED, blue_led_state = !blue_led_state); 
            if (!clear_fall_detected_flag()) {
                acc_reset();
                sleep_ms(200);
                acc_init();
            }
        }
        err = acc_test_poll();
        //if (err == 0)
            //err = acc_int_reset_ref();
        if (err != 0) {
            acc_reset();
            sleep_ms(200);
            acc_init();
        }
    }
}
