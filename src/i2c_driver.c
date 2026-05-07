#include <xc-pic32m.h>
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
    I2C1BRG = 1000;
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