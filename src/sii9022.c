/**
 ******************************************************************************
 * @file    sii9022.c
 * @brief   SiI9022ACNU HDMI Transmitter Driver - Duzeltilmis Implementasyon
 * @project CAM BOX / FHT Yazilim
 * @version 2.1.0
 *
 * YAPILAN DUZELTMELER (v2.0 -> v2.1):
 *   FIX-1:  stddef.h eklendi (NULL icin)
 *   FIX-2:  AVI InfoFrame register adlari CEA-861/Harmony ile hizalandi
 *   FIX-3:  SYS_CTRL bit tanimlari Linux sii902x.c ile dogrulandi
 *   FIX-4:  Audio register cakismasi giderildi
 *   FIX-5:  HPD debounce register (0x7C=0x14) Init'e eklendi
 *   FIX-6:  I2S ses konfigurasyonu eklendi (Harmony versiyonundan port)
 *   FIX-7:  Tum degisken bildirimleri fonksiyon basina tasindi (C90/XC32)
 *   FIX-8:  ~mask icin (uint8_t) cast eklendi (sign extension onleme)
 *   FIX-9:  AVI DB2 aspect ratio: R3:R0 bitleri eklendi (0x28/0x18)
 *   FIX-10: Init sirasi calisan Harmony versiyonu ile hizalandi
 *   FIX-11: Reset sonrasi bekleme suresi 10ms -> 50ms (Harmony ile ayni)
 *
 ******************************************************************************
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>     /* [FIX-1] NULL icin gerekli */
#include "sii9022.h"
#include "i2c_driver.h"

/* NOT: <xc.h> bu dosyada GEREKMEZ - SFR erisimi yok.
 * i2c_driver.c zaten <xc.h> include eder. */

/* ==========================================================================
 * DAHILI DEGISKENLER
 * ========================================================================== */
static SII9022_State_t sii9022_state = {
    false,   /* initialized      */
    false,   /* tmds_enabled     */
    true,    /* hdmi_mode        */
    false,   /* hotplug_detected */
    {0,0,0}  /* chip_id          */
};

/* ==========================================================================
 * ONCEDEN TANIMLI VIDEO MODLARI - CEA-861 Standart Timing
 *
 * pixel_clk_10khz: pixel clock / 10kHz  (74.25 MHz = 7425)
 * v_freq_hz:       dikey frekans x100   (60.00 Hz  = 6000)
 * h_total:         toplam piksel/satir  (active + HFP + HSYNC + HBP)
 * v_total:         toplam satir/kare    (active + VFP + VSYNC + VBP)
 * vic:             CEA-861 Video Identification Code
 * ========================================================================== */

const SII9022_VideoMode_t SII9022_MODE_720P60 = {
    7425, 6000, 1650, 750, 4, "1280x720p60"
};

const SII9022_VideoMode_t SII9022_MODE_1080P60 = {
    14850, 6000, 2200, 1125, 16, "1920x1080p60"
};

const SII9022_VideoMode_t SII9022_MODE_480P60 = {
    2700, 6000, 858, 525, 3, "720x480p60"
};

const SII9022_VideoMode_t SII9022_MODE_576P50 = {
    2700, 5000, 864, 625, 18, "720x576p50"
};

/* ==========================================================================
 * TEMEL REGISTER ERISIM FONKSIYONLARI
 * ========================================================================== */

uint8_t SII9022_ReadReg(uint8_t reg)
{
    return i2c_read_reg(SII9022_I2C_ADDR, reg);
}

bool SII9022_WriteReg(uint8_t reg, uint8_t value)
{
    return (i2c_write_reg(SII9022_I2C_ADDR, reg, value) == 0);
}

bool SII9022_UpdateReg(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = SII9022_ReadReg(reg);
    /* [FIX-8] ~mask int'e promote olur, sign extension onlemek icin cast */
    uint8_t new_val = (current & (uint8_t)(~mask)) | (value & mask);
    return SII9022_WriteReg(reg, new_val);
}

/* ==========================================================================
 * HARDWARE RESET
 *
 * CAM BOX sematigi: IC_RESET Active Low.
 * LOW = reset assert, HIGH = normal calisma.
 *
 * [FIX-11] Deassert sonrasi bekleme 10ms -> 50ms.
 * ========================================================================== */
static void SII9022_HardwareReset(void)
{
    SII9022_RESET_LOW();
    SII9022_DelayMs(10);
    SII9022_RESET_HIGH();
    SII9022_DelayMs(50);    /* [FIX-11] Harmony ile ayni: 50ms */
}

