#include <xc-pic32m.h>
#include "stdio.h"
#include "definitions.h"
#include "i2c_driver.h"

#define I2C_TIMEOUT_COUNT   10000UL

static uint8_t i2c_wait(void)
{
    uint32_t timeout = I2C_TIMEOUT_COUNT;
    while((I2C1CON & 0x1F) || (I2C1STATbits.TRSTAT))
    {
        if(--timeout == 0) return 1;
    }
    return 0;
}

void i2c_master_init(void){
    /* PBCLK = 100 MHz, F_SCL = 400 kHz
     * BRG = (100.000.000 / (2 * 400.000)) - 2 = 123 */
    I2C1BRG = 123;
    I2C1CONbits.ON = 1;
}

uint8_t i2c_master_start(void)
{
    uint32_t timeout = I2C_TIMEOUT_COUNT;    
    if(i2c_wait()) return 1;
    
    I2C1CONbits.SEN = 1;
    
    
    
    while(I2C1CONbits.SEN){
        if(--timeout == 0) return 1;
    }
    return 0;
}

uint8_t i2c_repeated_start(void)
{
    uint32_t timeout = I2C_TIMEOUT_COUNT;    
    
    if(i2c_wait() ) 
        return 1;
    
    I2C1CONbits.RSEN = 1;
    
    while(I2C1CONbits.RSEN)
    {
        if(--timeout == 0) return 1;
    }
    return 0;
}


uint8_t i2c_stop(void){
    uint32_t timeout = I2C_TIMEOUT_COUNT;
    if(i2c_wait()) 
        return 1; 
    
    I2C1CONbits.PEN = 1;
    
    while(I2C1CONbits.PEN)
    {
        if(--timeout == 0) return 1;
    }
    return 0;
}

 uint8_t i2c_master_send(unsigned char data)
{
    uint32_t timeout = I2C_TIMEOUT_COUNT;
    if(i2c_wait())
        return 1;
    
    I2C1TRN = data;
    
    while(I2C1STATbits.TRSTAT)
    {
        if(--timeout == 0) 
            return 1;
    }
    /* ACKSTAT: 0 = ACK alindi, 1 = NACK alindi */    
    return I2C1STATbits.ACKSTAT;
}

 unsigned char i2c_master_recv(uint8_t send_nack)
{
    uint32_t timeout = I2C_TIMEOUT_COUNT;

    if (i2c_wait()) return 0xFF;

    I2C1CONbits.RCEN = 1;  /* Al?m modunu ba?lat */

    while (!I2C1STATbits.RBF)  /* Byte gelene kadar bekle */
    {
        if (--timeout == 0) return 0xFF;
    }

    uint8_t data = (uint8_t)I2C1RCV;

    /* ACK veya NACK gönder */
    if (i2c_wait()) return data;
    I2C1CONbits.ACKDT = send_nack ? 1 : 0; /* 1=NACK, 0=ACK */
    I2C1CONbits.ACKEN = 1;

    timeout = I2C_TIMEOUT_COUNT;
    while (I2C1CONbits.ACKEN)
    {
        if (--timeout == 0) break;
    }

    return data;
}

uint8_t i2c_read_reg(uint8_t dev_addr, uint8_t reg_addr)
{
    uint8_t data = 0xFF;

    /* Asama 1: Register adresini yaz */
    if (i2c_master_start())                  { return 0xFF; }
    if (i2c_master_send(dev_addr << 1)) { i2c_stop(); return 0xFF; } /* Write */
    if (i2c_master_send(reg_addr))      { i2c_stop(); return 0xFF; }

    /* Asama 2: Veriyi oku */
    if (i2c_repeated_start())         { i2c_stop(); return 0xFF; }
    if (i2c_master_send((dev_addr << 1) | 0x01)) /* Read biti set */
                                      { i2c_stop(); return 0xFF; }

    data = i2c_master_recv(1); /* 1 = NACK gonder (son ve tek byte) */

    i2c_stop();

    return data;
}

uint8_t i2c_ping(uint8_t dev_addr)
{
    uint8_t ret;
    if (i2c_master_start()) return 1;
    ret = i2c_master_send((unsigned char)(dev_addr << 1)); /* 0=ACK=mevcut */
    i2c_stop();
    return ret;
}
 
/* -------------------------------------------------------------------------
 * i2c_write_reg
 * Tek bir register'a tek byte yazar.
 * Dönü?: 0 = ba?ar?, 1 = hata
 * ------------------------------------------------------------------------- */
uint8_t i2c_write_reg(uint8_t dev_addr, uint8_t reg_addr, uint8_t data)
{
    if (i2c_master_start())                              return 1;
    if (i2c_master_send((unsigned char)(dev_addr << 1))) { i2c_stop(); return 1; }
    if (i2c_master_send(reg_addr))                       { i2c_stop(); return 1; }
    if (i2c_master_send(data))                           { i2c_stop(); return 1; }
    return i2c_stop();
}
 
/* -------------------------------------------------------------------------
 * i2c_write_bulk
 * Ba?lang?ç register adresinden itibaren len byte'l?k diziyi yazar.
 * SiI9022 gibi auto-increment destekleyen cihazlarda video timing
 * gibi ard???k register bloklar?n? tek seferde yazmak için kullan?l?r.
 * Dönü?: 0 = ba?ar?, 1 = hata
 * ------------------------------------------------------------------------- */
uint8_t i2c_write_bulk(uint8_t dev_addr, uint8_t reg_addr,
                       uint8_t *data, uint8_t len)
{
    uint8_t i;
    if (i2c_master_start())                              return 1;
    if (i2c_master_send((unsigned char)(dev_addr << 1))) { i2c_stop(); return 1; }
    if (i2c_master_send(reg_addr))                       { i2c_stop(); return 1; }
    for (i = 0; i < len; i++)
    {
        if (i2c_master_send(data[i]))                    { i2c_stop(); return 1; }
    }
    return i2c_stop();
}
