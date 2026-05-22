/**
 * i2c_driver.h - PIC32MZ DA I2C1 Master Driver
 * i2c_ping, i2c_write_reg, i2c_write_bulk eklendi (sii9022.c için gerekli)
 */

#ifndef I2C_DRIVER_H_
#define I2C_DRIVER_H_

#include <xc.h>
#include "definitions.h"

/* --------------------------------------------------------------------------
 * Mevcut fonksiyonlar
 * -------------------------------------------------------------------------- */
void          i2c_master_init(void);
uint8_t       i2c_master_start(void);
uint8_t       i2c_repeated_start(void);
uint8_t       i2c_master_send(unsigned char data);
unsigned char i2c_master_recv(uint8_t send_nack);
uint8_t       i2c_stop(void);

/* Tek register oku (register pointer + repeated start + read) */
uint8_t       i2c_read_reg(uint8_t dev_addr, uint8_t reg_addr);

/* --------------------------------------------------------------------------
 * Yeni fonksiyonlar ? sii9022.c taraf?ndan kullan?l?r
 * -------------------------------------------------------------------------- */

/**
 * @brief  I2C bus'ta cihaz adresi var m? kontrol eder.
 * @return 0 = cihaz ACK verdi (mevcut), 1 = NACK (bulunamad?)
 */
uint8_t i2c_ping(uint8_t dev_addr);

/**
 * @brief  Tek register yaz.
 * @return 0 = ba?ar?l?, 1 = hata
 */
uint8_t i2c_write_reg(uint8_t dev_addr, uint8_t reg_addr, uint8_t data);

/**
 * @brief  Ard???k N byte yaz (register adresi otomatik artar).
 * @return 0 = ba?ar?l?, 1 = hata
 */
uint8_t i2c_write_bulk(uint8_t dev_addr, uint8_t reg_addr,
                       uint8_t *data, uint8_t len);

#endif /* I2C_DRIVER_H_ */