/* --- TPI Mode Enable ------------------------------------------------------ */
static bool SII9022_EnableTPI(void)
{
    return SII9022_WriteReg(SII9022_REG_TPI_ENABLE, SII9022_TPI_ENABLE_VALUE);
}

/* ==========================================================================
 * CHIP ID OKUMA
 * ========================================================================== */
bool SII9022_ReadChipID(uint8_t *id0, uint8_t *id1, uint8_t *id2)
{
    uint8_t val0 = SII9022_ReadReg(SII9022_REG_DEVICE_ID);
    uint8_t val1 = SII9022_ReadReg(SII9022_REG_DEVICE_REV);
    uint8_t val2 = SII9022_ReadReg(SII9022_REG_TPI_REV);

    if (id0 != NULL) { *id0 = val0; }
    if (id1 != NULL) { *id1 = val1; }
    if (id2 != NULL) { *id2 = val2; }

    return true;
}

/* ==========================================================================
 * GUC YONETIMI
 * ========================================================================== */
bool SII9022_PowerUp(void)
{
    return SII9022_UpdateReg(SII9022_REG_POWER_STATE,
                             SII9022_PWR_MASK,
                             SII9022_PWR_D0);
}

bool SII9022_PowerDown(void)
{
    SII9022_EnableTMDS(false);
    return SII9022_UpdateReg(SII9022_REG_POWER_STATE,
                             SII9022_PWR_MASK,
                             SII9022_PWR_D2);
}

/* ==========================================================================
 * TMDS CIKIS KONTROLU
 *
 * Register 0x1A (SYS_CTRL):
 *   Bit 0: 0=DVI, 1=HDMI
 *   Bit 3: 1=AV mute active
 *   Bit 4: 0=TMDS active, 1=TMDS disabled
 *
 * [FIX-3] Bit tanimlari header makrolariyla eslesti.
 * ========================================================================== */
bool SII9022_EnableTMDS(bool enable)
{
    uint8_t val = 0;

    if (sii9022_state.hdmi_mode) {
        val |= SII9022_SYSCTRL_HDMI_MODE;   /* Bit 0 = 1: HDMI */
    }

    if (!enable) {
        val |= SII9022_SYSCTRL_TMDS_OFF;    /* Bit 4 = 1: TMDS kapali */
    }

    sii9022_state.tmds_enabled = enable;
    return SII9022_WriteReg(SII9022_REG_SYS_CTRL, val);
}

bool SII9022_SetOutputMode(bool hdmi)
{
    sii9022_state.hdmi_mode = hdmi;
    return SII9022_EnableTMDS(sii9022_state.tmds_enabled);
}

/* ==========================================================================
 * VIDEO MODU AYARLAMA
 * ========================================================================== */
