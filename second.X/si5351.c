/*
 * si5351.c - Si5351 Driver for PIC32MZ (C Version)
 *
 * Orijinal C++ kodu: Jason Milldrum <milldrum@gmail.com>
 * PIC32MZ C portu v2: i2c_driver.h ile uyumlu hale getirildi
 *
 * Degisiklikler (v2):
 *   - <stdint.h>/<stdbool.h> -> "definitions.h"
 *   - i2c_hal.h -> i2c_driver.h
 *   - i2c_ping()     -> i2c_master_start() + i2c_master_send() + i2c_stop()
 *   - i2c_write_reg()  -> i2c_master_start/send/stop() kombinasyonu
 *   - i2c_write_bulk() -> i2c_master_start/send/stop() kombinasyonu
 *   - i2c_read_reg()   -> i2c_driver.c'de tanimli, dogrudan kullanildi
 *   - i2c_recv_byte()  -> i2c_master_recv()
 *   - i2c_init()       -> i2c_master_init()
 */

#include "definitions.h"
#include "si5351.h"
#include "i2c_driver.h"

/* =========================================================
 * PRIVATE (dahili) fonksiyon prototipleri
 * ========================================================= */
static uint64_t pll_calc(Si5351Dev *dev, si5351_pll_t pll,
                         uint64_t freq, Si5351RegSet *reg,
                         int32_t correction, uint8_t vcxo);

static uint64_t multisynth_calc(uint64_t freq, uint64_t pll_freq,
                                Si5351RegSet *reg);

static uint64_t multisynth67_calc(uint64_t freq, uint64_t pll_freq,
                                  Si5351RegSet *reg);

static void update_sys_status(Si5351Dev *dev, Si5351Status *status);
static void update_int_status(Si5351Dev *dev, Si5351IntStatus *int_status);
static void ms_div(Si5351Dev *dev, si5351_clock_t clk,
                   uint8_t r_div, uint8_t div_by_4);
static uint8_t select_r_div(uint64_t *freq);
static uint8_t select_r_div_ms67(uint64_t *freq);

/* =========================================================
 * DUSUK SEVIYE I2C FONKS?YONLARI
 * i2c_driver.h fonksiyonlari uzerinden calisir
 * ========================================================= */

/*
 * si5351_write()
 * Tek register yaz.
 * Protokol: [START][ADDR+W][REG][DATA][STOP]
 */
uint8_t si5351_write(Si5351Dev *dev, uint8_t addr, uint8_t data)
{
    if (i2c_master_start())                      return 1;
    if (i2c_master_send(dev->i2c_bus_addr << 1)) { i2c_stop(); return 1; }
    if (i2c_master_send(addr))                   { i2c_stop(); return 1; }
    if (i2c_master_send(data))                   { i2c_stop(); return 1; }
    i2c_stop();
    return 0;
}

/*
 * si5351_write_bulk()
 * Otomatik adres artisli coklu byte yaz.
 * Protokol: [START][ADDR+W][REG][D0][D1]...[Dn][STOP]
 */
uint8_t si5351_write_bulk(Si5351Dev *dev, uint8_t addr,
                          uint8_t bytes, uint8_t *data)
{
    uint8_t i;

    if (i2c_master_start())                      return 1;
    if (i2c_master_send(dev->i2c_bus_addr << 1)) { i2c_stop(); return 1; }
    if (i2c_master_send(addr))                   { i2c_stop(); return 1; }

    for (i = 0; i < bytes; i++)
    {
        if (i2c_master_send(data[i]))            { i2c_stop(); return 1; }
    }

    i2c_stop();
    return 0;
}

/*
 * si5351_read()
 * Tek register oku.
 * i2c_driver.c'deki i2c_read_reg() fonksiyonunu kullanir.
 * Protokol: [START][ADDR+W][REG][RSTART][ADDR+R][DATA][NACK][STOP]
 */
uint8_t si5351_read(Si5351Dev *dev, uint8_t addr)
{
    return i2c_read_reg(dev->i2c_bus_addr, addr);
}

/* =========================================================
 * PUBLIC FONKSIYONLAR
 * ========================================================= */

/*
 * si5351_init_dev()
 * Surucü yapisini varsayilan degerlerle doldurur.
 * i2c_master_init() cagrisi burada YAPILMAZ ? main'de ayri cagir.
 */
void si5351_init_dev(Si5351Dev *dev, uint8_t i2c_addr)
{
    uint8_t i;

    dev->i2c_bus_addr  = i2c_addr;
    dev->xtal_freq[0]  = SI5351_XTAL_FREQ;
    dev->xtal_freq[1]  = SI5351_XTAL_FREQ;
    dev->plla_freq     = 0;
    dev->pllb_freq     = 0;
    dev->plla_ref_osc  = SI5351_PLL_INPUT_XO;
    dev->pllb_ref_osc  = SI5351_PLL_INPUT_XO;
    dev->clkin_div     = SI5351_CLKIN_DIV_1;

    dev->ref_correction[0] = 0;
    dev->ref_correction[1] = 0;

    for (i = 0; i < 8; i++)
    {
        dev->clk_freq[i]       = 0;
        dev->pll_assignment[i] = SI5351_PLLA;
        dev->clk_first_set[i]  = false;
    }

    dev->dev_status.SYS_INIT = 0;
    dev->dev_status.LOL_B    = 0;
    dev->dev_status.LOL_A    = 0;
    dev->dev_status.LOS      = 0;
    dev->dev_status.REVID    = 0;

    dev->dev_int_status.SYS_INIT_STKY = 0;
    dev->dev_int_status.LOL_B_STKY    = 0;
    dev->dev_int_status.LOL_A_STKY    = 0;
    dev->dev_int_status.LOS_STKY      = 0;
}

/*
 * si5351_init()
 * I2C uzerinden chip'e erismeyi dener, varsa yapilandirir.
 *
 * NOT: i2c_master_init() bu fonksiyon cagrilmadan once
 *      main()'de cagrilmis olmalidir!
 *
 * Ping mekanizmasi: START + adres + STOP -> ACK gelirse cihaz var
 */
