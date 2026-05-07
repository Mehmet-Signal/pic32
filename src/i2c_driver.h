#ifndef I2C_DRIVER_H_
#define I2C_DRIVER_H_

#include <xc.h>
#include "definitions.h"

void i2c_master_init(void);
uint8_t i2c_master_start(void);
uint8_t i2c_repeated_start();
uint8_t i2c_master_send(unsigned char data);
unsigned char i2c_master_recv(uint8_t data);
void i2c_master_ack(int val);
uint8_t i2c_stop(void);
uint8_t i2c_read_reg(uint8_t dev_addr, uint8_t reg_addr);

#endif