bool SII9022_SetVideoMode(const SII9022_VideoMode_t *mode)
{
    /* [FIX-7] Tum degiskenler fonksiyon basinda - C90/XC32 uyumu */
    uint8_t timing_data[8];
    uint8_t bus_cfg;
    uint8_t input_fmt;
    uint8_t output_fmt;
    uint8_t avi_db1;
    uint8_t avi_db2;

    if (mode == NULL) {
        return false;
    }

    /* --- Video Timing (register 0x00-0x07, bulk write) --- */
    timing_data[0] = (uint8_t)(mode->pixel_clk_10khz        & 0xFFU);
    timing_data[1] = (uint8_t)((mode->pixel_clk_10khz >> 8) & 0xFFU);
    timing_data[2] = (uint8_t)(mode->v_freq_hz               & 0xFFU);
    timing_data[3] = (uint8_t)((mode->v_freq_hz >> 8)        & 0xFFU);
    timing_data[4] = (uint8_t)(mode->h_total                 & 0xFFU);
    timing_data[5] = (uint8_t)((mode->h_total >> 8)          & 0xFFU);
    timing_data[6] = (uint8_t)(mode->v_total                 & 0xFFU);
    timing_data[7] = (uint8_t)((mode->v_total >> 8)          & 0xFFU);

    if (i2c_write_bulk(SII9022_I2C_ADDR, SII9022_REG_VIDEO_DATA_BASE,
                       timing_data, 8) != 0) {
        return false;
    }

    /* --- Input Bus (register 0x08) ---
     * 24-bit RGB, rising edge, IDCK:TMDS = 1:1, no pixel repetition */
    bus_cfg = SII9022_INPUT_BUS_24BIT
            | SII9022_INPUT_BUS_RISING
            | SII9022_INPUT_CLK_RATIO_1X
            | SII9022_INPUT_PIXEL_REP_NONE;

    if (!SII9022_WriteReg(SII9022_REG_INPUT_BUS, bus_cfg)) {
        return false;
    }

    /* --- Input Format (register 0x09) ---
     * RGB, 8-bit per channel, auto quantization range */
    input_fmt = SII9022_INFMT_CS_RGB
              | SII9022_INFMT_RANGE_AUTO
              | SII9022_INFMT_8BIT;

    if (!SII9022_WriteReg(SII9022_REG_INPUT_FMT, input_fmt)) {
        return false;
    }

    /* --- Output Format (register 0x0A) --- */
    output_fmt = SII9022_OUTFMT_CS_RGB
               | SII9022_OUTFMT_RANGE_AUTO;

    if (!SII9022_WriteReg(SII9022_REG_OUTPUT_FMT, output_fmt)) {
        return false;
    }

    /* --- AVI InfoFrame ---
     *
     * DB1 (register 0x0C):
     *   Bit[6:5] = Y1:Y0 = color space (00=RGB)
     *   Bit[4]   = A0    = 1 (Active Format Info valid)
     *
     * [FIX-2] Harmony driver ile ayni deger: 0x10
     */
    avi_db1 = 0x10U;   /* A0=1 (AFD valid), Y1:Y0=00 (RGB) */

    if (!SII9022_WriteReg(SII9022_REG_AVI_DB1, avi_db1)) {
        return false;
    }

    /* DB2 (register 0x0D):
     *   Bit[5:4] = M1:M0 = Picture aspect ratio (01=4:3, 10=16:9)
     *   Bit[3:0] = R3:R0 = Active format AR     (1000=same as coded)
     *
     * [FIX-9] R3:R0 bitleri eklendi.
     */
    if (mode->vic == 3U || mode->vic == 18U) {
        avi_db2 = 0x18U;   /* 4:3  (M=01) + same as coded (R=1000) */
    } else {
        avi_db2 = 0x28U;   /* 16:9 (M=10) + same as coded (R=1000) */
    }

    if (!SII9022_WriteReg(SII9022_REG_AVI_DB2, avi_db2)) {
        return false;
    }

    /* DB4 (register 0x0F): VIC kodu */
    if (!SII9022_WriteReg(SII9022_REG_AVI_DB4, mode->vic)) {
        return false;
    }

    return true;
}

bool SII9022_SetVideoMode_720p60(void)
{
    return SII9022_SetVideoMode(&SII9022_MODE_720P60);
}

/* ==========================================================================
 * I2S SES KONFIGURASYONU
 *
 * [FIX-6] Harmony drv_sii9022.c _write_audio() fonksiyonundan port.
 * ========================================================================== */

/**
 * @brief  I2S sample rate -> TPI register kodu
 *         Kaynak: SiI9022A TPI Programming Reference Table 2.6
 */
static uint8_t sii9022_sample_rate_code(uint32_t rate)
{
    switch (rate) {
        case  32000U: return 0x03U;
        case  44100U: return 0x00U;
        case  48000U: return 0x02U;
        case  88200U: return 0x08U;
        case  96000U: return 0x0AU;
        case 176400U: return 0x0CU;
        case 192000U: return 0x0EU;
        default:      return 0x02U;  /* 48 kHz fallback */
    }
}

bool SII9022_ConfigureAudio(const SII9022_AudioCfg_t *audio)
{
    uint8_t freq_code;
    uint8_t audio_if_val;

    if (audio == NULL) {
        return false;
    }

    if (!audio->enable) {
        return SII9022_WriteReg(SII9022_REG_AUDIO_IF, SII9022_AIF_MUTE);
    }

    freq_code = sii9022_sample_rate_code(audio->sampleRate);

    /* I2S Enable: SD0 pin aktif, FIFO0'a ata */
    if (!SII9022_WriteReg(SII9022_REG_I2S_ENABLE, 0x80U)) {
        return false;
    }

    /* Channel Status bytes (IEC60958) */
    if (!SII9022_WriteReg(SII9022_REG_I2S_CHSTAT0, 0x00U)) { return false; }
    if (!SII9022_WriteReg(SII9022_REG_I2S_CHSTAT1, 0x00U)) { return false; }
    if (!SII9022_WriteReg(SII9022_REG_I2S_CHSTAT2, 0x00U)) { return false; }
    if (!SII9022_WriteReg(SII9022_REG_I2S_CHSTAT3, freq_code)) { return false; }
    if (!SII9022_WriteReg(SII9022_REG_I2S_CHSTAT4, 0x00U)) { return false; }

    /* Audio Interface: I2S stereo, mute opsiyonel */
    audio_if_val = SII9022_AIF_I2S;
    if (audio->mute) {
        audio_if_val |= SII9022_AIF_MUTE;
    }

    if (!SII9022_WriteReg(SII9022_REG_AUDIO_IF, audio_if_val)) {
        return false;
    }

    return true;
}