bool si5351_init(Si5351Dev *dev, uint8_t xtal_load_c,
                 uint32_t xo_freq, int32_t corr)
{
    uint8_t ping_result;

    /* Cihaz var mi? START + ADDR+W -> ACK=0 ise var */
    ping_result = i2c_master_start();
    if (ping_result == 0)
        ping_result = i2c_master_send(dev->i2c_bus_addr << 1);
    i2c_stop();

    if (ping_result == 0) /* ACK geldi = cihaz mevcut */
    {
        /* SYS_INIT biti temizlenene kadar bekle */
        uint8_t status_reg = 0;
        do {
            status_reg = si5351_read(dev, SI5351_DEVICE_STATUS);
        } while ((status_reg >> 7) == 1);

        /* Kristal yuk kapasitansini ayarla */
        si5351_write(dev, SI5351_CRYSTAL_LOAD,
                     (xtal_load_c & SI5351_CRYSTAL_LOAD_MASK) | 0x12);

        /* Referans frekansini ayarla */
        if (xo_freq != 0)
        {
            si5351_set_ref_freq(dev, xo_freq, SI5351_PLL_INPUT_XO);
            si5351_set_ref_freq(dev, xo_freq, SI5351_PLL_INPUT_CLKIN);
        }
        else
        {
            si5351_set_ref_freq(dev, SI5351_XTAL_FREQ, SI5351_PLL_INPUT_XO);
            si5351_set_ref_freq(dev, SI5351_XTAL_FREQ, SI5351_PLL_INPUT_CLKIN);
        }

        /* Kalibrasyon degerini ayarla */
        si5351_set_correction(dev, corr, SI5351_PLL_INPUT_XO);
        si5351_set_correction(dev, corr, SI5351_PLL_INPUT_CLKIN);

        /* Chip'i sifirla */
        si5351_reset(dev);

        return true;
    }

    return false; /* Cihaz bulunamadi */
}

/*
 * si5351_reset()
 * Tum CLK cikislarini kapat, PLL'leri 800 MHz'e ayarla.
 */
void si5351_reset(Si5351Dev *dev)
{
    uint8_t i;

    si5351_write(dev, 16, 0x80);
    si5351_write(dev, 17, 0x80);
    si5351_write(dev, 18, 0x80);
    si5351_write(dev, 19, 0x80);
    si5351_write(dev, 20, 0x80);
    si5351_write(dev, 21, 0x80);
    si5351_write(dev, 22, 0x80);
    si5351_write(dev, 23, 0x80);

    si5351_write(dev, 16, 0x0C);
    si5351_write(dev, 17, 0x0C);
    si5351_write(dev, 18, 0x0C);
    si5351_write(dev, 19, 0x0C);
    si5351_write(dev, 20, 0x0C);
    si5351_write(dev, 21, 0x0C);
    si5351_write(dev, 22, 0x0C);
    si5351_write(dev, 23, 0x0C);

    si5351_set_pll(dev, SI5351_PLL_FIXED, SI5351_PLLA);
    si5351_set_pll(dev, SI5351_PLL_FIXED, SI5351_PLLB);

    for (i = 0; i < 6; i++) dev->pll_assignment[i] = SI5351_PLLA;
    dev->pll_assignment[6] = SI5351_PLLB;
    dev->pll_assignment[7] = SI5351_PLLB;

    si5351_set_ms_source(dev, SI5351_CLK0, SI5351_PLLA);
    si5351_set_ms_source(dev, SI5351_CLK1, SI5351_PLLA);
    si5351_set_ms_source(dev, SI5351_CLK2, SI5351_PLLA);
    si5351_set_ms_source(dev, SI5351_CLK3, SI5351_PLLA);
    si5351_set_ms_source(dev, SI5351_CLK4, SI5351_PLLA);
    si5351_set_ms_source(dev, SI5351_CLK5, SI5351_PLLA);
    si5351_set_ms_source(dev, SI5351_CLK6, SI5351_PLLB);
    si5351_set_ms_source(dev, SI5351_CLK7, SI5351_PLLB);

    si5351_write(dev, SI5351_VXCO_PARAMETERS_LOW,  0);
    si5351_write(dev, SI5351_VXCO_PARAMETERS_MID,  0);
    si5351_write(dev, SI5351_VXCO_PARAMETERS_HIGH, 0);

    si5351_pll_reset(dev, SI5351_PLLA);
    si5351_pll_reset(dev, SI5351_PLLB);

    for (i = 0; i < 8; i++)
    {
        dev->clk_freq[i]      = 0;
        dev->clk_first_set[i] = false;
        si5351_output_enable(dev, (si5351_clock_t)i, 0);
    }
}

/*
 * si5351_set_freq()
 * Belirtilen CLK cikisinda hedef frekansi uretir.
 * freq: Hz * 100 cinsinden (10 MHz -> 1000000000ULL)
 */
