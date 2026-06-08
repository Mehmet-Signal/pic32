/*
 * si5351.h - Si5351 Driver for PIC32MZ (C Version)
 *
 * Orijinal C++ kodu: Jason Milldrum <milldrum@gmail.com>
 * PIC32MZ C portu v2: i2c_driver.h ile uyumlu hale getirildi
 *
 * Degisiklikler (v2):
 *   - <stdint.h> / <stdbool.h> -> "definitions.h" (Harmony)
 *   - i2c_hal.h -> i2c_driver.h
 */

#ifndef SI5351_H
#define SI5351_H

#include "definitions.h"   /* Harmony: stdint, stdbool ve tum PLIB header'lari */

/* =========================================================
 * REGISTER ADRESLERI VE BIT TANIMLARI
 * ========================================================= */

#define SI5351_BUS_BASE_ADDR            0x60
#define SI5351_XTAL_FREQ                25000000UL
#define SI5351_PLL_FIXED                80000000000ULL
#define SI5351_FREQ_MULT                100ULL
#define SI5351_DEFAULT_CLK              1000000000ULL

#define SI5351_PLL_VCO_MIN              600000000UL
#define SI5351_PLL_VCO_MAX              900000000UL
#define SI5351_MULTISYNTH_MIN_FREQ      500000UL
#define SI5351_MULTISYNTH_DIVBY4_FREQ   150000000UL
#define SI5351_MULTISYNTH_MAX_FREQ      225000000UL
#define SI5351_MULTISYNTH_SHARE_MAX     100000000UL
#define SI5351_MULTISYNTH_SHARE_MIN     1024000UL
#define SI5351_MULTISYNTH67_MAX_FREQ    SI5351_MULTISYNTH_DIVBY4_FREQ
#define SI5351_CLKOUT_MIN_FREQ          4000UL
#define SI5351_CLKOUT_MAX_FREQ          SI5351_MULTISYNTH_MAX_FREQ
#define SI5351_CLKOUT67_MS_MIN          (SI5351_PLL_VCO_MIN / SI5351_MULTISYNTH67_A_MAX)
#define SI5351_CLKOUT67_MIN_FREQ        (SI5351_CLKOUT67_MS_MIN / 128)
#define SI5351_CLKOUT67_MAX_FREQ        SI5351_MULTISYNTH67_MAX_FREQ

#define SI5351_PLL_A_MIN                15
#define SI5351_PLL_A_MAX                90
#define SI5351_PLL_B_MAX                (SI5351_PLL_C_MAX - 1)
#define SI5351_PLL_C_MAX                1048575UL
#define SI5351_MULTISYNTH_A_MIN         6
#define SI5351_MULTISYNTH_A_MAX         1800
#define SI5351_MULTISYNTH67_A_MAX       254
#define SI5351_MULTISYNTH_B_MAX         (SI5351_MULTISYNTH_C_MAX - 1)
#define SI5351_MULTISYNTH_C_MAX         1048575UL
#define SI5351_MULTISYNTH_P1_MAX        ((1 << 18) - 1)
#define SI5351_MULTISYNTH_P2_MAX        ((1 << 20) - 1)
#define SI5351_MULTISYNTH_P3_MAX        ((1 << 20) - 1)
#define SI5351_VCXO_PULL_MIN            30
#define SI5351_VCXO_PULL_MAX            240
#define SI5351_VCXO_MARGIN              103

#define SI5351_DEVICE_STATUS            0
#define SI5351_INTERRUPT_STATUS         1
#define SI5351_INTERRUPT_MASK           2
#define SI5351_STATUS_SYS_INIT          (1 << 7)
#define SI5351_STATUS_LOL_B             (1 << 6)
#define SI5351_STATUS_LOL_A             (1 << 5)
#define SI5351_STATUS_LOS               (1 << 4)
#define SI5351_OUTPUT_ENABLE_CTRL       3
#define SI5351_OEB_PIN_ENABLE_CTRL      9
#define SI5351_PLL_INPUT_SOURCE         15
#define SI5351_CLKIN_DIV_MASK           (3 << 6)
#define SI5351_CLKIN_DIV_1              (0 << 6)
#define SI5351_CLKIN_DIV_2              (1 << 6)
#define SI5351_CLKIN_DIV_4              (2 << 6)
#define SI5351_CLKIN_DIV_8              (3 << 6)
#define SI5351_PLLB_SOURCE              (1 << 3)
#define SI5351_PLLA_SOURCE              (1 << 2)