/* ==========================================================================
 * INTERRUPT YONETIMI
 * ========================================================================== */
bool SII9022_EnableInterrupts(void)
{
    uint8_t int_mask = SII9022_INT_HPD_EVENT | SII9022_INT_RXSENSE_EVENT;
    return SII9022_WriteReg(SII9022_REG_INT_ENABLE, int_mask);
}

uint8_t SII9022_ClearInterrupts(void)
{
    uint8_t status = SII9022_ReadReg(SII9022_REG_INT_STATUS);

    /* Write-1-to-clear */
    SII9022_WriteReg(SII9022_REG_INT_STATUS, status);

    sii9022_state.hotplug_detected =
        ((status & SII9022_INT_HPD_STATE) != 0U) ? true : false;

    return status;
}

bool SII9022_GetHotPlugStatus(bool *hpd, bool *rxsense)
{
    uint8_t status = SII9022_ReadReg(SII9022_REG_INT_STATUS);

    if (hpd != NULL) {
        *hpd = ((status & SII9022_INT_HPD_STATE) != 0U) ? true : false;
    }
    if (rxsense != NULL) {
        *rxsense = ((status & SII9022_INT_RXSENSE_STATE) != 0U) ? true : false;
    }

    return true;
}

/* ==========================================================================
 * INIT - Ana Baslangic Dizisi
 *
 * [FIX-10] Sira, calisan Harmony driver (drv_sii9022.c) ile hizalandi:
 *   1. HW Reset
 *   2. I2C ping
 *   3. TPI Enable  (0xC7 = 0x00)
 *   4. Chip ID verify (0x1B == 0xB0)
 *   5. TMDS termination (0x82 = 0x25)
 *   6. HPD debounce (0x7C = 0x14)   [FIX-5]
 *   7. Power D0
 *   8. TMDS off
 *   9. Interrupt enable + clear
 * ========================================================================== */
bool SII9022_Init(void)
{
    uint8_t id0, id1, id2;

    sii9022_state.initialized = false;

    /* 1. Hardware Reset */
    SII9022_HardwareReset();

    /* 2. I2C bus'ta cihaz var mi? */
    if (i2c_ping(SII9022_I2C_ADDR) != 0) {
        return false;
    }

    /* 3. TPI Mode Enable - EN KRITIK ADIM */
    if (!SII9022_EnableTPI()) {
        return false;
    }
    SII9022_DelayMs(10);

    /* 4. Chip ID Verify */
    if (!SII9022_ReadChipID(&id0, &id1, &id2)) {
        return false;
    }
    sii9022_state.chip_id[0] = id0;
    sii9022_state.chip_id[1] = id1;
    sii9022_state.chip_id[2] = id2;

    if (id0 != SII9022_CHIP_ID_EXPECTED) {
        return false;   /* Yanlis cihaz veya I2C hatasi */
    }

    /* 5. TMDS Termination */
    if (!SII9022_WriteReg(SII9022_REG_TMDS_CTRL, SII9022_TMDS_CTRL_DEFAULT)) {
        return false;
    }

    /* 6. HPD Debounce ~64ms [FIX-5] */
    if (!SII9022_WriteReg(SII9022_REG_HPD_DEBOUNCE, SII9022_HPD_DEBOUNCE_64MS)) {
        return false;
    }

    /* 7. Power State D0 */
    if (!SII9022_PowerUp()) {
        return false;
    }
    SII9022_DelayMs(10);

    /* 8. TMDS Off (video modu yazilmadan goruntu gonderme) */
    if (!SII9022_EnableTMDS(false)) {
        return false;
    }

    /* 9. Interrupt Enable + Clear */
    SII9022_EnableInterrupts();
    SII9022_ClearInterrupts();

    sii9022_state.initialized = true;
    return true;
}

/* ==========================================================================
 * DURUM SORGULAMA
 * ========================================================================== */
const SII9022_State_t* SII9022_GetState(void)
{
    return &sii9022_state;
}