// Implementation file for BMA530 Accelerometer functions

#include <hardware/gpio.h>
#include <hardware/sync.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "acc.h"
#include "common.h"

#ifndef NDEBUG
#define SENSITIVITY_2G   0.000061035 // 63.035 micro g/LSB for 2G full scale
#define SENSITIVITY_4G   0.000122070 // 122.070 micro g/LSB for 4G full scale
#define SENSITIVITY_8G   0.000244141  // 244.141 micro g/LSB for 8G full scale
#define SENSITIVITY_16G  0.000488281  // 488.281 micro g/LSB for 16G full scale
static const float SENSITIVITY = SENSITIVITY_4G; // chosen sensitivity for accelerometer
#endif

// I2C address
static const uint8_t ACC_ADDR = 0x18; // I2C address for BMA530 accelerometer

// I2C Instance
i2c_inst_t *i2c;

// accelerometer state variables

volatile uint8_t fall_state = FALL_STATE_IDLE;
volatile uint64_t free_fall_time = 0;
volatile uint64_t fall_time = 0;

volatile bool fall_detected = false;
bool int2_high = false;

int acc_int_reset_ref() {
    int err;
    uint8_t multi_data[2] = {0x00, 0x00};
    uint8_t ext_reg[2] = {0x00, 0x00};
    // Ensure acc ref x is 0
    ext_reg[0] = 0x08;
    multi_data[0] = 0x00;
    multi_data[1] = 0x00;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }
    // Ensure acc ref y is 0
    ext_reg[0] = 0x09;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }
    // Ensure acc ref z is 0
    ext_reg[0] = 0x0A;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }
    // Ensure acc ref x is 0
    ext_reg[0] = 0x0F;
    multi_data[0] = 0x00;
    multi_data[1] = 0x00;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }
    // Ensure acc ref y is 0
    ext_reg[0] = 0x10;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }
    // Ensure acc ref z is 0
    ext_reg[0] = 0x11;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }
    return 0;
}
   

// Write 1 byte to the specified register
int reg_write(i2c_inst_t *i2c, const uint addr, const uint8_t reg, uint8_t *buf, const uint8_t nbytes) {
    uint8_t msg[nbytes + 1];

    // Check to make sure caller is sending 1 or more bytes
    if (nbytes < 1) {
        return PICO_ERROR_INVALID_ARG;
    }

    // Append register address to front of data packet
    msg[0] = reg;
    for (int i = 0; i < nbytes; i++) {
        msg[i + 1] = buf[i];
    }
    int out;
    // Write data to register(s) over I2C
    if ((out = i2c_write_timeout_per_char_us(i2c, addr, msg, (nbytes + 1), false, WATCHDOG_DELAY_US)) < 0) {
        if (out == PICO_ERROR_TIMEOUT) { //i2c_write pauses watchdog timer, so reboot is explicitly called if timeout occurred 
            sys_reset();
        }
    }
    return out;

}

// Read byte(s) from specified register. If nbytes > 1, read from consecutive registers.
int reg_read(i2c_inst_t *i2c, const uint addr, uint8_t reg, uint8_t *buf, const uint8_t nbytes) {
    // Check to make sure caller is asking for 1 or more bytes
    if (nbytes < 1) {
        return PICO_ERROR_INVALID_ARG;
    }
    if (nbytes > 1) {
        reg |= 0x80; // Set MSB to indicate that we want to read from consecutive registers
    }
    // Read data from register(s) over I2C
    int out;
    if ((out = i2c_write_timeout_per_char_us(i2c, addr, &reg, 1, true,  WATCHDOG_DELAY_US)) < 0) {
        debug_printf("ERROR: Could not write to register %2X\r\n", reg);
        if (out == PICO_ERROR_TIMEOUT) {
            sys_reset();
        }
        return out;
    }
    out = i2c_read_timeout_per_char_us(i2c, addr, buf, nbytes, false,  WATCHDOG_DELAY_US);
    if (out == PICO_ERROR_TIMEOUT) { //i2c_read pauses watchdog timer, so reboot is explicitly called if timeout occurred 
        sys_reset();
    }
    return out;
}

// Write 2 bytes to specified extended register on BMA530 Accelerometer
int ext_reg_write(uint8_t *reg, uint8_t *data) {
    int err = 0;
    if ((err = reg_write(i2c, ACC_ADDR, 0x5E, reg, 2)) < 0) { 
        return err;
    }
    if ((err = reg_write(i2c, ACC_ADDR, 0x5F, data, 2)) < 0) { 
        return err;
    }
    return 0;
}

#ifndef NDEBUG
void print_binary(uint8_t reg_addr, uint8_t val) {
    printf("reg: %X -> ", reg_addr);
    for (int i = 7; i >= 0; i--) {
        putchar((val & (1 << i)) ? '1' : '0');
    }
    printf("\n");
}
#else
#define print_binary(reg_addr, val) 
#endif