uint8_t si5351_set_freq(Si5351Dev *dev, uint64_t freq, si5351_clock_t clk)
{
    Si5351RegSet ms_reg;
    uint64_t pll_freq;
    uint8_t int_mode = 0;
    uint8_t div_by_4 = 0;
    uint8_t r_div    = 0;
    uint8_t i;

    if ((uint8_t)clk <= (uint8_t)SI5351_CLK5)
    {
        if (freq > 0 && freq < SI5351_CLKOUT_MIN_FREQ * SI5351_FREQ_MULT)
            freq = SI5351_CLKOUT_MIN_FREQ * SI5351_FREQ_MULT;

        if (freq > SI5351_MULTISYNTH_MAX_FREQ * SI5351_FREQ_MULT)
            freq = SI5351_MULTISYNTH_MAX_FREQ * SI5351_FREQ_MULT;

        if (freq > (SI5351_MULTISYNTH_SHARE_MAX * SI5351_FREQ_MULT))
        {
            for (i = 0; i < 6; i++)
            {
                if (dev->clk_freq[i] > (SI5351_MULTISYNTH_SHARE_MAX * SI5351_FREQ_MULT))
                {
                    if (i != (uint8_t)clk &&
                        dev->pll_assignment[i] == dev->pll_assignment[clk])
                        return 1;
                }
            }

            if (dev->clk_first_set[(uint8_t)clk] == false)
            {
                si5351_output_enable(dev, clk, 1);
                dev->clk_first_set[(uint8_t)clk] = true;
            }

            dev->clk_freq[(uint8_t)clk] = freq;
            pll_freq = multisynth_calc(freq, 0, &ms_reg);
            si5351_set_pll(dev, pll_freq, dev->pll_assignment[clk]);

            for (i = 0; i < 6; i++)
            {
                if (dev->clk_freq[i] != 0 &&
                    dev->pll_assignment[i] == dev->pll_assignment[clk])
                {
                    Si5351RegSet temp_reg;
                    uint64_t temp_freq = dev->clk_freq[i];

                    r_div = select_r_div(&temp_freq);
                    multisynth_calc(temp_freq, pll_freq, &temp_reg);

                    div_by_4 = (temp_freq >= SI5351_MULTISYNTH_DIVBY4_FREQ * SI5351_FREQ_MULT) ? 1 : 0;
                    int_mode = div_by_4;

                    si5351_set_ms(dev, (si5351_clock_t)i, temp_reg,
                                  int_mode, r_div, div_by_4);
                }
            }

            si5351_pll_reset(dev, dev->pll_assignment[clk]);
        }
        else
        {
            dev->clk_freq[(uint8_t)clk] = freq;

            if (dev->clk_first_set[(uint8_t)clk] == false)
            {
                si5351_output_enable(dev, clk, 1);
                dev->clk_first_set[(uint8_t)clk] = true;
            }

            r_div = select_r_div(&freq);

            if (dev->pll_assignment[clk] == SI5351_PLLA)
                multisynth_calc(freq, dev->plla_freq, &ms_reg);
            else
                multisynth_calc(freq, dev->pllb_freq, &ms_reg);

            if (freq >= SI5351_MULTISYNTH_DIVBY4_FREQ * SI5351_FREQ_MULT)
            {
                div_by_4 = 1;
                int_mode = 1;
            }

            si5351_set_ms(dev, clk, ms_reg, int_mode, r_div, div_by_4);
        }

        return 0;
    }
    else
    {
        /* CLK6 ve CLK7 - sadece integer bolme */
        if (freq > 0 && freq < SI5351_CLKOUT67_MIN_FREQ * SI5351_FREQ_MULT)
            freq = SI5351_CLKOUT_MIN_FREQ * SI5351_FREQ_MULT;

        if (freq >= SI5351_MULTISYNTH_DIVBY4_FREQ * SI5351_FREQ_MULT)
            freq = SI5351_MULTISYNTH_DIVBY4_FREQ * SI5351_FREQ_MULT - 1;

        if (clk == SI5351_CLK6)
        {
            if (dev->clk_freq[7] != 0)
            {
                if (dev->pllb_freq % freq == 0)
                {
                    if ((dev->pllb_freq / freq) % 2 != 0) return 1;
                    dev->clk_freq[(uint8_t)clk] = freq;
                    r_div = select_r_div_ms67(&freq);
                    multisynth67_calc(freq, dev->pllb_freq, &ms_reg);
                }
                else return 1;
            }
            else
            {
                dev->clk_freq[(uint8_t)clk] = freq;
                r_div = select_r_div_ms67(&freq);
                pll_freq = multisynth67_calc(freq, 0, &ms_reg);
                si5351_set_pll(dev, pll_freq, SI5351_PLLB);
            }
        }
        else
        {
            if (dev->clk_freq[6] != 0)
            {
                if (dev->pllb_freq % freq == 0)
                {
                    if ((dev->pllb_freq / freq) % 2 != 0) return 1;
                    dev->clk_freq[(uint8_t)clk] = freq;
                    r_div = select_r_div_ms67(&freq);
                    multisynth67_calc(freq, dev->pllb_freq, &ms_reg);
                }
                else return 1;
            }
            else
            {
                dev->clk_freq[(uint8_t)clk] = freq;
                r_div = select_r_div_ms67(&freq);
                pll_freq = multisynth67_calc(freq, 0, &ms_reg);
                si5351_set_pll(dev, pll_freq, dev->pll_assignment[clk]);
            }
        }

        si5351_set_ms(dev, clk, ms_reg, 0, r_div, 0);
        return 0;
    }
}

/*
 * si5351_set_freq_manual()
 */
uint8_t si5351_set_freq_manual(Si5351Dev *dev, uint64_t freq,
                               uint64_t pll_freq, si5351_clock_t clk)
{
    Si5351RegSet ms_reg;
    uint8_t int_mode = 0;
    uint8_t div_by_4 = 0;
    uint8_t r_div;

    if (freq > 0 && freq < SI5351_CLKOUT_MIN_FREQ * SI5351_FREQ_MULT)
        freq = SI5351_CLKOUT_MIN_FREQ * SI5351_FREQ_MULT;
    if (freq > SI5351_CLKOUT_MAX_FREQ * SI5351_FREQ_MULT)
        freq = SI5351_CLKOUT_MAX_FREQ * SI5351_FREQ_MULT;

    dev->clk_freq[(uint8_t)clk] = freq;
    si5351_set_pll(dev, pll_freq, dev->pll_assignment[clk]);
    si5351_output_enable(dev, clk, 1);

    r_div = select_r_div(&freq);
    multisynth_calc(freq, pll_freq, &ms_reg);

    if (freq >= SI5351_MULTISYNTH_DIVBY4_FREQ * SI5351_FREQ_MULT)
    {
        div_by_4 = 1; int_mode = 1;
    }

    si5351_set_ms(dev, clk, ms_reg, int_mode, r_div, div_by_4);
    return 0;
}

/*
 * si5351_set_pll()
 */
