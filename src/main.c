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

static bool SII9022_Start(void)
{
    SII9022_AudioCfg_t audio;
 
    /* 1. Donan?m resetle + TPI aktif + chip ID do?rula + D0 güç */
    if (!SII9022_Init())
        return false;
 
    /* 2. Video modu: 1280x720 @ 60Hz (CEA VIC=4) */
    if (!SII9022_SetVideoMode_720p60())
        return false;
 
    /* 3. HDMI cikis modu sec (DVI icin false) */
    SII9022_SetOutputMode(true);
 
    /* 4. Ses: 48 kHz stereo I2S (ses yoksa atlayabilirsin) */
    audio.enable     = true;
    audio.mute       = false;
    audio.sampleRate = 48000U;
    SII9022_ConfigureAudio(&audio);
 
    /* 5. TMDS ac ? goruntu ve ses aktarimi baslar */
    if (!SII9022_EnableTMDS(true))
        return false;
 
    return true;
}

static bool SI5351_Start(void)
{
    /* 1) Yaz?l?m yap?s?n? s?f?rla */
    si5351_init_dev(&si5351, SI5351_BUS_BASE_ADDR);   /* I2C adresi: 0x60 */
 
    /* 2) Donan?m kaydedicilerini yap?land?r
     *    - 10 pF kristal yükü  (pcb'ye göre 6/8/10 pF seç)
     *    - 25 MHz referans kristal
     *    - 0 frekans düzeltmesi (kalibrasyon sonras? de?i?tirilebilir)    */
    if (!si5351_init(&si5351,
                     SI5351_CRYSTAL_LOAD_10PF,  /* kristal yük kap. */
                     25000000UL,                /* xtal = 25 MHz    */
                     0))                        /* düzeltme = 0     */
    {
        return false;
    }
 
    /* 3) CLK0 PLL atamas?: PLLA
     *    CLK1 PLL atamas?: PLLB  (ba??ms?z frekans için ayr? PLL) */
    si5351_set_ms_source(&si5351, SI5351_CLK0, SI5351_PLLA);
    si5351_set_ms_source(&si5351, SI5351_CLK1, SI5351_PLLB);
 
    /* 4) CLK0 ? 74.25 MHz  (Hz × 100 = centi-Hz birimi)
     *    720p HDMI pixel clock                                        */
    if (si5351_set_freq(&si5351,
                        7425000000ULL,    /* 74 250 000 Hz × 100 */
                        SI5351_CLK0) != 0)
    {
        return false;
    }
 
    /* 5) CLK1 ? 25.000 MHz  (Ethernet PHY REFCLK veya genel referans) */
    if (si5351_set_freq(&si5351,
                        2500000000ULL,    /* 25 000 000 Hz × 100 */
                        SI5351_CLK1) != 0)
    {
        return false;
    }
 
    /* 6) Sürücü gücü ayarla */
    si5351_drive_strength(&si5351, SI5351_CLK0, SI5351_DRIVE_8MA);
    si5351_drive_strength(&si5351, SI5351_CLK1, SI5351_DRIVE_4MA);
 
    /* 7) Ç?k??lar? etkinle?tir */
    si5351_output_enable(&si5351, SI5351_CLK0, 1);
    si5351_output_enable(&si5351, SI5351_CLK1, 1);
 
    return true;
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
    if (!SI5351_Start())
    {
        while (1)
        {
            /* TODO: hata göstergesi */
        }
    }
    delay_ms(100);

    if (!SII9022_Start())
    {
        /* Hata: HDMI TX bulunamad?, HPD yok veya chip ID uyumsuz */
        while (1) { /* LED yak / UART'a yaz */ }
    }
    
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

