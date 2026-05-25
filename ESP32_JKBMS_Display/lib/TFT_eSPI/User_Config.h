#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#include <pgmspace.h>

#define CONFIG_HW_SPI
#define CONFIG_SPI_FREQUENCY 40000000
#define CONFIG_SPI_READ_FREQ 20000000

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

#define DISPLAY_MODEL_ST7789

#define TOUCH_CS -1
#define TOUCH_IRQ -1

#define SD_CARD_CS -1
#define SD_CARD_MISO -1
#define SD_CARD_MOSI -1
#define SD_CARD_SCLK -1

#endif