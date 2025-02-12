/*
 * si5351.c
 *
 *  Created on: Feb 10, 2025
 *      Author: antonin
 */

#include "si5351.h"
#include "stm32f0xx.h"

void Si5351_Init(void)
{
	// I2C on PB6-SCL and PB7-SDA (Alternate function 1)
	// I2C1 running at 8MHz, frequency of SCL is 100kHz

	// Si5351 output frequency is 300kHz on CLK0 (for example)
	// Output frequency = fVCO / (MultiSynth0 divider * Output0 divider)
	// fVCO = fXTAL * Feedback Ratio = fPLL

	// Enable GPIOB clock
	RCC->AHBENR |= RCC_AHBENR_GPIOBEN;

	// Set PB6 and PB7 as SCL, SDA
	GPIOB->MODER &= ~(GPIO_MODER_MODER6_Msk | GPIO_MODER_MODER7_Msk);
	GPIOB->MODER |= (0x02 << GPIO_MODER_MODER6_Pos) | (0x02 << GPIO_MODER_MODER7_Pos);
	GPIOB->OTYPER |= GPIO_OTYPER_OT_6 | GPIO_OTYPER_OT_7;
	GPIOB->AFR[0] &= ~(GPIO_AFRL_AFRL6_Msk | GPIO_AFRL_AFRL7_Msk);
	GPIOB->AFR[0] |= (0x01 << GPIO_AFRL_AFRL6_Pos) | (0x01 << GPIO_AFRL_AFRL7_Pos);

	// Enable I2C1 clock
	RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

	// Reset I2C configuration
	I2C1->CR1 = 0x00000000;
	I2C1->CR2 = 0x00000000;

	// Set Si5351 address
	I2C1->CR2 |= ((Si5351_ADDRESS << 1) << I2C_CR2_SADD_Pos);

	// Set I2C timing pre-scaler to /8 for 1MHz counter clock
	I2C1->TIMINGR &= ~I2C_TIMINGR_PRESC_Msk;
	I2C1->TIMINGR |= (0x07 << I2C_TIMINGR_PRESC_Pos);

	// Set I2C low and high period for 100kHz clock signal
	I2C1->TIMINGR &= ~(I2C_TIMINGR_SCLL_Msk | I2C_TIMINGR_SCLH_Msk);
	I2C1->TIMINGR |= ((5 -1) << I2C_TIMINGR_SCLL_Pos) | ((5 -1) << I2C_TIMINGR_SCLH_Pos);

	// Set data setup and hold time to minimum
	I2C1->TIMINGR &= ~(I2C_TIMINGR_SCLDEL_Msk | I2C_TIMINGR_SDADEL_Msk);

	// Enable peripheral
	I2C1->CR1 |= I2C_CR1_PE;

	// Wait for device initialization to be complete
	while ((Si5351_ReadByte(DEVICE_STATUS) & 0x80) == 0x80);

	// Disable all 3 Si5351 outputs
	Si5351_WriteByte(OUTPUT_ENABLE_CONTROL, 0x07);

	// Power down all 3 output drivers
	Si5351_WriteByte(CLK0_CONTROL, 0x80);
	Si5351_WriteByte(CLK1_CONTROL, 0x80);
	Si5351_WriteByte(CLK2_CONTROL, 0x80);

	// Disable Spread Spectrum
	uint8_t tmp = Si5351_ReadByte(0x95);
	tmp &= ~0x80;
	Si5351_WriteByte(0x95, tmp);

	// Set VCO frequency for PLLA for x26 => 650MHz VCO
	// Ratio must be between 15 and 90
	Si5351_SetVCOFrequencyPLLA(26, 0, 1);

	// Use MultiSynth0 for CLK0 + use 4mA drive
	// CLK0 is connected to PLLA by default
	tmp = Si5351_ReadByte(CLK0_CONTROL);
	tmp |= (3 << 2) | (1 << 0);
	Si5351_WriteByte(CLK0_CONTROL, tmp);

	// Divide VCO frequency by a value other than 4
	tmp = Si5351_ReadByte(0x2C);
	tmp &= ~0x0C;
	Si5351_WriteByte(0x2C, tmp);

	// Set MultiSynth0 - CLK0 divide value for 300kHz output (2*(a + b/c) = 650MHz / 300kHz = 2*1083.333)
	// Set a+b/c to 1083.33 and use /2 output divider
	Si5351_MultiSynth0_WriteDivider(1083, 1, 3);

	// Divide output by 2
	Si5351_Output0_WriteDivider(2);

	// Apply PLLA and PLLB soft reset
	Si5351_WriteByte(PLL_RESET, 0xA0);

	// Power up CLK0
	tmp = Si5351_ReadByte(CLK0_CONTROL);
	tmp &= ~0x80;
	Si5351_WriteByte(CLK0_CONTROL, tmp);

	// Enable CLK0 output - frequency seen on oscilloscope should be 300kHz
	tmp = Si5351_ReadByte(OUTPUT_ENABLE_CONTROL);
	tmp &= ~0x01;
	Si5351_WriteByte(OUTPUT_ENABLE_CONTROL, tmp);
}

