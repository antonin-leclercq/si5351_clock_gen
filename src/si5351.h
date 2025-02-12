/*
 * si5351.h
 *
 *  Created on: Feb 10, 2025
 *      Author: antonin
 */

#ifndef SRC_SI5351_H_
#define SRC_SI5351_H_

// Slave address
#define Si5351_ADDRESS 0x60

typedef long unsigned int uint32_t;
typedef unsigned char uint8_t;

// Register Map
#define DEVICE_STATUS 0x00
#define OUTPUT_ENABLE_CONTROL 0x03
#define CLK0_CONTROL 0x10
#define CLK1_CONTROL 0x11
#define CLK2_CONTROL 0x12
#define PLL_RESET 0xB1

void Si5351_Init(void);

void Si5351_WriteByte(uint8_t reg, uint8_t data);
unsigned char Si5351_ReadByte(uint8_t reg);

void Si5351_MultiSynth0_WriteDivider(const uint32_t a, const uint32_t b, const uint32_t c);
void Si5351_Output0_WriteDivider(uint8_t rdiv);

void Si5351_CalculateParams123(const uint32_t a, const uint32_t b, const uint32_t c,
		uint32_t* param1, uint32_t* param2, uint32_t* param3);

void Si5351_SetVCOFrequencyPLLA(const uint32_t a, const uint32_t b, const uint32_t c);


#endif /* SRC_SI5351_H_ */
