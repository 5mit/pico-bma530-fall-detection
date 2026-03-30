// Implementation file for BMA530 Accelerometer functions

#include <hardware/gpio.h>
#include <hardware/sync.h>
#include <hardware/timer.h>
#include <pico/sem.h>
#include <pico/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <math.h>
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "acc.h"
#include "common.h"
#include "rf_model.h"


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

volatile bool free_fall_detected = false;

uint8_t fall_window[FALL_WINDOW_SIZE];

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
int reg_write(i2c_inst_t *i2c, const uint addr, const uint8_t reg, uint8_t *buf, const size_t nbytes) {
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
int reg_read(i2c_inst_t *i2c, const uint addr, uint8_t reg, uint8_t *buf, const size_t nbytes) {
    // Check to make sure caller is asking for 1 or more bytes
    if (nbytes < 1) {
        return PICO_ERROR_INVALID_ARG;
    }
    //if (nbytes > 1) {
    //    reg |= 0x80; // Set MSB to indicate that we want to read from consecutive registers
    // }
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
    data = 0x25; //set low power mode perm //cfg.power_save_mode == 0? 0xA6 : 0x26; // Set bandwidth to 100Hz, normal filter mode, High performance mode if user configured power save mode is off
    if ((err = reg_write(i2c, ACC_ADDR, 0x31, &data, 1)) < 0) {
        return err;
    }
    data = 0x01; //  Set full scale to +/- 4g
    if ((err = reg_write(i2c, ACC_ADDR, 0x32, &data, 1)) < 0) {
        return err;
    }
    data = 0b00001111;  // Enabled FIFO on all axes
    if ((err = reg_write(i2c, ACC_ADDR, 0x41, &data, 1)) < 0) {
        return err;
    }
    data = 0b00000010;  // Old data dropped, no sensor time, half ram (512 B) for FIFO
    if ((err = reg_write(i2c, ACC_ADDR, 0x42, &data, 1)) < 0) {
        return err;
    }
    sleep_ms(50); // Only 10ms should be needed.
    watchdog_update();
    

    uint8_t multi_data[2] = {0x00, 0x00};
    uint8_t ext_reg[2] = {0x00, 0x00};

    // ** INT 1 **

    data = 0b00000010; // Enable INT1: active low and latchtriggered
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
    multi_data[0] = 0x40;//0x01;// TEMP 0x40;
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

    data =  0x00;//0b00001011;// | (uint8_t)free_fall_alarm_enabled;
    if ((err = reg_write(i2c, ACC_ADDR, 0x56, &data, 1)) < 0) { // gen 1 use 50Hz filter
    return err;
    }
    data =  0x01;//0b00001011;// | (uint8_t)free_fall_alarm_enabled;
    if ((err = reg_write(i2c, ACC_ADDR, 0x55, &data, 1)) < 0) { // Enable s gen_int1 
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
    data = 0x01;  // Reset FIFO
    if ((err = reg_write(i2c, ACC_ADDR, 0x40, &data, 1)) < 0) {
        return err;
    }
    sleep_ms(1000); // Wait for accelerometer to start up

    gpio_set_irq_enabled_with_callback(ACC_INT1_PIN, GPIO_IRQ_EDGE_FALL, true, &irq_handler);

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
    free_fall_detected = false;
    fall_state = FALL_STATE_IDLE;
    restore_interrupts(irq);
    return clear_acc_interrupt_regs();

}
    
void handle_acc_int1_signal() {
    if (fall_state == FALL_STATE_FREE_FALL_DETECTED) {
        return;
    }
    else {
        fall_state = FALL_STATE_FREE_FALL_DETECTED;
        free_fall_detected = true;
        free_fall_time = time_us_64();
        sem_release(&sem);
    }
}

int acc_get_fifo(uint8_t *buff) {
    // get fifo level
    int err = 0;
    uint16_t fifo_level = 0;

    if ((err = reg_read(i2c, ACC_ADDR, 0x22, (uint8_t*)(&fifo_level), 2)) < 0) { 
        return -1;
    } 
    if ((err = reg_read(i2c, ACC_ADDR, 0x24, buff, fifo_level)) < 0) { 
        return -2;
    }
    
    printf("fifo-level: %u\n", fifo_level);
    
    return fifo_level;
}

int acc_get_fall_window(uint8_t *fifo_buff, const size_t fifo_buff_size) {
    uint8_t *fifo_ptr = fifo_buff;
    int fifo_level = 0;
    for (size_t i = 0; i < (fifo_buff_size / 512); ++i) {
        if ((fifo_level = acc_get_fifo(fifo_ptr)) < 0) break;
        //printf("f lev: %d\n", fifo_level);
        fifo_ptr += ((fifo_level / 7) * 7); // prepare to overwrite last frame written if it is not complete (frames are 7B long)
        if (i % 2 == 1) {
            watchdog_update(); 
            sleep_ms(1500); // Wait for buffer to fill (approx 1.5 sec at 512B buffer with 7B FIFO frames and 50Hz sample rate)
            watchdog_update(); 
        }
    }
    if (fifo_level < 0) {
        acc_reset();
        sleep_ms(200);
        acc_init();
        return -1;
    }
    return fifo_ptr - fifo_buff;
}

void debug_print_fifo(uint8_t *buff, int fifo_level) {
    //uint32_t prev_time = 0;
    float t = 0;
    for (uint8_t* ptr = buff; ptr < (buff + (fifo_level)); ) {
        uint8_t header = ptr[0];
        uint8_t frame_type = (header & 0b01100000) >> 5;
        
        int16_t x = 0, y = 0, z = 0;
        ptr++;
        if (frame_type & 2) {
            x = (int16_t)((ptr[1] << 8) | ptr[0]);
            y = (int16_t)((ptr[3] << 8) | ptr[2]);
            z = (int16_t)((ptr[5] << 8) | ptr[4]);
            ptr += 6;
        }
        debug_printf("t:%f\tHeader: %X\tX: %d\t Y: %d\t Z: %d\t\t X: %f\t Y: %f\t Z: %f\n", t, header, x, y, z, (float)x * SENSITIVITY, (float)y * SENSITIVITY,(float)z * SENSITIVITY);
        t += 0.02;
    }
}

uint64_t acc_get_time() {
    uint64_t sensor_time = 0;
    if (reg_read(i2c, ACC_ADDR, 0x1F, (uint8_t*)(&sensor_time), 3) < 0) { 
        return -1;
    }
    return sensor_time * 312.5; 
}

fall_features fall_window_get_features(const uint8_t *fifo_buff, const size_t fifo_buff_size, const size_t fifo_level) {
    fall_features feats{};

    float sum_mag = 0.0f;
    float sum_abs_dev = 0.0f;
    float prev_mag = 0.0f;
    float sum_jerk = 0.0f;
    float jerk_max = 0.0f;

    int freefall_count = 0;
    int max_consecutive_ff_count = 0;
    int cur_consecutive_ff_count = 0;
    float max_mag = -1e6f;
    float min_mag = 1e6f;

    size_t sample_idx = 0;
    size_t last_ff_idx = 0;
    size_t impact_idx = 0;

    const float FREEFALL_THR = 4096.0f * 4096.0f; // tune threshold
    const size_t PRE_IMPACT_WINDOW = 10; // number of samples before impact (200ms)
    const size_t POST_IMPACT_WINDOW = 20; // number of samples after impact (400ms)
    float mags[512]; // temporary storage, adjust if fifo_level > 256

    // First pass: magnitude, max/min, freefall, jerk
    for (const uint8_t* ptr = fifo_buff; ptr < fifo_buff + fifo_level; ) {
        uint8_t header = ptr[0];
        uint8_t frame_type = (header & 0b01100000) >> 5;
        int16_t x = 0, y = 0, z = 0;
        ptr++;
        if (frame_type & 2) {
            x = (int16_t)((ptr[1] << 8) | ptr[0]);
            y = (int16_t)((ptr[3] << 8) | ptr[2]);
            z = (int16_t)((ptr[5] << 8) | ptr[4]);
            ptr += 6;
        } else {
            continue;
        }

        float mag = x*x + y*y + z*z;
        mags[sample_idx] = mag;

        sum_mag += mag;
        if (mag > max_mag) max_mag = mag;
        if (mag < min_mag) min_mag = mag;

        if (mag < FREEFALL_THR) {
            freefall_count++;
            if (sample_idx - last_ff_idx == 1) {
                cur_consecutive_ff_count++;
            } else {
                if (cur_consecutive_ff_count > max_consecutive_ff_count)
                    max_consecutive_ff_count = cur_consecutive_ff_count;
                
                cur_consecutive_ff_count = 1;
            }
            last_ff_idx = sample_idx; 
        }

        if (sample_idx > 0) {
            float jerk = fabsf(mag - prev_mag);
            sum_jerk += jerk;
            if (jerk > jerk_max) jerk_max = jerk;
        }
        prev_mag = mag;
        sample_idx++;
    }

    if (sample_idx == 0) return feats;

    float mean_mag = sum_mag / sample_idx;

    // Second pass: mean absolute deviation
    sum_abs_dev = 0.0f;
    for (size_t i = 0; i < sample_idx; i++) {
        sum_abs_dev += fabsf(mags[i] - mean_mag);
    }
    float mad_mag = sum_abs_dev / sample_idx;

    // Impact detection
    if (freefall_count > 0 && last_ff_idx < sample_idx) {
        float max_post_ff = mags[last_ff_idx];
        impact_idx = last_ff_idx;
        for (size_t i = last_ff_idx; i < sample_idx; i++) {
            if (mags[i] > max_post_ff) {
                max_post_ff = mags[i];
                impact_idx = i;
            }
        }
    } else {
        impact_idx = 0;
        float max_mag_val = mags[0];
        for (size_t i = 1; i < sample_idx; i++) {
            if (mags[i] > max_mag_val) {
                max_mag_val = mags[i];
                impact_idx = i;
            }
        }
    }

    // Pre/post impact windows
    size_t start_pre = (impact_idx >= PRE_IMPACT_WINDOW) ? impact_idx - PRE_IMPACT_WINDOW : 0;
    float sum_pre = 0.0f;
    for (size_t i = start_pre; i < impact_idx; i++) sum_pre += mags[i];
    float pre_mean = (impact_idx > start_pre) ? sum_pre / (impact_idx - start_pre) : mean_mag;
    sum_abs_dev = 0.0f;
    for (size_t i = start_pre; i < impact_idx; i++) sum_abs_dev += fabsf(mags[i] - pre_mean);
    float pre_mad = (impact_idx > start_pre) ? (sum_abs_dev / (impact_idx - start_pre)) : mad_mag;
    

    size_t end_post = ((impact_idx + POST_IMPACT_WINDOW) < sample_idx) ? impact_idx + POST_IMPACT_WINDOW : sample_idx;
    float sum_post = 0.0f;
    for (size_t i = impact_idx; i < end_post; i++) sum_post += mags[i];
    float post_mean = (end_post > impact_idx) ? sum_post / (end_post - impact_idx) : mean_mag;
    sum_abs_dev = 0.0f;
    for (size_t i = impact_idx; i < end_post; i++) sum_abs_dev += fabsf(mags[i] - post_mean);
    float post_mad = (end_post > impact_idx) ? sum_abs_dev / (end_post - impact_idx) : mad_mag;

    // Impact rise
    float local_min = mags[start_pre];
    for (size_t i = start_pre; i < impact_idx; i++) {
        if (mags[i] < local_min) local_min = mags[i];
    }
    float impact_rise = (impact_idx > start_pre) ? mags[impact_idx] - local_min: mags[impact_idx] - min_mag;

    // Set struct
    feats.mag_mean = mean_mag;
    feats.mag_mad = mad_mag;
    feats.mag_max = max_mag;
    feats.mag_min = min_mag;
    feats.mag_range = max_mag - min_mag;
    feats.freefall_samples = freefall_count;
    feats.max_consecutive_ff_count = max_consecutive_ff_count;
    feats.impact_mag = mags[impact_idx];
    feats.pre_impact_mean = pre_mean;
    feats.pre_impact_mad = pre_mad;
    feats.post_impact_mean = post_mean;
    feats.post_impact_mad = post_mad;
    feats.impact_rise = impact_rise;
    feats.jerk_max = jerk_max;
    feats.jerk_mean_abs = sum_jerk / (sample_idx - 1);

    return feats;
}

bool fall_occured() {
    if (!free_fall_detected) {
        return false;
    }
    int fifo_level = 0;
    if ((fifo_level = acc_get_fall_window(fall_window, sizeof(fall_window))) <= 0) {
            debug_printf("Error reading fifo\n");
            watchdog_update(); 
            acc_reset();
            sleep_ms(200);
            acc_init();
            clear_fall_detected_flag();
            return false;
    }
    debug_printf("fifo level %d\n", fifo_level);
    fall_features feats = fall_window_get_features(fall_window, sizeof(fall_window), fifo_level);
#ifndef NDEBUG
    int chars_written = snprintf(tcp_send_buff, TCP_MSG_SIZE,
         "%f, %f, %f, %f, %f, %f, %f, %f,"\
         "%f, %f, %f, %f, %f, %f, %f\n",
        feats.mag_mean,
        feats.mag_mad,
        feats.mag_max,
        feats.mag_min,
        feats.mag_range,
        (float)feats.freefall_samples,
        (float)feats.max_consecutive_ff_count,
        feats.impact_mag,
        feats.pre_impact_mean,
        feats.pre_impact_mad,
        feats.post_impact_mean,
        feats.post_impact_mad,
        feats.impact_rise,
        feats.jerk_max,
        feats.jerk_mean_abs);
#endif
    if (feats.max_consecutive_ff_count > 100) { // :onger than 2 seconds in the air, automatically consider a fall
        if (!clear_fall_detected_flag()) {
            acc_reset();
            sleep_ms(200);
            acc_init();
        }
        return true;
    }
    float x[N_FEATURES]; // or however many features your model uses
    flatten_features(&feats, x);
    Eloquent::ML::Port::RandomForest rf;
    int predicted_class = rf.predict(x);
#ifndef NDEBUG
    snprintf(tcp_send_buff + chars_written, TCP_MSG_SIZE - chars_written, "Fall occured: %d\n", predicted_class);
#endif
    if (!clear_fall_detected_flag()) {
            acc_reset();
            sleep_ms(200);
            acc_init();
    }
    // return true; // Uncomment to test detection
    return (bool)predicted_class;

}