void si5351_set_pll(Si5351Dev *dev, uint64_t pll_freq, si5351_pll_t target_pll)
{
    Si5351RegSet pll_reg;
    uint8_t params[20];
    uint8_t i = 0;
    uint8_t temp;

    if (target_pll == SI5351_PLLA)
        pll_calc(dev, SI5351_PLLA, pll_freq, &pll_reg,
                 dev->ref_correction[(uint8_t)dev->plla_ref_osc], 0);
    else
        pll_calc(dev, SI5351_PLLB, pll_freq, &pll_reg,
                 dev->ref_correction[(uint8_t)dev->pllb_ref_osc], 0);

    params[i++] = (uint8_t)((pll_reg.p3 >> 8) & 0xFF);
    params[i++] = (uint8_t)(pll_reg.p3 & 0xFF);
    params[i++] = (uint8_t)((pll_reg.p1 >> 16) & 0x03);
    params[i++] = (uint8_t)((pll_reg.p1 >> 8) & 0xFF);
    params[i++] = (uint8_t)(pll_reg.p1 & 0xFF);
    temp  = (uint8_t)((pll_reg.p3 >> 12) & 0xF0);
    temp += (uint8_t)((pll_reg.p2 >> 16) & 0x0F);
    params[i++] = temp;
    params[i++] = (uint8_t)((pll_reg.p2 >> 8) & 0xFF);
    params[i++] = (uint8_t)(pll_reg.p2 & 0xFF);

    if (target_pll == SI5351_PLLA)
    {
        si5351_write_bulk(dev, SI5351_PLLA_PARAMETERS, i, params);
        dev->plla_freq = pll_freq;
    }
    else
    {
        si5351_write_bulk(dev, SI5351_PLLB_PARAMETERS, i, params);
        dev->pllb_freq = pll_freq;
    }
}

/*
 * si5351_set_ms()
 */
void si5351_set_ms(Si5351Dev *dev, si5351_clock_t clk,
                   Si5351RegSet ms_reg, uint8_t int_mode,
                   uint8_t r_div, uint8_t div_by_4)
{
    uint8_t params[20];
    uint8_t i = 0;
    uint8_t temp;
    uint8_t reg_val;

    if ((uint8_t)clk <= (uint8_t)SI5351_CLK5)
    {
        params[i++] = (uint8_t)((ms_reg.p3 >> 8) & 0xFF);
        params[i++] = (uint8_t)(ms_reg.p3 & 0xFF);

        reg_val = si5351_read(dev, (SI5351_CLK0_PARAMETERS + 2) + (clk * 8));
        reg_val &= ~(0x03);
        params[i++] = reg_val | ((uint8_t)((ms_reg.p1 >> 16) & 0x03));

        params[i++] = (uint8_t)((ms_reg.p1 >> 8) & 0xFF);
        params[i++] = (uint8_t)(ms_reg.p1 & 0xFF);

        temp  = (uint8_t)((ms_reg.p3 >> 12) & 0xF0);
        temp += (uint8_t)((ms_reg.p2 >> 16) & 0x0F);
        params[i++] = temp;

        params[i++] = (uint8_t)((ms_reg.p2 >> 8) & 0xFF);
        params[i++] = (uint8_t)(ms_reg.p2 & 0xFF);
    }
    else
    {
        temp = (uint8_t)ms_reg.p1;
    }

    switch (clk)
    {
        case SI5351_CLK0:
            si5351_write_bulk(dev, SI5351_CLK0_PARAMETERS, i, params);
            si5351_set_int(dev, clk, int_mode);
            ms_div(dev, clk, r_div, div_by_4);
            break;
        case SI5351_CLK1:
            si5351_write_bulk(dev, SI5351_CLK1_PARAMETERS, i, params);
            si5351_set_int(dev, clk, int_mode);
            ms_div(dev, clk, r_div, div_by_4);
            break;
        case SI5351_CLK2:
            si5351_write_bulk(dev, SI5351_CLK2_PARAMETERS, i, params);
            si5351_set_int(dev, clk, int_mode);
            ms_div(dev, clk, r_div, div_by_4);
            break;
        case SI5351_CLK3:
            si5351_write_bulk(dev, SI5351_CLK3_PARAMETERS, i, params);
            si5351_set_int(dev, clk, int_mode);
            ms_div(dev, clk, r_div, div_by_4);
            break;
        case SI5351_CLK4:
            si5351_write_bulk(dev, SI5351_CLK4_PARAMETERS, i, params);
            si5351_set_int(dev, clk, int_mode);
            ms_div(dev, clk, r_div, div_by_4);
            break;
        case SI5351_CLK5:
            si5351_write_bulk(dev, SI5351_CLK5_PARAMETERS, i, params);
            si5351_set_int(dev, clk, int_mode);
            ms_div(dev, clk, r_div, div_by_4);
            break;
        case SI5351_CLK6:
            si5351_write(dev, SI5351_CLK6_PARAMETERS, temp);
            ms_div(dev, clk, r_div, div_by_4);
            break;
        case SI5351_CLK7:
            si5351_write(dev, SI5351_CLK7_PARAMETERS, temp);
            ms_div(dev, clk, r_div, div_by_4);
            break;
        default:
            break;
    }
}

void si5351_output_enable(Si5351Dev *dev, si5351_clock_t clk, uint8_t enable)
{
    uint8_t reg_val = si5351_read(dev, SI5351_OUTPUT_ENABLE_CTRL);
    if (enable == 1) reg_val &= ~(1 << (uint8_t)clk);
    else             reg_val |=  (1 << (uint8_t)clk);
    si5351_write(dev, SI5351_OUTPUT_ENABLE_CTRL, reg_val);
}

