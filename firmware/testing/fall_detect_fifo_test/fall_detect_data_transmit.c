#include <hardware/sync.h>
#include <hardware/timer.h>
#include <pico/time.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hardware/watchdog.h"
#include "acc.h"
#include "common.h"
#include "pico/sync.h"

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
    sem_init(&sem, 0, 1);
    int err = 0;
    bool blue_led_state = false;
    uint8_t fifo_buff[2048];
    int fifo_level = 0;
    memset(fifo_buff, 0, sizeof(fifo_buff));
    uint32_t prev_time = time_us_32();
    while (true) {
        watchdog_update(); 
        sleep_ms_or_until_sem(1000);
        //uint32_t time = time_us_32();
        //printf("dt = %u", time - prev_time);
        //prev_time = time;
        if(free_fall_detected) {
            uint32_t irq = save_and_disable_interrupts();
            uint64_t mcu_time = time_us_64(), sensor_time = acc_get_time();
            uint64_t dt = clamp_sub_u64(mcu_time, sensor_time);
            uint64_t ff_time_sensor =  (free_fall_time - dt) / 312.5;
            printf("estimated free fall sensor time (us): %llu\ndt: %llu\nmcu:%llu\tacc:%llu\n", ff_time_sensor, dt, mcu_time, sensor_time);
            if (fifo_level < 0) {
                acc_reset();
                sleep_ms(200);
                acc_init();
            }
            uint8_t *fifo_ptr = fifo_buff;
            for (size_t i = 0; i < (sizeof(fifo_buff) / 512); ++i) {
                if ((fifo_level = acc_get_fifo(fifo_ptr)) < 0) break;
                fifo_ptr += ((fifo_level / 10) * 10);
                sleep_ms(500);
            }
            if (fifo_level < 0) {
                acc_reset();
                sleep_ms(200);
                acc_init();
            }
            else {
                fifo_level = fifo_ptr - fifo_buff;
                debug_print_fifo(fifo_buff, fifo_level);
                printf("fifo level %d\n", fifo_level);
                clear_leds();
                gpio_put(BLUE_LED, blue_led_state = !blue_led_state);
                printf("debug print done\n");
                if (!clear_fall_detected_flag()) {
                    acc_reset();
                    sleep_ms(200);
                    acc_init();
                }
                memset(fifo_buff, 0, sizeof(fifo_buff));
                printf("clear fall detected flag\n");
                restore_interrupts(irq);
            }

        }
        /*
        err = acc_test_poll();
        //if (err == 0)
            //err = acc_int_reset_ref();
        if (err != 0) {
            acc_reset();
            sleep_ms(200);
            acc_init();
        }
        */
    }
}