#define SI5351_CLK0_CTRL                16
#define SI5351_CLK1_CTRL                17
#define SI5351_CLK2_CTRL                18
#define SI5351_CLK3_CTRL                19
#define SI5351_CLK4_CTRL                20
#define SI5351_CLK5_CTRL                21
#define SI5351_CLK6_CTRL                22
#define SI5351_CLK7_CTRL                23
#define SI5351_CLK_POWERDOWN            (1 << 7)
#define SI5351_CLK_INTEGER_MODE         (1 << 6)
#define SI5351_CLK_PLL_SELECT           (1 << 5)
#define SI5351_CLK_INVERT               (1 << 4)
#define SI5351_CLK_INPUT_MASK           (3 << 2)
#define SI5351_CLK_INPUT_XTAL           (0 << 2)
#define SI5351_CLK_INPUT_CLKIN          (1 << 2)
#define SI5351_CLK_INPUT_MULTISYNTH_0_4 (2 << 2)
#define SI5351_CLK_INPUT_MULTISYNTH_N   (3 << 2)
#define SI5351_CLK_DRIVE_STRENGTH_MASK  (3 << 0)
#define SI5351_CLK_DRIVE_STRENGTH_2MA   (0 << 0)
#define SI5351_CLK_DRIVE_STRENGTH_4MA   (1 << 0)
#define SI5351_CLK_DRIVE_STRENGTH_6MA   (2 << 0)
#define SI5351_CLK_DRIVE_STRENGTH_8MA   (3 << 0)

#define SI5351_CLK3_0_DISABLE_STATE     24
#define SI5351_CLK7_4_DISABLE_STATE     25
#define SI5351_CLK_DISABLE_STATE_MASK   3
#define SI5351_CLK_DISABLE_STATE_LOW    0
#define SI5351_CLK_DISABLE_STATE_HIGH   1
#define SI5351_CLK_DISABLE_STATE_FLOAT  2
#define SI5351_CLK_DISABLE_STATE_NEVER  3

#define SI5351_PARAMETERS_LENGTH        8
#define SI5351_PLLA_PARAMETERS          26
#define SI5351_PLLB_PARAMETERS          34
#define SI5351_CLK0_PARAMETERS          42
#define SI5351_CLK1_PARAMETERS          50
#define SI5351_CLK2_PARAMETERS          58
#define SI5351_CLK3_PARAMETERS          66
#define SI5351_CLK4_PARAMETERS          74
#define SI5351_CLK5_PARAMETERS          82
#define SI5351_CLK6_PARAMETERS          90
#define SI5351_CLK7_PARAMETERS          91
#define SI5351_CLK6_7_OUTPUT_DIVIDER    92

#define SI5351_OUTPUT_CLK_DIV_MASK      (7 << 4)
#define SI5351_OUTPUT_CLK6_DIV_MASK     (7 << 0)
#define SI5351_OUTPUT_CLK_DIV_SHIFT     4
#define SI5351_OUTPUT_CLK_DIV6_SHIFT    0
#define SI5351_OUTPUT_CLK_DIV_1         0
#define SI5351_OUTPUT_CLK_DIV_2         1
#define SI5351_OUTPUT_CLK_DIV_4         2
#define SI5351_OUTPUT_CLK_DIV_8         3
#define SI5351_OUTPUT_CLK_DIV_16        4
#define SI5351_OUTPUT_CLK_DIV_32        5
#define SI5351_OUTPUT_CLK_DIV_64        6
#define SI5351_OUTPUT_CLK_DIV_128       7
#define SI5351_OUTPUT_CLK_DIVBY4        (3 << 2)

