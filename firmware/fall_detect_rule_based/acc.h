#ifndef _ACC_H
#define _ACC_H
// Definitions of Accelerometer functions and I2C read/write functions

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

enum fall_states {
    FALL_STATE_IDLE,
    FALL_STATE_FREE_FALL_DETECTED,
    FALL_STATE_IMPACT_DETECTED,
};

#define get_i2c_inst() (((SDA_PIN % 4) == 0)? i2c0: i2c1) // Based on chosen SDA pin, determine I2C instance

extern volatile bool fall_detected;

/**
 * @fn int reg_write(i2c_inst_t *i2c, const uint addr, const uint8_t reg, uint8_t *buf, const uint8_t nbytes)
 * @brief  Write the first `nbytes` of data from `*buf` to the slave device at I2C address `addr` starting at register `reg`. The `nbytes` are written to consecutive registers: `buf[0]` is written to `reg`, `buf[1]` is written to `reg + 1`, and so on. If a timeout occurs while writing, the accelerometer and the RP2040 will be reset.
 * @param  *i2c Pointer to the I2C instance.
 * @param addr I2C address of slave device.
 * @param reg Register on i2c slave device to start writing to.
 * @param *buf Pointer to data that will be writen to slave device.
 * @param nbytes Number of bytes from `*buf` to write.
 * @retval [negative] Error
 * @retval [positve] Number of bytes written to target I2C device.
 */
int reg_write(i2c_inst_t *i2c, const uint addr, const uint8_t reg, uint8_t *buf, const uint8_t nbytes);
/**
 * @fn int reg_read(i2c_inst_t *i2c, const uint addr, const uint8_t reg, uint8_t *buf, const uint8_t nbytes)
 * @brief  Read the first `nbytes` of data starting at register `reg` from the slave device at I2C address `addr` and store it in `*buf`. The `nbytes` are read from consecutive registers: `reg` is copied to `buf[0]`, `reg + 1` is copied to `buf[1], and so on. If a timeout occurs while reading, the accelerometer and the RP2040 will be reset.
 * @param  *i2c Pointer to the I2C instance.
 * @param addr I2C address of slave device.
 * @param reg Register on i2c slave device to start reading from.
 * @param *buf Pointer to buffer that will have data read into.
 * @param nbytes Number of bytes starting at `reg` to read.
 * @retval [negative] Error
 * @retval [positve] Number of bytes read from target I2C device.
 */
int reg_read(i2c_inst_t *i2c, const uint addr, const uint8_t reg, uint8_t *buf, const uint8_t nbytes);
/**
 * @fn void update_motion_state()
 * @brief Calculate the average variance in acceleration magnitude since the last call to this function, and based on the value, set the motion bits in `cfg.bit_field` to be either not moving, walking, or running. This function will also reset `var_mag_sum`, setting it to the average variance in acceleration magnitude, and setting `n_var_mag_samples` equal to 1.
 */
int acc_int_reset_ref();
int ext_reg_write(uint8_t *reg, uint8_t *data);
void update_motion_state();
int16_t get_temp();
void acc_reset();
int acc_init();
void acc_deinit();
int acc_test_poll();
void update_fall_detected_flag();
bool clear_fall_detected_flag();

void handle_acc_int1_signal();

void handle_acc_int2_signal(); 

#endif