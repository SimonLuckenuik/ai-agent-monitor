// TFT_eSPI processor layer for the emulator: build.sh puts it in place of
// Processors/TFT_eSPI_ESP8266.h in its copy of the library, so the emulator runs the
// clock's real TFT_eSPI. Every byte goes to the simulated ST7789 (panel.cpp), which
// draws into the framebuffer and charges the time the clock's SPI bus would take.
// Modelled on Processors/TFT_eSPI_Generic.h.
#ifndef _TFT_eSPI_HOSTH_
#define _TFT_eSPI_HOSTH_

#include <stdint.h>

void emuTftDc(bool data);                      // the D/C line: false = command
void emuTftByte(uint8_t b);
void emuTftWord(uint16_t w);                   // big-endian, as on the wire
void emuTftFill(uint16_t color, uint32_t len); // the same pixel len times
void emuTftPixels(const uint16_t* data, uint32_t len, bool swap);

#define PROCESSOR_ID 0x8266
#define SET_BUS_WRITE_MODE
#define SET_BUS_READ_MODE
#define DMA_BUSY_CHECK
#define SPI_BUSY_CHECK
#define INIT_TFT_DATA_BUS
// No SUPPORT_TRANSACTIONS: the core's host SPI mock has no beginTransaction().

#define DC_C emuTftDc(false)
#define DC_D emuTftDc(true)
#define CS_L
#define CS_H
#define T_CS_L
#define T_CS_H

#ifndef TFT_RD
  #define TFT_RD -1
#endif
#ifndef TFT_MISO
  #define TFT_MISO -1
#endif

#define tft_Write_8(C)     emuTftByte((uint8_t)(C))
#define tft_Write_16(C)    emuTftWord((uint16_t)(C))
#define tft_Write_16N(C)   emuTftWord((uint16_t)(C))
#define tft_Write_16S(C)   emuTftWord((uint16_t)(((C) >> 8) | ((C) << 8)))
#define tft_Write_32(C)    emuTftWord((uint16_t)((C) >> 16)); emuTftWord((uint16_t)(C))
#define tft_Write_32C(C,D) emuTftWord((uint16_t)(C)); emuTftWord((uint16_t)(D))
#define tft_Write_32D(C)   emuTftWord((uint16_t)(C)); emuTftWord((uint16_t)(C))
#define tft_Read_8()       0   // the clock's panel has no MISO either

#endif