#define SI5351_SSC_PARAM0               149
#define SI5351_SSC_PARAM1               150
#define SI5351_SSC_PARAM2               151
#define SI5351_SSC_PARAM3               152
#define SI5351_SSC_PARAM4               153
#define SI5351_SSC_PARAM5               154
#define SI5351_SSC_PARAM6               155
#define SI5351_SSC_PARAM7               156
#define SI5351_SSC_PARAM8               157
#define SI5351_SSC_PARAM9               158
#define SI5351_SSC_PARAM10              159
#define SI5351_SSC_PARAM11              160
#define SI5351_SSC_PARAM12              161

#define SI5351_VXCO_PARAMETERS_LOW      162
#define SI5351_VXCO_PARAMETERS_MID      163
#define SI5351_VXCO_PARAMETERS_HIGH     164

#define SI5351_CLK0_PHASE_OFFSET        165
#define SI5351_CLK1_PHASE_OFFSET        166
#define SI5351_CLK2_PHASE_OFFSET        167
#define SI5351_CLK3_PHASE_OFFSET        168
#define SI5351_CLK4_PHASE_OFFSET        169
#define SI5351_CLK5_PHASE_OFFSET        170

#define SI5351_PLL_RESET                177
#define SI5351_PLL_RESET_B              (1 << 7)
#define SI5351_PLL_RESET_A              (1 << 5)

#define SI5351_CRYSTAL_LOAD             183
#define SI5351_CRYSTAL_LOAD_MASK        (3 << 6)
#define SI5351_CRYSTAL_LOAD_0PF         (0 << 6)
#define SI5351_CRYSTAL_LOAD_6PF         (1 << 6)
#define SI5351_CRYSTAL_LOAD_8PF         (2 << 6)
#define SI5351_CRYSTAL_LOAD_10PF        (3 << 6)

#define SI5351_FANOUT_ENABLE            187
#define SI5351_CLKIN_ENABLE             (1 << 7)
#define SI5351_XTAL_ENABLE              (1 << 6)
#define SI5351_MULTISYNTH_ENABLE        (1 << 4)

/* =========================================================
 * YARDIMCI MAKRO - 64-bit bolme
 * ========================================================= */
#define RFRAC_DENOM 1000000ULL

#define do_div(n, base) ({                      \
    uint64_t __base = (base);                   \
    uint64_t __rem;                             \
    __rem = ((uint64_t)(n)) % __base;           \
    (n)  = ((uint64_t)(n)) / __base;            \
    __rem;                                      \
})

/* =========================================================
 * ENUM TANIMLARI
 * ========================================================= */

typedef enum {
    SI5351_CLK0 = 0, SI5351_CLK1, SI5351_CLK2, SI5351_CLK3,
    SI5351_CLK4,     SI5351_CLK5, SI5351_CLK6, SI5351_CLK7
} si5351_clock_t;

typedef enum { SI5351_PLLA = 0, SI5351_PLLB } si5351_pll_t;

typedef enum {
    SI5351_DRIVE_2MA = 0, SI5351_DRIVE_4MA,
    SI5351_DRIVE_6MA,     SI5351_DRIVE_8MA
} si5351_drive_t;

typedef enum {
    SI5351_CLK_SRC_XTAL = 0, SI5351_CLK_SRC_CLKIN,
    SI5351_CLK_SRC_MS0,       SI5351_CLK_SRC_MS
} si5351_clock_source_t;

typedef enum {
    SI5351_CLK_DISABLE_LOW = 0, SI5351_CLK_DISABLE_HIGH,
    SI5351_CLK_DISABLE_HI_Z,    SI5351_CLK_DISABLE_NEVER
} si5351_clock_disable_t;

typedef enum {
    SI5351_FANOUT_CLKIN = 0, SI5351_FANOUT_XO, SI5351_FANOUT_MS
} si5351_clock_fanout_t;

typedef enum { SI5351_PLL_INPUT_XO = 0, SI5351_PLL_INPUT_CLKIN } si5351_pll_input_t;

/* =========================================================
 * STRUCT TANIMLARI
 * ========================================================= */

typedef struct { uint32_t p1; uint32_t p2; uint32_t p3; } Si5351RegSet;

typedef struct {
    uint8_t SYS_INIT; uint8_t LOL_B; uint8_t LOL_A;
    uint8_t LOS;      uint8_t REVID;
} Si5351Status;