// Hold reset pin on accelerometer to perform a hard reset
void acc_reset() {
    acc_deinit();
    gpio_put(ACC_PWR_CTRL_PIN, ACC_PWR_OFF);
    sleep_ms(1000);
    gpio_put(ACC_PWR_CTRL_PIN, ACC_PWR_ON); // hold pwr line low TODO: ensure the acc is wired active high
    sleep_ms(1000);
}

// Setup and initialize i2c accelerometer
int acc_init() {
    watchdog_update();
    debug_printf("i2c setup started\n");
    int err = 0;
    // Set I2C instance
    i2c = get_i2c_inst();
   //Initialize I2C port at 100 kHz
    i2c_init(i2c, 100 * 1000);
    // Initialize I2C pins
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);

    //temp_high_alarm_enabled = cfg.bit_field & TEMP_HIGH_ALARM_ENABLED;
    //temp_low_alarm_enabled = cfg.bit_field & TEMP_LOW_ALARM_ENABLED;

    // Disables ACC interrupt handling during setup

    // Initialize GPIO for interrupt 1
    gpio_init(ACC_INT1_PIN);
    gpio_set_dir(ACC_INT1_PIN, GPIO_IN);
    gpio_pull_up(ACC_INT1_PIN);
    // Initialize GPIO for interrupt 2
    gpio_init(ACC_INT2_PIN);
    gpio_set_dir(ACC_INT2_PIN, GPIO_IN);
    gpio_pull_up(ACC_INT2_PIN);

    gpio_put(ACC_PWR_CTRL_PIN, ACC_PWR_ON); // Ensure line is held low to keep acc powered 

    sleep_ms(400); // Wait for accelerometer to power up
    watchdog_update();

    uint8_t data;
    data = 0xB6; // soft reset
    reg_write(i2c, ACC_ADDR, 0x7E, &data, 1);
    reg_read(i2c, ACC_ADDR, 0x0, &data, 1); // "This initial transaction selects 4-w SPI or I2C (NACK), while the returning value is invalid.""
    
    if ((err = reg_read(i2c, ACC_ADDR, 0x0, &data, 1)) < 0) { // "Check CHIP_ID"
        return err;
    } 
    if ((err = reg_read(i2c, ACC_ADDR, 0x2, &data, 1)) < 0) { // Check health status
        return err;
    }
    data &= 0x0F;
    if (data != 0x0F) { // Power up health not ok
        return -1;
    }
    //Configure INT2 as output
    data= 0x01;
    if((err = reg_write(i2c, ACC_ADDR, 0x35, &data, 1)) < 0) { 
        return err;
    }
    sleep_ms(50); // Only 10ms should be needed.
    // After POR, the accelerometer is enabled automatically. It needs to disabled before changing configurations.
    data = 0;
    if ((err = reg_write(i2c, ACC_ADDR, 0x30, &data, 1)) < 0) { // Disable accel before config
        return err;
    }
    data = 0x26; //set low power mode perm //cfg.power_save_mode == 0? 0xA6 : 0x26; // Set bandwidth to 100Hz, normal filter mode, High performance mode if user configured power save mode is off
    if ((err = reg_write(i2c, ACC_ADDR, 0x31, &data, 1)) < 0) {
        return err;
    }
    data = 0x01; //  Set full scale to +/- 4g
    if ((err = reg_write(i2c, ACC_ADDR, 0x32, &data, 1)) < 0) {
        return err;
    }

    sleep_ms(50); //only 10 needed.
    watchdog_update();


    uint8_t multi_data[2] = {0x00, 0x00};
    uint8_t ext_reg[2] = {0x00, 0x00};
    // ** INT 1 **

    data = 0b00000010; // Enable INT1: active low and short pulse triggered
    if ((err = reg_write(i2c, ACC_ADDR, 0x34, &data, 1)) < 0) { 
        return err;
    }
    data = 0b01000000; // map generic interrupt 1 to INT1
    if ((err = reg_write(i2c, ACC_ADDR, 0x36, &data, 1)) < 0) { 
        return err;
    }
    
    // set gen1 slope to 0.5g on all axis (256LSB * (1/512)g/LSB)
    ext_reg[0] = 0x04;
    multi_data[0] = 0b00000000;
    multi_data[1] = 0b11110000; //0b11110000 // TEMP
    multi_data[0] |= (((uint8_t)(FREE_FALL_THRES_LSB)) & 0xFF);
    multi_data[1] |= ((uint8_t)(((uint16_t)(FREE_FALL_THRES_LSB)) >> 8) & 0x0F);
    debug_printf("REG 0x04: %02X %02X\n", multi_data[0], multi_data[1]);
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }

    // Set gen1 duration to hysteresis
    ext_reg[0] = 0x05;
    multi_data[0] = 0x03;       // Hysteresis of 3 LSB (3 * (1/512)g/LSB = 5.9mg)
    multi_data[1] = 0b00010000; // Manual acc ref up & motion criterion selection for stationary state of the device
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }
    
    // Set duration 
    ext_reg[0] = 0x06;
    multi_data[0] = (((uint8_t)(FREE_FALL_MIN_DUR_LSB)) & 0xFF);
    multi_data[1] = ((uint8_t)(((uint16_t)(FREE_FALL_MIN_DUR_LSB)) >> 8) & 0b00011111);
    debug_printf("REG 0x06: %02X %02X\n", multi_data[0], multi_data[1]);
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }

    // Set gen1 quiet time to 
    ext_reg[0] = 0x07;
    multi_data[0] = 0x01;// TEMP 0x40;
    multi_data[1] = 0x00;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }
    // Ensure acc ref x is 0
    ext_reg[0] = 0x08;
    multi_data[0] = 0x00;
    multi_data[1] = 0x00;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }
    // Ensure acc ref y is 0
    ext_reg[0] = 0x09;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }
    // Ensure acc ref z is 0
    ext_reg[0] = 0x0A;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }

    // ** INT 2 **
    
    data = 0b00000010; // Enable INT2: active low and short pulse triggered
    if ((err = reg_write(i2c, ACC_ADDR, 0x35, &data, 1)) < 0) { 
        return err;
    }
    data = 0b00000010; // Map generic interrupt 2 to INT2
    if ((err = reg_write(i2c, ACC_ADDR, 0x37, &data, 1)) < 0) { 
        return err;
    }
    
    // set gen2 slope to above IMPACT_THRES_LSB on any axis (512LSB * (1/512)g/LSB)
    ext_reg[0] = 0x0B;
    multi_data[0] = 0b00000000;
    multi_data[1] = 0b11100000; //0b11110000 // TEMP
    multi_data[0] |= (((uint8_t)(IMPACT_THRES_LSB)) & 0xFF);
    multi_data[1] |= ((uint8_t)(((uint16_t)(IMPACT_THRES_LSB)) >> 8) & 0x0F);
    debug_printf("REG 0x0B: %02X %02X\n", multi_data[0], multi_data[1]);
    //*((uint16_t*)multi_data) |= FREE_FALL_THRES_LSB;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }

    // Set gen2 duration to hysteresis to update from from accel data
    ext_reg[0] = 0x0C;
    multi_data[0] = 0x03;       // Hysteresis of 3 LSB (3 * (1/512)g/LSB = 5.9mg)
    multi_data[1] = 0b00010100; // Manual acc ref up & motion criterion selection for stationary state of the device
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }

    ext_reg[0] = 0x0D;
    multi_data[0] = (((uint8_t)(IMPACT_MIN_DUR_LSB)) & 0xFF);
    multi_data[1] = ((uint8_t)(((uint16_t)(IMPACT_MIN_DUR_LSB)) >> 8) & 0b00011111);
    // Set duration to 20ms (1LSB / 50s/LSB)
    debug_printf("REG 0x0D: %02X %02X\n", multi_data[0], multi_data[1]);
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }

    // Set gen2 quiet time to 
    ext_reg[0] = 0x0E;
    multi_data[0] = 0x01; // TEMp 0x40 -> 1.28s
    multi_data[1] = 0x00;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }
    // Ensure acc ref x is 0
    ext_reg[0] = 0x0F;
    multi_data[0] = 0x00;
    multi_data[1] = 0x00;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }
    // Ensure acc ref y is 0
    ext_reg[0] = 0x10;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }
    // Ensure acc ref z is 0
    ext_reg[0] = 0x11;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
        return err;
    }

    // Enable the step counter to provide motion state detection (not moving, walking, or running)
    ext_reg[0] = 0x19;
    multi_data[0] = 0b00000000;
    multi_data[1] = 0b00010000;
    if ((err = ext_reg_write(ext_reg, multi_data))) {
            return err;
    }

    data =  0x0F;//0b00001011;// | (uint8_t)free_fall_alarm_enabled;
    if ((err = reg_write(i2c, ACC_ADDR, 0x55, &data, 1)) < 0) { // Enable step counter as well as gen_int1 and gen_int2 //TODO adapt this to if fall detection is on
    return err;
    }
    data = 0x01;
    if ((err = reg_write(i2c, ACC_ADDR, 0x54, &data, 1)) < 0) {  // Update feat eng
    return err;
    }

    data = 0x0F; // Enable accel
    if ((err = reg_write(i2c, ACC_ADDR, 0x30, &data, 1)) < 0) { 
        return err;
    }
    sleep_ms(1000); // Wait for accelerometer to start up

    gpio_set_irq_enabled_with_callback(ACC_INT1_PIN, GPIO_IRQ_EDGE_FALL, true, &irq_handler);
    gpio_set_irq_enabled(ACC_INT2_PIN, GPIO_IRQ_EDGE_FALL, true);

   return 0;
}

