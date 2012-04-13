#pragma once

#define GPIO_NUM_VDD        114

enum {
    Tsc2046_start = 0x1 << 7,
    Tsc2046_x     = 0x1 << 4,
    Tsc2046_z1    = 0x3 << 4,
    Tsc2046_z2    = 0x4 << 4,
    Tsc2046_y     = 0x5 << 4,
};

//
// Bit positions for ADS7846E Control byte
//
#define ADS7846E_S				0x80	    // Start bit, always 1
#define ADS7846E_8BIT			0x08	    // 0 = 12-bit conversion, 1 = 8-bits
#define ADS7846E_SER			0x04	    // 0 = Differential, 1 = Single ended

//
// Address select defines for single ended mode (or'ed with the single ended select bit)
//
#define ADS7846E_ADD_SER_TEMP0	(ADS7846E_SER | (0x0 << 4))	// temperature measurement 1
#define ADS7846E_ADD_SER_Y		(ADS7846E_SER | (0x1 << 4))	// Y position measurement
#define ADS7846E_ADD_SER_BAT	(ADS7846E_SER | (0x2 << 4))	// battery input measurement
#define ADS7846E_ADD_SER_Z1		(ADS7846E_SER | (0x3 << 4))	// pressure measurement 1
#define ADS7846E_ADD_SER_Z2		(ADS7846E_SER | (0x4 << 4))	// pressure measurement 2
#define ADS7846E_ADD_SER_X		(ADS7846E_SER | (0x5 << 4))	// X position measurement
#define ADS7846E_ADD_SER_AUX	(ADS7846E_SER | (0x6 << 4))	// auxillary input measurement
#define ADS7846E_ADD_SER_TEMP1	(ADS7846E_SER | (0x7 << 4))	// temperature measurement 2

//
// Address select defines for differential mode
//
#define ADS7846E_ADD_DFR_Y		(0x1 << 4)	// Y position measurement
#define ADS7846E_ADD_DFR_Z1		(0x3 << 4)	// pressure measurement 1
#define ADS7846E_ADD_DFR_Z2		(0x4 << 4)	// pressure measurement 2
#define ADS7846E_ADD_DFR_X		(0x5 << 4)	// X position measurement

//
// Power Down Modes
//
#define ADS_PD10_PDOWN      (0 << 0)    /* low power mode + penirq */
#define ADS_PD10_ADC_ON     (1 << 0)    /* ADC on */
#define ADS_PD10_REF_ON     (2 << 0)    /* vREF on + penirq */
#define ADS_PD10_ALL_ON     (3 << 0)    /* ADC + vREF on */