typedef struct {
    uint8_t SYS_INIT_STKY; uint8_t LOL_B_STKY;
    uint8_t LOL_A_STKY;    uint8_t LOS_STKY;
} Si5351IntStatus;

typedef struct {
    uint8_t            i2c_bus_addr;
    uint64_t           plla_freq;
    uint64_t           pllb_freq;
    uint64_t           clk_freq[8];
    si5351_pll_t       pll_assignment[8];
    si5351_pll_input_t plla_ref_osc;
    si5351_pll_input_t pllb_ref_osc;
    uint32_t           xtal_freq[2];
    int32_t            ref_correction[2];
    uint8_t            clkin_div;
    bool               clk_first_set[8];
    Si5351Status       dev_status;
    Si5351IntStatus    dev_int_status;
} Si5351Dev;

/* =========================================================
 * PUBLIC FONKSIYON PROTOTIPLERI
 * ========================================================= */

void    si5351_init_dev(Si5351Dev *dev, uint8_t i2c_addr);
bool    si5351_init(Si5351Dev *dev, uint8_t xtal_load_c,
                    uint32_t xo_freq, int32_t corr);
void    si5351_reset(Si5351Dev *dev);
uint8_t si5351_set_freq(Si5351Dev *dev, uint64_t freq, si5351_clock_t clk);
uint8_t si5351_set_freq_manual(Si5351Dev *dev, uint64_t freq,
                               uint64_t pll_freq, si5351_clock_t clk);
void    si5351_set_pll(Si5351Dev *dev, uint64_t pll_freq, si5351_pll_t target_pll);
void    si5351_set_ms(Si5351Dev *dev, si5351_clock_t clk,
                      Si5351RegSet ms_reg, uint8_t int_mode,
                      uint8_t r_div, uint8_t div_by_4);
void    si5351_output_enable(Si5351Dev *dev, si5351_clock_t clk, uint8_t enable);
void    si5351_drive_strength(Si5351Dev *dev, si5351_clock_t clk, si5351_drive_t drive);
void    si5351_update_status(Si5351Dev *dev);
void    si5351_set_correction(Si5351Dev *dev, int32_t corr, si5351_pll_input_t ref_osc);
int32_t si5351_get_correction(Si5351Dev *dev, si5351_pll_input_t ref_osc);
void    si5351_set_phase(Si5351Dev *dev, si5351_clock_t clk, uint8_t phase);
void    si5351_pll_reset(Si5351Dev *dev, si5351_pll_t target_pll);
void    si5351_set_ms_source(Si5351Dev *dev, si5351_clock_t clk, si5351_pll_t pll);
void    si5351_set_int(Si5351Dev *dev, si5351_clock_t clk, uint8_t enable);
void    si5351_set_clock_pwr(Si5351Dev *dev, si5351_clock_t clk, uint8_t pwr);
void    si5351_set_clock_invert(Si5351Dev *dev, si5351_clock_t clk, uint8_t inv);
void    si5351_set_clock_source(Si5351Dev *dev, si5351_clock_t clk,
                                si5351_clock_source_t src);
void    si5351_set_clock_disable(Si5351Dev *dev, si5351_clock_t clk,
                                 si5351_clock_disable_t dis_state);
void    si5351_set_clock_fanout(Si5351Dev *dev, si5351_clock_fanout_t fanout,
                                uint8_t enable);
void    si5351_set_pll_input(Si5351Dev *dev, si5351_pll_t pll,
                             si5351_pll_input_t input);
void    si5351_set_vcxo(Si5351Dev *dev, uint64_t pll_freq, uint8_t ppm);
void    si5351_set_ref_freq(Si5351Dev *dev, uint32_t ref_freq,
                            si5351_pll_input_t ref_osc);

/* Dusuk seviye - test/debug icin */
uint8_t si5351_write_bulk(Si5351Dev *dev, uint8_t addr,
                          uint8_t bytes, uint8_t *data);
uint8_t si5351_write(Si5351Dev *dev, uint8_t addr, uint8_t data);
uint8_t si5351_read(Si5351Dev *dev, uint8_t addr);

#endif /* SI5351_H */