void acc_deinit() {
    gpio_set_irq_enabled(ACC_INT1_PIN, GPIO_IRQ_EDGE_FALL, false);
    gpio_set_irq_enabled(ACC_INT2_PIN, GPIO_IRQ_EDGE_FALL, false);
    gpio_deinit(ACC_INT1_PIN);
    gpio_deinit(ACC_INT2_PIN);
    i2c_deinit(i2c);
}

// Clear int 1 & 2 status reg (unlatches interrupt):
static inline bool clear_acc_interrupt_regs() {
     uint8_t data = 0b00001000;
    if ((reg_write(i2c, ACC_ADDR, 0x12, &data, 1)) < 0) { 
        return false;
    }
    data = 0b00010000;
    if ((reg_write(i2c, ACC_ADDR, 0x14, &data, 1)) < 0) { 
        return false;
    }
    return true;
}


int16_t get_temp() {
    uint8_t data;
    // 1K/LSB, 0 == 23C/296.15K
    if ((reg_read(i2c, ACC_ADDR, 0x1E, &data, 1) < 0) || data == 0x80) { 
        // If cannot read temperature reg or error occurs, reset and reinit acc
        acc_reset();
        acc_init();
        return 0;
    }
    int16_t temp = (int8_t)data + 23;

    return temp;
}