void si5351_drive_strength(Si5351Dev *dev, si5351_clock_t clk,
                           si5351_drive_t drive)
{
    uint8_t reg_val = si5351_read(dev, SI5351_CLK0_CTRL + (uint8_t)clk);
    reg_val &= ~(0x03);
    switch (drive)
    {
        case SI5351_DRIVE_2MA: reg_val |= 0x00; break;
        case SI5351_DRIVE_4MA: reg_val |= 0x01; break;
        case SI5351_DRIVE_6MA: reg_val |= 0x02; break;
        case SI5351_DRIVE_8MA: reg_val |= 0x03; break;
        default: break;
    }
    si5351_write(dev, SI5351_CLK0_CTRL + (uint8_t)clk, reg_val);
}

void si5351_update_status(Si5351Dev *dev)
{
    update_sys_status(dev, &dev->dev_status);
    update_int_status(dev, &dev->dev_int_status);
}

void si5351_set_correction(Si5351Dev *dev, int32_t corr,
                           si5351_pll_input_t ref_osc)
{
    dev->ref_correction[(uint8_t)ref_osc] = corr;
    si5351_set_pll(dev, dev->plla_freq, SI5351_PLLA);
    si5351_set_pll(dev, dev->pllb_freq, SI5351_PLLB);
}

int32_t si5351_get_correction(Si5351Dev *dev, si5351_pll_input_t ref_osc)
{
    return dev->ref_correction[(uint8_t)ref_osc];
}

void si5351_set_phase(Si5351Dev *dev, si5351_clock_t clk, uint8_t phase)
{
    si5351_write(dev, SI5351_CLK0_PHASE_OFFSET + (uint8_t)clk, phase & 0x7F);
}

void si5351_pll_reset(Si5351Dev *dev, si5351_pll_t target_pll)
{
    if (target_pll == SI5351_PLLA)
        si5351_write(dev, SI5351_PLL_RESET, SI5351_PLL_RESET_A);
    else
        si5351_write(dev, SI5351_PLL_RESET, SI5351_PLL_RESET_B);
}

void si5351_set_ms_source(Si5351Dev *dev, si5351_clock_t clk,
                          si5351_pll_t pll)
{
    uint8_t reg_val = si5351_read(dev, SI5351_CLK0_CTRL + (uint8_t)clk);
    if (pll == SI5351_PLLA) reg_val &= ~(SI5351_CLK_PLL_SELECT);
    else                    reg_val |=  (SI5351_CLK_PLL_SELECT);
    si5351_write(dev, SI5351_CLK0_CTRL + (uint8_t)clk, reg_val);
    dev->pll_assignment[(uint8_t)clk] = pll;
}

void si5351_set_int(Si5351Dev *dev, si5351_clock_t clk, uint8_t enable)
{
    uint8_t reg_val = si5351_read(dev, SI5351_CLK0_CTRL + (uint8_t)clk);
    if (enable) reg_val |=  SI5351_CLK_INTEGER_MODE;
    else        reg_val &= ~SI5351_CLK_INTEGER_MODE;
    si5351_write(dev, SI5351_CLK0_CTRL + (uint8_t)clk, reg_val);
}

void si5351_set_clock_pwr(Si5351Dev *dev, si5351_clock_t clk, uint8_t pwr)
{
    uint8_t reg_val = si5351_read(dev, SI5351_CLK0_CTRL + (uint8_t)clk);
    if (pwr) reg_val &= 0x7F;
    else     reg_val |= 0x80;
    si5351_write(dev, SI5351_CLK0_CTRL + (uint8_t)clk, reg_val);
}

void si5351_set_clock_invert(Si5351Dev *dev, si5351_clock_t clk, uint8_t inv)
{
    uint8_t reg_val = si5351_read(dev, SI5351_CLK0_CTRL + (uint8_t)clk);
    if (inv) reg_val |=  SI5351_CLK_INVERT;
    else     reg_val &= ~SI5351_CLK_INVERT;
    si5351_write(dev, SI5351_CLK0_CTRL + (uint8_t)clk, reg_val);
}

void si5351_set_clock_source(Si5351Dev *dev, si5351_clock_t clk,
                             si5351_clock_source_t src)
{
    uint8_t reg_val = si5351_read(dev, SI5351_CLK0_CTRL + (uint8_t)clk);
    reg_val &= ~SI5351_CLK_INPUT_MASK;
    switch (src)
    {
        case SI5351_CLK_SRC_XTAL:  reg_val |= SI5351_CLK_INPUT_XTAL;           break;
        case SI5351_CLK_SRC_CLKIN: reg_val |= SI5351_CLK_INPUT_CLKIN;          break;
        case SI5351_CLK_SRC_MS0:
            if (clk == SI5351_CLK0) return;
            reg_val |= SI5351_CLK_INPUT_MULTISYNTH_0_4;
            break;
        case SI5351_CLK_SRC_MS:    reg_val |= SI5351_CLK_INPUT_MULTISYNTH_N;   break;
        default: return;
    }
    si5351_write(dev, SI5351_CLK0_CTRL + (uint8_t)clk, reg_val);
}

void si5351_set_clock_disable(Si5351Dev *dev, si5351_clock_t clk,
                              si5351_clock_disable_t dis_state)
{
    uint8_t reg_val, reg;
    if      (clk <= SI5351_CLK3) reg = SI5351_CLK3_0_DISABLE_STATE;
    else if (clk <= SI5351_CLK7) reg = SI5351_CLK7_4_DISABLE_STATE;
    else return;

    reg_val = si5351_read(dev, reg);
    if (clk <= SI5351_CLK3)
    {
        reg_val &= ~(0x03 << (clk * 2));
        reg_val |=  (dis_state << (clk * 2));
    }
    else
    {
        reg_val &= ~(0x03 << ((clk - 4) * 2));
        reg_val |=  (dis_state << ((clk - 4) * 2));
    }
    si5351_write(dev, reg, reg_val);
}