void Si5351_WriteByte(uint8_t reg, uint8_t data)
{
	// Switch to write transfer
	I2C1->CR2 &= ~I2C_CR2_RD_WRN;

	// Enable auto-end : automatic STOP condition generation after NBYTES is sent
	I2C1->CR2 |= I2C_CR2_AUTOEND;

	// Set Number of bytes to 2 (register + data)
	I2C1->CR2 &= ~I2C_CR2_NBYTES_Msk;
	I2C1->CR2 |= (0x02 << I2C_CR2_NBYTES_Pos);

	// Generate START condition
	I2C1->CR2 |= I2C_CR2_START;

	// Wait for TX register to be empty
	while((I2C1->ISR & I2C_ISR_TXE) != I2C_ISR_TXE);

	// Send register code
	I2C1->TXDR = reg;

	// Wait for TX register to be empty
	while((I2C1->ISR & I2C_ISR_TXE) != I2C_ISR_TXE);

	// Send data
	I2C1->TXDR = data;

	// Wait for STOP condition to finally end the write sequence
	while((I2C1->ISR & I2C_ISR_STOPF) != I2C_ISR_STOPF);
}

uint8_t Si5351_ReadByte(uint8_t reg)
{
	// First switch to write transfer to write register code
	I2C1->CR2 &= ~I2C_CR2_RD_WRN;

	// Disable AUTOEND for STOP condition
	I2C1->CR2 &= ~I2C_CR2_AUTOEND;

	// Set Number of bytes to 1 (register)
	I2C1->CR2 &= ~I2C_CR2_NBYTES_Msk;
	I2C1->CR2 |= (0x01 << I2C_CR2_NBYTES_Pos);

	// Generate START condition
	I2C1->CR2 |= I2C_CR2_START;

	// Wait for TX register to be empty
	while((I2C1->ISR & I2C_ISR_TXE) != I2C_ISR_TXE);

	// Send register code
	I2C1->TXDR = reg;

	// Wait for transfer complete
	while((I2C1->ISR & I2C_ISR_TC) != I2C_ISR_TC);

	// Then switch to read transfer
	I2C1->CR2 |= I2C_CR2_RD_WRN;

	// Set number of bytes to 1
	I2C1->CR2 &= ~I2C_CR2_NBYTES_Msk;
	I2C1->CR2 |= (0x01 << I2C_CR2_NBYTES_Pos);

	// Generate START condition
	I2C1->CR2 |= I2C_CR2_START;

	// Wait for RX register to fill
	while((I2C1->ISR & I2C_ISR_RXNE) != I2C_ISR_RXNE);

	// Get received data
	uint8_t data = I2C1->RXDR;

	// Generate STOP condition
	I2C1->CR2 |= I2C_CR2_STOP;

	// Wait for STOP condition
	while((I2C1->ISR & I2C_ISR_STOPF) != I2C_ISR_STOPF);

	return data;
}