int acc_test_poll() {
        int err = 0;
        uint8_t data;
        watchdog_update();
        if ((err = reg_read(i2c, ACC_ADDR, 0x11, &data, 1)) < 0) { 
            return err;
        }
        if (data & 0x01) {
            uint8_t accel_data[6];
            if ((err = reg_read(i2c, ACC_ADDR, 0x18, accel_data, 6)) < 0) { 
                return err;
            }
        #ifndef NDEBUG
            int16_t x = (int16_t)((accel_data[1] << 8) | accel_data[0]);
            int16_t y = (int16_t)((accel_data[3] << 8) | accel_data[2]);
            int16_t z = (int16_t)((accel_data[5] << 8) | accel_data[4]);
            debug_printf("X: %d\t Y: %d\t Z: %d\n X: %f\t Y: %f\t Z: %f\n", x, y, z, (float)x * SENSITIVITY, (float)y * SENSITIVITY,(float)z * SENSITIVITY);
        #endif
        }
        
/*         // Read int1 status reg
        if ((err = reg_read(i2c, ACC_ADDR, 0x12, &data, 1)) < 0) { 
            return err;
        }
        debug_printf("int1 status: %02X\n", data);
        // Clear int 1 status reg:
        data = 0x08; 
        if ((err = reg_write(i2c, ACC_ADDR, 0x12, &data, 1)) < 0) { 
            return err;
        }
        // Read feat eng status
        if ((err = reg_read(i2c, ACC_ADDR, 0x51, &data, 1)) < 0) { 
            return err;
        }
         // Read int1 status reg
        if ((err = reg_read(i2c, ACC_ADDR, 0x5A, &data, 1)) < 0) { 
            return err;
        } */

        return  0;
}


bool clear_fall_detected_flag() {
    // Clear fall detected bit flag
    uint32_t irq = save_and_disable_interrupts();
    fall_detected = false;
    restore_interrupts(irq);
    return clear_acc_interrupt_regs();

}
    
void handle_acc_int1_signal() {
    free_fall_time = time_us_64();
    if (fall_state != FALL_STATE_FREE_FALL_DETECTED && clamp_sub_u64(free_fall_time, fall_time) < FALL_QUIET_TIME_US) {
        return;
    }
    else {
        fall_state = FALL_STATE_FREE_FALL_DETECTED;
        debug_printf("Free fall detected at time: %llu us\n", free_fall_time);
    }
}


void handle_acc_int2_signal() {
    uint64_t impact_time = time_us_64();
    debug_printf("Impact detected at time: %llu us\n", impact_time);
    //cfg.bit_field |= FREE_FALL_IMPACT_ALARM; 
    if (fall_state == FALL_STATE_FREE_FALL_DETECTED && clamp_sub_u64(impact_time, free_fall_time) <= FREE_FALL_TO_IMPACT_TIMEOUT_US) {
        fall_state = FALL_STATE_IMPACT_DETECTED;
        fall_detected = true;
        fall_time = impact_time;
        debug_printf("***FALL_DETECTED***");
    }
    else {
        fall_state = FALL_STATE_IDLE;
    }
}