void si5351_set_clock_fanout(Si5351Dev *dev, si5351_clock_fanout_t fanout,
                             uint8_t enable)
{
    uint8_t reg_val = si5351_read(dev, SI5351_FANOUT_ENABLE);
    switch (fanout)
    {
        case SI5351_FANOUT_CLKIN:
            if (enable) reg_val |=  SI5351_CLKIN_ENABLE;
            else        reg_val &= ~SI5351_CLKIN_ENABLE;
            break;
        case SI5351_FANOUT_XO:
            if (enable) reg_val |=  SI5351_XTAL_ENABLE;
            else        reg_val &= ~SI5351_XTAL_ENABLE;
            break;
        case SI5351_FANOUT_MS:
            if (enable) reg_val |=  SI5351_MULTISYNTH_ENABLE;
            else        reg_val &= ~SI5351_MULTISYNTH_ENABLE;
            break;
        default: break;
    }
    si5351_write(dev, SI5351_FANOUT_ENABLE, reg_val);
}

void si5351_set_pll_input(Si5351Dev *dev, si5351_pll_t pll,
                          si5351_pll_input_t input)
{
    uint8_t reg_val = si5351_read(dev, SI5351_PLL_INPUT_SOURCE);
    switch (pll)
    {
        case SI5351_PLLA:
            if (input == SI5351_PLL_INPUT_CLKIN)
            {
                reg_val |= SI5351_PLLA_SOURCE;
                reg_val |= dev->clkin_div;
                dev->plla_ref_osc = SI5351_PLL_INPUT_CLKIN;
            }
            else { reg_val &= ~SI5351_PLLA_SOURCE; dev->plla_ref_osc = SI5351_PLL_INPUT_XO; }
            break;
        case SI5351_PLLB:
            if (input == SI5351_PLL_INPUT_CLKIN)
            {
                reg_val |= SI5351_PLLB_SOURCE;
                reg_val |= dev->clkin_div;
                dev->pllb_ref_osc = SI5351_PLL_INPUT_CLKIN;
            }
            else { reg_val &= ~SI5351_PLLB_SOURCE; dev->pllb_ref_osc = SI5351_PLL_INPUT_XO; }
            break;
        default: return;
    }
    si5351_write(dev, SI5351_PLL_INPUT_SOURCE, reg_val);
    si5351_set_pll(dev, dev->plla_freq, SI5351_PLLA);
    si5351_set_pll(dev, dev->pllb_freq, SI5351_PLLB);
}

void si5351_set_vcxo(Si5351Dev *dev, uint64_t pll_freq, uint8_t ppm)
{
    Si5351RegSet pll_reg;
    uint64_t vcxo_param;
    uint8_t params[20];
    uint8_t i = 0;
    uint8_t temp;

    if (ppm < SI5351_VCXO_PULL_MIN) ppm = SI5351_VCXO_PULL_MIN;
    if (ppm > SI5351_VCXO_PULL_MAX) ppm = SI5351_VCXO_PULL_MAX;

    vcxo_param = pll_calc(dev, SI5351_PLLB, pll_freq, &pll_reg,
                          dev->ref_correction[(uint8_t)dev->pllb_ref_osc], 1);

    params[i++] = (uint8_t)((pll_reg.p3 >> 8) & 0xFF);
    params[i++] = (uint8_t)(pll_reg.p3 & 0xFF);
    params[i++] = (uint8_t)((pll_reg.p1 >> 16) & 0x03);
    params[i++] = (uint8_t)((pll_reg.p1 >> 8) & 0xFF);
    params[i++] = (uint8_t)(pll_reg.p1 & 0xFF);
    temp  = (uint8_t)((pll_reg.p3 >> 12) & 0xF0);
    temp += (uint8_t)((pll_reg.p2 >> 16) & 0x0F);
    params[i++] = temp;
    params[i++] = (uint8_t)((pll_reg.p2 >> 8) & 0xFF);
    params[i++] = (uint8_t)(pll_reg.p2 & 0xFF);

    si5351_write_bulk(dev, SI5351_PLLB_PARAMETERS, i, params);

    vcxo_param = ((vcxo_param * ppm * SI5351_VCXO_MARGIN) / 100ULL) / 1000000ULL;
    si5351_write(dev, SI5351_VXCO_PARAMETERS_LOW,  (uint8_t)(vcxo_param & 0xFF));
    si5351_write(dev, SI5351_VXCO_PARAMETERS_MID,  (uint8_t)((vcxo_param >> 8) & 0xFF));
    si5351_write(dev, SI5351_VXCO_PARAMETERS_HIGH, (uint8_t)((vcxo_param >> 16) & 0x3F));
}

void si5351_set_ref_freq(Si5351Dev *dev, uint32_t ref_freq,
                         si5351_pll_input_t ref_osc)
{
    if (ref_freq <= 30000000UL)
    {
        dev->xtal_freq[(uint8_t)ref_osc] = ref_freq;
        if (ref_osc == SI5351_PLL_INPUT_CLKIN) dev->clkin_div = SI5351_CLKIN_DIV_1;
    }
    else if (ref_freq <= 60000000UL)
    {
        dev->xtal_freq[(uint8_t)ref_osc] = ref_freq / 2;
        if (ref_osc == SI5351_PLL_INPUT_CLKIN) dev->clkin_div = SI5351_CLKIN_DIV_2;
    }
    else if (ref_freq <= 100000000UL)
    {
        dev->xtal_freq[(uint8_t)ref_osc] = ref_freq / 4;
        if (ref_osc == SI5351_PLL_INPUT_CLKIN) dev->clkin_div = SI5351_CLKIN_DIV_4;
    }
}

/* =========================================================
 * PRIVATE HESAPLAMA FONKSIYONLARI
 * ========================================================= */

