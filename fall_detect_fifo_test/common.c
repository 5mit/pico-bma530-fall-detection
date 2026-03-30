#include "common.h"
#include "hardware/gpio.h"
#include "acc.h"
#include "hardware/watchdog.h"
#include <hardware/timer.h>
#include <pico/sem.h>
#include <stdint.h>

volatile bool acc_irq_enabled = false;
semaphore_t sem;

void init_leds() {
    gpio_init(RED_LED);
    gpio_init(GREEN_LED);
    gpio_init(BLUE_LED);
    gpio_set_dir(RED_LED, GPIO_OUT);
    gpio_set_dir(GREEN_LED, GPIO_OUT);
    gpio_set_dir(BLUE_LED, GPIO_OUT);
}

void set_single_led(uint gpio) {
    clear_leds();
    gpio_put(gpio, true); 
}

void clear_leds() {
    gpio_put(RED_LED, false);
    gpio_put(GREEN_LED, false);
    gpio_put(BLUE_LED, false);
}

// Global handler for all IRQ events when only BLE Beaconing is enabled
void irq_handler(uint gpio, uint32_t events) {
    if (gpio == ACC_INT1_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        handle_acc_int1_signal();
    }
}

void sys_reset() {
    /*
    SYSRESETREQ: Writing 1 to bit 2 causes the SYSRESETREQ signal to the
    outer system to be asserted to request a reset. The intention is to force a large
    system reset of all major components except for debug. The C_HALT bit in the
    DHCSR is cleared as a result of the system reset requested. The debugger
    does not lose contact with the device.
    */
    acc_reset(); // Reset accelerometer 
    //*((volatile uint32_t*)(PPB_BASE + M0PLUS_AIRCR_OFFSET)) = 0x5FA0004; // Reset RP2040 (doesn't work on rp2350)
    

    // Restart pico with watchdog:
    watchdog_enable(0, 0);
    while(1) {tight_loop_contents();}
}

alarm_id_t alarm_id;
int64_t timer_callback(alarm_id_t id, void *user_data) {
    sem_release(&sem);
    return 0; // one-shot
}

void sleep_ms_or_until_sem(uint32_t ms) {
    // Ensure semaphore drained to 0
    while (sem_try_acquire(&sem)) {}
    // set one-shot timer 
    alarm_id = add_alarm_in_ms(ms, timer_callback, NULL, false);
    // block until timer releases sem or releases otherwise
    sem_acquire_blocking(&sem);
    //  cancel timer in case it wasn't the cause
    cancel_alarm(alarm_id);
}