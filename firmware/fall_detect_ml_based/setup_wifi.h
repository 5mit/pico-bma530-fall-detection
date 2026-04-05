#ifndef PICO_TEST_SETUP_WIFI_H
#define PICO_TEST_SETUP_WIFI_H

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwipopts.h"
#include "common.h"

#define LINK_TIMEOUT 10 // Seconds until connection attempt times out and is abandoned when Wi-Fi Tag is connecting to network

int setup(uint32_t country, const char *ssid, const char *pass, uint32_t auth, const char *hostname, ip_addr_t *ip, ip_addr_t *mask, ip_addr_t *gw);
int setup_default(uint32_t country, const char *ssid, const char *pass, uint32_t auth, const char *hostname);
int init_arch_sta(uint32_t country);

#endif
