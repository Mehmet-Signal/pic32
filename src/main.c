/*******************************************************************************
  Main Source File

  Company:
    Microchip Technology Inc.

  File Name:
    main.c

  Summary:
    This file contains the "main" function for a project.

  Description:
    This file contains the "main" function for a project.  The
    "main" function calls the "SYS_Initialize" function to initialize the state
    machines of all modules in the system
 *******************************************************************************/

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include <stddef.h>                     // Defines NULL
#include <stdbool.h>                    // Defines true
#include <stdlib.h>                     // Defines EXIT_FAILURE
#include <stdint.h>
#include "definitions.h"                // SYS function prototypes
#include "i2c_driver.h"
#include "si5351.h"

// *****************************************************************************
// *****************************************************************************
// Section: Main Entry Point
// *****************************************************************************
// *****************************************************************************

static volatile uint32_t timer_tick = 0; /* Her timer kesmesinde artar */
static Si5351Dev si5351;                /* Si5351 sürücü yap?s?        */

static void delay_ms(uint32_t ms)
{
    uint32_t start = timer_tick;
    while ((timer_tick - start) < ms);
}
// 1. Kesme an?nda çal??acak fonksiyonun (Callback)

unsigned int test;

static bool si5351_setup(void)
{
    si5351_init_dev(&si5351, SI5351_BUS_BASE_ADDR);

    bool found = si5351_init(&si5351,
                             SI5351_CRYSTAL_LOAD_8PF,
                             25000000UL,
                             0);
    if (!found)
        return false;

    /*
     * CLK0 -> 24 MHz -> MCU_CLK (PIC32MZ OSC1 pinine gider)
     * 24 MHz * 100 = 2.400.000.000
     */
    si5351_set_freq(&si5351, 2400000000ULL, SI5351_CLK0);
    si5351_drive_strength(&si5351, SI5351_CLK0, SI5351_DRIVE_8MA);

    /*
     * CLK2 -> 148.5 MHz -> HDMI Clock
     * 148.5 MHz * 100 = 14.850.000.000
     *
     * NOT: 148.5 MHz > 100 MHz oldu?u için PLLB'yi kullanmal?,
     * CLK0 ile ayn? PLL'i payla?mamal?.
     */
    si5351_set_ms_source(&si5351, SI5351_CLK2, SI5351_PLLB);
    si5351_set_freq(&si5351, 14850000000ULL, SI5351_CLK2);
    si5351_drive_strength(&si5351, SI5351_CLK2, SI5351_DRIVE_8MA);

    return true;
}

void BenimTimerKesmem(uint32_t status, uintptr_t context)
{
    // LED'i tersle (Toggle)
    // GPIO_PinToggle fonksiyonunu veya kendi tan?mlad???n makroyu kullan
    test++;
    if (test > 5)
    {
        LED_IO1_Toggle();
        LED_IO2_Toggle();
        test = 0;
    }
}

int main(void)
{
    /* 1. Ad?m: Harmony ba?lat (FRC ile çal???r) */
    SYS_Initialize(NULL);

    TMR2_CallbackRegister(BenimTimerKesmem, 0);
    TMR2_Start();

    LED_IO1_Clear();
    LED_IO2_Clear();

    /* 2. Ad?m: I2C ve Si5351 ba?lat */
    i2c_master_init();
    delay_ms(100);

    if (!si5351_setup())
    {
        LED_IO1_Toggle();
    }

    /* 3. Ad?m: Si5351 CLK0 stabil olduktan sonra POSC'a geç */
    delay_ms(10); /* CLK0 stabil olsun */

    /*
     * POSC geçi?i - OSCCON register?na yaz
     * Bu i?lemi Harmony otomatik yap?yorsa bu sat?rlar? silmen gerekebilir.
     * SYS_Initialize içinde zaten yap?l?yorsa tekrar yapma.
     */
    SYSKEY = 0xAA996655;  /* Unlock sequence 1 */
    SYSKEY = 0x556699AA;  /* Unlock sequence 2 */
    OSCCONbits.NOSC = 0b011; /* PRIPLL seç */
    OSCCONbits.OSWEN = 1;    /* Geçi?i ba?lat */
    SYSKEY = 0x00000000;     /* Lock */

    /* Geçi? tamamlanana kadar bekle */
    while (OSCCONbits.OSWEN);

    /* 4. Ad?m: Ba?ar?l? */
    LED_IO1_Set();
    LED_IO2_Clear();

    while (true)
    {
        SYS_Tasks();
    }

    return EXIT_FAILURE;}


/*******************************************************************************
 End of File
 */

