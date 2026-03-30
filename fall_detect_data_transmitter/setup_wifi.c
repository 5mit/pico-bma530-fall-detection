#include "setup_wifi.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "lwipopts.h"
#include "common.h"

// -- Helpers --
static inline bool bssid_valid(const uint8_t b[6]) {
    for (int i = 0; i < 6; ++i) if (b[i] != 0) return true;
    return false;
}

// -- Implementations --

// Wrapper function for setup which doesn't modify hostname or netif_default
int setup_default(uint32_t country, const char *ssid, const char *pass, uint32_t auth, const char *hostname) {return setup(country, ssid, pass, auth, hostname, NULL, NULL, NULL);}

// Connect pico to Wi-Fi, set the hostname, then set the netif_default ip, mask, and gateway
/* CYW43 status codes
 * CYW43_LINK_DOWN    = 0   link is down
 * CYW43_LINK_JOIN    = 1   Connected to wifi
 * CYW43_LINK_NOIP    = 2   Connected to wifi, but no IP address
 * CYW43_LINK_UP      = 3   Connected to wifi with an IP address
 * CYW43_LINK_FAIL    = -1  Connection failed
 * CYW43_LINK_NONET   = -2  No matching SSID found (could be out of range, or down)
 * CYW43_LINK_BADAUTH = -3  Authentication failure
 * */
int setup(uint32_t country, const char *ssid, const char *pass, uint32_t auth, const char *hostname, ip_addr_t *ip, ip_addr_t *mask, ip_addr_t *gw) {
    watchdog_enable(WATCHDOG_DELAY_MS, 1);

    if (init_arch_sta(country)) {
        return CYW43_LINK_FAIL;
    }
    
    if (hostname != NULL) {
        netif_set_hostname(netif_default, hostname);
    }
    debug_printf("Attempting connection\n");
    int rc = cyw43_arch_wifi_connect_async(ssid, pass, auth);
    if (rc) {
        return CYW43_LINK_FAIL;
    }

    if (ip != NULL) {
        netif_set_ipaddr(netif_default, ip);
    }
    if (mask != NULL) {
        netif_set_netmask(netif_default, mask);
    }
    if (gw != NULL) {
        netif_set_gw(netif_default, gw);
    }
    int sleep_time = 100; // ms
    int status = CYW43_LINK_UP + 1;
    int time_on_status = 0, total_time = 0;
    while (status >= 0 && status != CYW43_LINK_UP) {
        watchdog_update();
        int new_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (new_status != status) {
            status = new_status;
            debug_printf("Connect Status: %d\tTime on Prev Status %d\n", status, time_on_status);
            time_on_status = 0;
            
        }
        else if (time_on_status >= (LINK_TIMEOUT * 1000)) {
            debug_printf("Error: connection to Wi-Fi failed (timed out).\n");
            status = CYW43_LINK_FAIL; // Force failed status
        }
        sleep_ms(sleep_time);
        time_on_status += sleep_time;
        total_time += sleep_time;
        if (total_time % 1000 == 0) {
            gpio_put(GREEN_LED, (total_time / 1000) % 2 == 0);
        }

    }

    watchdog_update();
    return status;
}

int init_arch_sta(uint32_t country) {
    if(cyw43_arch_init_with_country(country)) {
        return CYW43_LINK_FAIL;
    }
    cyw43_arch_enable_sta_mode();
    return 0;
}