static uint64_t pll_calc(Si5351Dev *dev, si5351_pll_t pll,
                         uint64_t freq, Si5351RegSet *reg,
                         int32_t correction, uint8_t vcxo)
{
    uint64_t ref_freq;
    uint32_t a, b, c, p1, p2, p3;
    uint64_t lltmp;

    if (pll == SI5351_PLLA)
        ref_freq = (uint64_t)dev->xtal_freq[(uint8_t)dev->plla_ref_osc] * SI5351_FREQ_MULT;
    else
        ref_freq = (uint64_t)dev->xtal_freq[(uint8_t)dev->pllb_ref_osc] * SI5351_FREQ_MULT;

    ref_freq = ref_freq + (int32_t)((((((int64_t)correction) << 31)
               / 1000000000LL) * ref_freq) >> 31);

    if (freq < SI5351_PLL_VCO_MIN * SI5351_FREQ_MULT)
        freq = SI5351_PLL_VCO_MIN * SI5351_FREQ_MULT;
    if (freq > SI5351_PLL_VCO_MAX * SI5351_FREQ_MULT)
        freq = SI5351_PLL_VCO_MAX * SI5351_FREQ_MULT;

    a = (uint32_t)(freq / ref_freq);
    if (a < SI5351_PLL_A_MIN) freq = ref_freq * SI5351_PLL_A_MIN;
    if (a > SI5351_PLL_A_MAX) freq = ref_freq * SI5351_PLL_A_MAX;

    if (vcxo)
    {
        b = (uint32_t)(((uint64_t)(freq % ref_freq) * 1000000ULL) / ref_freq);
        c = 1000000UL;
    }
    else
    {
        b = (uint32_t)(((uint64_t)(freq % ref_freq) * RFRAC_DENOM) / ref_freq);
        c = b ? (uint32_t)RFRAC_DENOM : 1;
    }

    p1 = 128 * a + ((128 * b) / c) - 512;
    p2 = 128 * b - c * ((128 * b) / c);
    p3 = c;

    lltmp  = ref_freq * b;
    do_div(lltmp, c);
    freq = lltmp + ref_freq * a;

    reg->p1 = p1; reg->p2 = p2; reg->p3 = p3;

    if (vcxo) return (uint64_t)(128 * a * 1000000ULL + b);
    else      return freq;
}

static uint64_t multisynth_calc(uint64_t freq, uint64_t pll_freq,
                                Si5351RegSet *reg)
{
    uint64_t lltmp;
    uint32_t a, b, c, p1, p2, p3;
    uint8_t divby4  = 0;
    uint8_t ret_val = 0;

    if (freq > SI5351_MULTISYNTH_MAX_FREQ * SI5351_FREQ_MULT)
        freq = SI5351_MULTISYNTH_MAX_FREQ * SI5351_FREQ_MULT;
    if (freq < SI5351_MULTISYNTH_MIN_FREQ * SI5351_FREQ_MULT)
        freq = SI5351_MULTISYNTH_MIN_FREQ * SI5351_FREQ_MULT;

    if (freq >= SI5351_MULTISYNTH_DIVBY4_FREQ * SI5351_FREQ_MULT) divby4 = 1;

    if (pll_freq == 0)
    {
        if (divby4 == 0)
        {
            lltmp = SI5351_PLL_VCO_MAX * SI5351_FREQ_MULT;
            do_div(lltmp, freq);
            if      (lltmp == 5) lltmp = 4;
            else if (lltmp == 7) lltmp = 6;
            a = (uint32_t)lltmp;
        }
        else a = 4;

        b = 0; c = 1;
        pll_freq = (uint64_t)a * freq;
    }
    else
    {
        ret_val = 1;
        a = (uint32_t)(pll_freq / freq);
        if (a < SI5351_MULTISYNTH_A_MIN) freq = pll_freq / SI5351_MULTISYNTH_A_MIN;
        if (a > SI5351_MULTISYNTH_A_MAX) freq = pll_freq / SI5351_MULTISYNTH_A_MAX;
        b = (uint32_t)((pll_freq % freq * RFRAC_DENOM) / freq);
        c = b ? (uint32_t)RFRAC_DENOM : 1;
    }

    if (divby4 == 1) { p3 = 1; p2 = 0; p1 = 0; }
    else
    {
        p1 = 128 * a + ((128 * b) / c) - 512;
        p2 = 128 * b - c * ((128 * b) / c);
        p3 = c;
    }

    reg->p1 = p1; reg->p2 = p2; reg->p3 = p3;
    return (ret_val == 0) ? pll_freq : freq;
}

static uint64_t multisynth67_calc(uint64_t freq, uint64_t pll_freq,
                                  Si5351RegSet *reg)
{
    uint32_t a;
    uint64_t lltmp;

    if (freq > SI5351_MULTISYNTH67_MAX_FREQ * SI5351_FREQ_MULT)
        freq = SI5351_MULTISYNTH67_MAX_FREQ * SI5351_FREQ_MULT;
    if (freq < SI5351_MULTISYNTH_MIN_FREQ * SI5351_FREQ_MULT)
        freq = SI5351_MULTISYNTH_MIN_FREQ * SI5351_FREQ_MULT;

    if (pll_freq == 0)
    {
        lltmp = (SI5351_PLL_VCO_MAX * SI5351_FREQ_MULT) - 100000000UL;
        do_div(lltmp, freq);
        a = (uint32_t)lltmp;
        if (a % 2 != 0) a++;
        if (a < SI5351_MULTISYNTH_A_MIN)  a = SI5351_MULTISYNTH_A_MIN;
        if (a > SI5351_MULTISYNTH67_A_MAX) a = SI5351_MULTISYNTH67_A_MAX;
        pll_freq = (uint64_t)a * freq;
        if      (pll_freq > SI5351_PLL_VCO_MAX * SI5351_FREQ_MULT) { a -= 2; pll_freq = (uint64_t)a * freq; }
        else if (pll_freq < SI5351_PLL_VCO_MIN * SI5351_FREQ_MULT) { a += 2; pll_freq = (uint64_t)a * freq; }
        reg->p1 = (uint8_t)a; reg->p2 = 0; reg->p3 = 0;
        return pll_freq;
    }
    else
    {
        if (pll_freq % freq != 0) return 0;
        a = (uint32_t)(pll_freq / freq);
        if (a < SI5351_MULTISYNTH_A_MIN || a > SI5351_MULTISYNTH67_A_MAX) return 0;
        reg->p1 = (uint8_t)a; reg->p2 = 0; reg->p3 = 0;
        return 1;
    }
}

