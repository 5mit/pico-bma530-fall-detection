#include <cyw43.h>
#include <cyw43_country.h>
#include <cyw43_ll.h>
#include <hardware/sync.h>
#include <hardware/timer.h>
#include <pico/time.h>
#include "pico/cyw43_arch.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hardware/watchdog.h"
#include "acc.h"
#include "common.h"
#include "setup_wifi.h"
#include "pico/sync.h"
#include "hardware/gpio.h"
#include "tcp.h"

void reset() {
    clear_leds();
    sleep_ms(100);
    sys_reset();
}

int main() {   
    watchdog_enable(WATCHDOG_DELAY_MS, false);
#ifndef  NDEBUG
    stdio_init_all();
#endif
    gpio_init(ACC_PWR_CTRL_PIN);
    gpio_set_dir(ACC_PWR_CTRL_PIN, GPIO_OUT);
    gpio_put(ACC_PWR_CTRL_PIN, ACC_PWR_OFF);
    init_leds();
    set_single_led(RED_LED);
    // Try 3 times to connect to wifi
    int status = CYW43_LINK_DOWN;
    for (size_t i = 0; i < 3; ++i) {
        if((status = setup_default(CYW43_COUNTRY_CANADA, WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA3_WPA2_AES_PSK, "client-tag")) == CYW43_LINK_UP) {
            break;
        }
        cyw43_arch_deinit();
        watchdog_update();
    }
    if (status != CYW43_LINK_UP) {
        reset();
    }

    // Init BMA530 accelerometer 
    if(acc_init()) {
        reset();
    }

    set_single_led(GREEN_LED);
    sem_init(&sem, 0, 1);
    bool blue_led_state = false;

    connectionState cs; // Connection state struct for tcp exchange
    cs.init = false;
    ip4_addr_t server_ip;
    ip4addr_aton(SERVER_IP, &server_ip);
    char recv_buff[RECV_BUFF_SIZE]; // Holds server response data
    uint8_t fall_window[FALL_WINDOW_SIZE]; // Fall window header + fall window data (fifo_buff)
    uint8_t* fall_window_header = fall_window, *fifo_buff = fall_window + FALL_WINDOW_HEADER_SIZE;
    int fifo_level = 0;
    memset(fall_window, 0, sizeof(fall_window));
    while (true) {
        watchdog_update(); 
        sleep_ms_or_until_sem(1000);
        if(free_fall_detected || true) {
            uint32_t irq = save_and_disable_interrupts();
            uint64_t ff_time = free_fall_time;
            restore_interrupts(irq);
            if((fifo_level = acc_get_fall_window(fifo_buff, sizeof(fall_window) - FALL_WINDOW_HEADER_SIZE)) <= 0) {
                debug_printf("Error reading fifo\n");
                watchdog_update(); 
                acc_reset();
                sleep_ms(200);
                acc_init();
                continue;
            }
            // Create Header data
            uint8_t *ptr = fall_window_header;
            memcpy(ptr, (uint8_t*)&fifo_level, sizeof(fifo_level)); // fifo level
            ptr += sizeof(fifo_level);
            memset(ptr, 0xFF, 4);   // 4 bytes of deadspace
            ptr += 4;
            memcpy(ptr, (uint8_t*)&ff_time, sizeof(ff_time));
            memset(ptr + sizeof(ff_time), 0xFF, 4);   // 4 bytes of deadspace
#ifndef NDEBUG
            debug_print_fifo(fifo_buff + sizeof(fifo_level), fifo_level); // free fall mcu detect time
#endif
            printf("fifo level %d\n", fifo_level);
            clear_leds();
            gpio_put(BLUE_LED, blue_led_state = !blue_led_state);
            if (tcp_message_exchange(&cs, &server_ip, SERVER_PORT, (char *)fall_window, recv_buff) != ERR_OK) {
                reset();
            }
            if (!clear_fall_detected_flag()) {
                acc_reset();
                sleep_ms(200);
                acc_init();
            }
            memset(fall_window, 0, sizeof(fall_window)); // clean up buffer for easier analysis if needed
        }
    }
}
