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
#include "hardware/gpio.h"
#include "tcp.h"

void reset() {
    clear_leds();
    sleep_ms(100);
    sys_reset();
}

int main()
{   
    watchdog_enable(WATCHDOG_DELAY_MS, false);
    
    gpio_init(ACC_PWR_CTRL_PIN);
    gpio_set_dir(ACC_PWR_CTRL_PIN, GPIO_OUT);
    gpio_put(ACC_PWR_CTRL_PIN, ACC_PWR_OFF);
    init_leds();
    set_single_led(RED_LED);

#ifndef  NDEBUG
    stdio_init_all();
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
    connectionState cs; // Connection state struct for tcp exchange
    cs.init = false;
    ip4_addr_t server_ip;
    ip4addr_aton(SERVER_IP, &server_ip);
    char recv_buff[RECV_BUFF_SIZE]; // Holds server response data
    tcp_send_buff = (char*)malloc(sizeof(char) * TCP_MSG_SIZE);
    
#endif

    if(acc_init()) {
        reset();
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
            #ifndef NDEBUG
            update_debug_msg();
            if (tcp_message_exchange(&cs, &server_ip, SERVER_PORT, tcp_send_buff, recv_buff) != ERR_OK) {
                reset();
            }
            #endif
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
