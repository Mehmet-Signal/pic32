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

/**
 * main.c
 * SI5351A + SiI9022ACNU - Duzeltilmis versiyon
 * Hedef : PIC32MZ1025DAR176  |  XC32 v5.10  |  Harmony v3
 *
 * DUZELTMELER:
 *   - I2C1_Write / I2C1_Read / I2C1_IsBusy kaldirildi
 *     (Harmony PLIB degil, kendi i2c_driver.c kullanilir)
 *   - CORETIMER_DelayMs kaldirildi
 *     (sii9022.h icindeki CP0 tabanli delay kullanilir)
 */

#include "definitions.h"
#include "i2c_driver.h"
#include "si5351.h"
#include "sii9022.h"

/* ==========================================================================
 * SI5351A
 * ========================================================================== */
static Si5351Dev si5351;

static bool SI5351_Start(void)
{
    si5351_init_dev(&si5351, SI5351_BUS_BASE_ADDR);   /* I2C addr: 0x60 */

    if (!si5351_init(&si5351, SI5351_CRYSTAL_LOAD_10PF, 25000000UL, 0))
        return false;

    si5351_set_ms_source(&si5351, SI5351_CLK0, SI5351_PLLA);
    si5351_set_ms_source(&si5351, SI5351_CLK1, SI5351_PLLB);

    /* CLK0 = 74.25 MHz  (SiI9022A pixel clock) */
    if (si5351_set_freq(&si5351, 7425000000ULL, SI5351_CLK0) != 0)
        return false;

    /* CLK1 = 25 MHz  (referans / Ethernet PHY) */
    if (si5351_set_freq(&si5351, 2500000000ULL, SI5351_CLK1) != 0)
        return false;

    si5351_drive_strength(&si5351, SI5351_CLK0, SI5351_DRIVE_8MA);
    si5351_drive_strength(&si5351, SI5351_CLK1, SI5351_DRIVE_4MA);

    si5351_output_enable(&si5351, SI5351_CLK0, 1);
    si5351_output_enable(&si5351, SI5351_CLK1, 1);

    return true;
}

/* ==========================================================================
 * SiI9022A
 * ========================================================================== */
static bool SII9022_Start(void)
{
    SII9022_AudioCfg_t audio;

    if (!SII9022_Init())
        return false;

    if (!SII9022_SetVideoMode_720p60())
        return false;

    SII9022_SetOutputMode(true);   /* HDMI modu */

    audio.enable     = true;
    audio.mute       = false;
    audio.sampleRate = 48000U;
    SII9022_ConfigureAudio(&audio);

    if (!SII9022_EnableTMDS(true))
        return false;

    return true;
}

/* ==========================================================================
 * MAIN
 * ========================================================================== */
int main(void)
{
    /* Harmony sistem baslatma (saatler, GPIO, peripheral) */
    SYS_Initialize(NULL);

    /*
     * I2C1 baslatma.
     * i2c_driver.c icindeki i2c_master_init() direkt SFR erisimi yapar.
     * Harmony I2C PLIB (I2C1_Write/Read) KULLANILMIYOR.
     */
    i2c_master_init();

    /* 1) SI5351A: pixel clock uret */
    if (!SI5351_Start())
    {
        /* Hata: sonsuz dongu veya LED */
        while (1);
    }

    /*
     * PLL kilitlenme suresi: ~10-20ms.
     * CP0 tabanli delay (CORETIMER_DelayMs gerektirmez).
     */
    SII9022_DelayMs(20);

    /* 2) SiI9022A: HDMI link kur */
    if (!SII9022_Start())
    {
        while (1);
    }

    /* Ana dongu */
    while (1)
    {
        SYS_Tasks();
    }

    return 0;
}