void Si5351_MultiSynth0_WriteDivider(const uint32_t a, const uint32_t b, const uint32_t c)
{
	// Output frequency = fVCO / (a + b/c)
	uint32_t param1, param2, param3;
	Si5351_CalculateParams123(a, b, c, &param1, &param2, &param3);

	// Param3 [15:0]
	Si5351_WriteByte(0x2B, (uint8_t)(param3 & 0xFF));
	Si5351_WriteByte(0x2A, (uint8_t)((param3 & 0xFF00) >> 8));

	// Param2 [15:0]
	Si5351_WriteByte(0x31, (uint8_t)(param2 & 0xFF));
	Si5351_WriteByte(0x30, (uint8_t)((param2 & 0xFF00) >> 8));

	// Param2 and Param3 [19:16]
	Si5351_WriteByte(0x2F, (uint8_t)(
			(param3 & 0xF0000) >> 12 |
			(param2 & 0xF0000) >> 16
		));

	// Param1 [15:0]
	Si5351_WriteByte(0x2E, (uint8_t)(param1 & 0xFF));
	Si5351_WriteByte(0x2D, (uint8_t)((param1 & 0xFF00) >> 8));

	// Param1 [17:16]
	uint8_t tmp = Si5351_ReadByte(0x2C);
	tmp &= ~0x03;
	tmp |= (param1 & 0x30000) >> 16;
	Si5351_WriteByte(0x2C, tmp);
}

void Si5351_Output0_WriteDivider(uint8_t rdiv)
{
	uint8_t actual_div = 0;
	switch(rdiv)
	{
	case 1: actual_div = 0; break;
	case 2: actual_div = 1; break;
	case 4: actual_div = 2; break;
	case 8: actual_div = 3; break;
	case 16: actual_div = 4; break;
	case 32: actual_div = 5; break;
	case 64: actual_div = 6; break;
	case 128: actual_div = 7; break;
	default: actual_div = 7; break;
	}

	uint8_t tmp = Si5351_ReadByte(0x2C);
	tmp &= ~0x70;
	tmp |= actual_div << 4;
	Si5351_WriteByte(0x2C, tmp);

}

void Si5351_CalculateParams123(const uint32_t a, const uint32_t b, const uint32_t c,
		uint32_t* param1, uint32_t* param2, uint32_t* param3)
{
	// Ratio is a + b/c
	uint32_t tmp = (uint32_t) (128 * (float)b / c);
	*param1 = (128 * a + tmp - 512) & 0x3FFFF;
	*param2 = (128 * b - c * tmp) & 0xFFFFF;
	*param3 = c & 0xFFFFF;
}

void Si5351_SetVCOFrequencyPLLA(const uint32_t a, const uint32_t b, const uint32_t c)
{
	// Sets VCO frequency for PLLA
	// fVCO = fXTAL * (a + b/c)

	uint32_t param1, param2, param3;
	Si5351_CalculateParams123(a, b, c, &param1, &param2, &param3);

	Si5351_WriteByte(0x1B, (uint8_t)(param3 & 0xFF));
	Si5351_WriteByte(0x1A, (uint8_t)((param3 & 0xFF00) >> 8));

	Si5351_WriteByte(0x1C, (uint8_t)((param1 & 0x30000) >> 16));
	Si5351_WriteByte(0x1D, (uint8_t)((param1 & 0xFF00) >> 8));

	Si5351_WriteByte(0x1E, (uint8_t)(param1 & 0xFF));
	Si5351_WriteByte(0x1F, (uint8_t)(
		(param3 & 0xF0000) >> 12 |
		(param2 & 0xF0000) >> 16
	));

	Si5351_WriteByte(0x20, (uint8_t)((param2 & 0xFF00) >> 8));
	Si5351_WriteByte(0x21, (uint8_t)(param2 & 0xFF));
}
