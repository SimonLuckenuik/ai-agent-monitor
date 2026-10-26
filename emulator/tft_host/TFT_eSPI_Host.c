// TFT_eSPI processor functions for the emulator (see TFT_eSPI_Host.h). Included by
// TFT_eSPI.cpp in place of Processors/TFT_eSPI_ESP8266.c.

SPIClass& spi = SPI;   // begin() and setFrequency() only; the bytes go to panel.cpp

void TFT_eSPI::pushBlock(uint16_t color, uint32_t len) { emuTftFill(color, len); }

void TFT_eSPI::pushPixels(const void* data_in, uint32_t len) {
  emuTftPixels((const uint16_t*)data_in, len, !_swapBytes);
}

void TFT_eSPI::pushSwapBytePixels(const void* data_in, uint32_t len) {
  emuTftPixels((const uint16_t*)data_in, len, true);
}
