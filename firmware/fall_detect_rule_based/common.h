#ifndef COMMON_H
#define COMMON_H

#include "pico/stdlib.h"

// When the CMake build type is set to Debug, print statements will be built in.
#ifndef NDEBUG
#define debug_printf(...) printf(__VA_ARGS__)
#else
#define debug_printf(...) do {} while(0) // Safe no-op which is optimized out in release builds
#endif


/*
 ***************************************************************************
 *  Network Settings (Debug)
 ***************************************************************************
 */
extern char *tcp_send_buff;

#define FALL_WINDOW_SIZE 2048

#define WIFI_SSID "fall-detect-data-trans"
#define WIFI_PASS "GoodPasswordLOL"
#define SERVER_IP "192.168.0.100"
#define SERVER_PORT 3045
#define RECV_BUFF_SIZE 128

#define TCP_MSG_SIZE 1024

/*
 ***************************************************************************
 * GPIO Mapping
 ***************************************************************************
 */
// LED pins
#define RED_LED 8
#define BLUE_LED 10
#define GREEN_LED 9
// I2C Pins
#define SDA_PIN (20)
#define SCL_PIN (21)
// ACC pins
#define ACC_INT1_PIN (19)
#define ACC_INT2_PIN (18)
#define ACC_PWR_CTRL_PIN (17)
/** 
* ACC_PWR_CTRL_PIN HIGH/LOW state for ACC power on and off
*/
#define ACC_PWR_ON 0 // MOSFET drain low
#define ACC_PWR_OFF 1 // MOSFET drain high

extern volatile bool acc_irq_enabled;

/*
 ***************************************************************************
 * Free-Fall Detection Thresholds
 ***************************************************************************
*/
#define FREE_FALL_MIN_DUR (300) // milliseconds
#define FREE_FALL_MIN_DUR_LSB ((FREE_FALL_MIN_DUR * 50 / 1000)) // BMA530: (1/50)s/LSB
static_assert(FREE_FALL_MIN_DUR_LSB < (1U << 13) && FREE_FALL_MIN_DUR_LSB >= 0, "FREE_FALL_MIN_DUR_LSB must be less than 2^13 and greater than or equal to 0"); // Max duration is 2^13 - 1 LSB

#define FREE_FALL_THRES (500) // millig
#define FREE_FALL_THRES_LSB (FREE_FALL_THRES * 512 / 1000) // BMA530: (1/512)g/LSB
static_assert(FREE_FALL_THRES_LSB < (1U << 12) && FREE_FALL_THRES_LSB >= 0, "FREE_FALL_THRES_LSB must be less than 2^12 and greater than or equal to 0"); // Max thres is 2^12 - 1 LSB

#define IMPACT_MIN_DUR (30) // milliseconds
#define IMPACT_MIN_DUR_LSB ((IMPACT_MIN_DUR * 50 / 1000)) // BMA530: (1/50)s/LSB
static_assert(IMPACT_MIN_DUR_LSB < (1U << 13) && IMPACT_MIN_DUR_LSB >= 0, "IMPACT_MIN_DUR_LSB must be less than 2^13 and greater than or equal to 0"); // Max duration is 2^13 - 1 LSB

#define IMPACT_THRES (2000) // millig
#define IMPACT_THRES_LSB (IMPACT_THRES * 512 / 1000) // BMA530: (1/512)g/LSB
static_assert(IMPACT_THRES_LSB < (1U << 12) && IMPACT_THRES_LSB >= 0, "IMPACT_THRES_LSB must be less than 2^12 and greater than or equal to 0"); // Max thres is 2^12 - 1 LSB


#define FALL_QUIET_TIME_US (3ULL * 1000ULL  * 1000ULL) // 3 seconds
#define FREE_FALL_TO_IMPACT_TIMEOUT_US (2ULL * 1000ULL * 1000ULL) // 2 seconds

/** 
 ***************************************************************************
 * Inactivity Alarm
 ***************************************************************************
*/
#define INACTIVITY_ALARM_TIME_THRES 255E6 //Minimum time in microseconds that will trigger the Wi-Fi Tag's inactivity alarm if no movement is detected. 
/*
 ***************************************************************************
 * Watchdog Timer
 ***************************************************************************
 */
#define WATCHDOG_DELAY_MS 5000 
#define WATCHDOG_DELAY_US (WATCHDOG_DELAY_MS * 1000) 

static inline uint64_t clamp_sub_u64(uint64_t a, uint64_t b) {
    return (a > b) ? (a - b) : 0;
}

void init_leds();
void set_single_led(uint gpio);
void clear_leds();
void irq_setup();
void irq_handler(uint gpio, uint32_t events);
void sys_reset();
#endif