static void update_sys_status(Si5351Dev *dev, Si5351Status *status)
{
    uint8_t reg_val = si5351_read(dev, SI5351_DEVICE_STATUS);
    status->SYS_INIT = (reg_val >> 7) & 0x01;
    status->LOL_B    = (reg_val >> 6) & 0x01;
    status->LOL_A    = (reg_val >> 5) & 0x01;
    status->LOS      = (reg_val >> 4) & 0x01;
    status->REVID    =  reg_val       & 0x03;
}

static void update_int_status(Si5351Dev *dev, Si5351IntStatus *int_status)
{
    uint8_t reg_val = si5351_read(dev, SI5351_INTERRUPT_STATUS);
    int_status->SYS_INIT_STKY = (reg_val >> 7) & 0x01;
    int_status->LOL_B_STKY    = (reg_val >> 6) & 0x01;
    int_status->LOL_A_STKY    = (reg_val >> 5) & 0x01;
    int_status->LOS_STKY      = (reg_val >> 4) & 0x01;
}

static void ms_div(Si5351Dev *dev, si5351_clock_t clk,
                   uint8_t r_div, uint8_t div_by_4)
{
    uint8_t reg_val;
    uint8_t reg_addr;

    switch (clk)
    {
        case SI5351_CLK0: reg_addr = SI5351_CLK0_PARAMETERS + 2; break;
        case SI5351_CLK1: reg_addr = SI5351_CLK1_PARAMETERS + 2; break;
        case SI5351_CLK2: reg_addr = SI5351_CLK2_PARAMETERS + 2; break;
        case SI5351_CLK3: reg_addr = SI5351_CLK3_PARAMETERS + 2; break;
        case SI5351_CLK4: reg_addr = SI5351_CLK4_PARAMETERS + 2; break;
        case SI5351_CLK5: reg_addr = SI5351_CLK5_PARAMETERS + 2; break;
        case SI5351_CLK6: reg_addr = SI5351_CLK6_7_OUTPUT_DIVIDER; break;
        case SI5351_CLK7: reg_addr = SI5351_CLK6_7_OUTPUT_DIVIDER; break;
        default: return;
    }

    reg_val = si5351_read(dev, reg_addr);

    if ((uint8_t)clk <= (uint8_t)SI5351_CLK5)
    {
        reg_val &= ~(0x7C);
        if (div_by_4 == 0) reg_val &= ~SI5351_OUTPUT_CLK_DIVBY4;
        else               reg_val |=  SI5351_OUTPUT_CLK_DIVBY4;
        reg_val |= (r_div << SI5351_OUTPUT_CLK_DIV_SHIFT);
    }
    else if (clk == SI5351_CLK6)
    {
        reg_val &= ~(0x07);
        reg_val |= r_div;
    }
    else
    {
        reg_val &= ~(0x70);
        reg_val |= (r_div << SI5351_OUTPUT_CLK_DIV_SHIFT);
    }

    si5351_write(dev, reg_addr, reg_val);
}

static uint8_t select_r_div(uint64_t *freq)
{
    uint8_t r_div = SI5351_OUTPUT_CLK_DIV_1;
    if      (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351_FREQ_MULT * 2)
        { r_div = SI5351_OUTPUT_CLK_DIV_128; *freq *= 128ULL; }
    else if (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351_FREQ_MULT * 4)
        { r_div = SI5351_OUTPUT_CLK_DIV_64;  *freq *= 64ULL; }
    else if (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351_FREQ_MULT * 8)
        { r_div = SI5351_OUTPUT_CLK_DIV_32;  *freq *= 32ULL; }
    else if (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351_FREQ_MULT * 16)
        { r_div = SI5351_OUTPUT_CLK_DIV_16;  *freq *= 16ULL; }
    else if (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351_FREQ_MULT * 32)
        { r_div = SI5351_OUTPUT_CLK_DIV_8;   *freq *= 8ULL; }
    else if (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351_FREQ_MULT * 64)
        { r_div = SI5351_OUTPUT_CLK_DIV_4;   *freq *= 4ULL; }
    else if (*freq < SI5351_CLKOUT_MIN_FREQ * SI5351_FREQ_MULT * 128)
        { r_div = SI5351_OUTPUT_CLK_DIV_2;   *freq *= 2ULL; }
    return r_div;
}

static uint8_t select_r_div_ms67(uint64_t *freq)
{
    uint8_t r_div = SI5351_OUTPUT_CLK_DIV_1;
    if      (*freq < SI5351_CLKOUT67_MIN_FREQ * SI5351_FREQ_MULT * 2)
        { r_div = SI5351_OUTPUT_CLK_DIV_128; *freq *= 128ULL; }
    else if (*freq < SI5351_CLKOUT67_MIN_FREQ * SI5351_FREQ_MULT * 4)
        { r_div = SI5351_OUTPUT_CLK_DIV_64;  *freq *= 64ULL; }
    else if (*freq < SI5351_CLKOUT67_MIN_FREQ * SI5351_FREQ_MULT * 8)
        { r_div = SI5351_OUTPUT_CLK_DIV_32;  *freq *= 32ULL; }
    else if (*freq < SI5351_CLKOUT67_MIN_FREQ * SI5351_FREQ_MULT * 16)
        { r_div = SI5351_OUTPUT_CLK_DIV_16;  *freq *= 16ULL; }
    else if (*freq < SI5351_CLKOUT67_MIN_FREQ * SI5351_FREQ_MULT * 32)
        { r_div = SI5351_OUTPUT_CLK_DIV_8;   *freq *= 8ULL; }
    else if (*freq < SI5351_CLKOUT67_MIN_FREQ * SI5351_FREQ_MULT * 64)
        { r_div = SI5351_OUTPUT_CLK_DIV_4;   *freq *= 4ULL; }
    else if (*freq < SI5351_CLKOUT67_MIN_FREQ * SI5351_FREQ_MULT * 128)
        { r_div = SI5351_OUTPUT_CLK_DIV_2;   *freq *= 2ULL; }
    return r_div;
}