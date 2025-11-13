#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/structs/scb.h"
#include "hardware/regs/m0plus.h"
#include "Color.h" // couleurs RGB565 prédéfinies   
#include "ScrollableArea.h"
#include "DHT11.h"
#include "font_mini.h"
#include "font_standard.h"
#include "arial_S32.h"

// -------- CONFIGURATION matériel (modifie si nécessaire) ----------
struct TFTConfig {
    static constexpr int SPI_PORT         = 0;   // Use 0 for spi0, 1 for spi1
    static constexpr int PIN_SCK          = 2;   // SPI0 SCK
    static constexpr int PIN_MOSI         = 3;   // SPI0 TX (MOSI)
    static constexpr int PIN_CS           = 1;   // optional, chip select
    static constexpr int PIN_DC           = 0;   // Data/Command
    static constexpr int PIN_RST          = 7;
    // static constexpr int PIN_BL        = 14; // Uncomment if needed

    static constexpr int WIDTH            = 240;
    static constexpr int HEIGHT           = 240;
    static constexpr int BYTES_PER_PIXEL  = 2;
    static constexpr int FB_SIZE_BYTES    = WIDTH * HEIGHT * BYTES_PER_PIXEL;
    static constexpr int SPI_BAUDRATE     = 40000000; // 40 MHz
};

struct DHT11Config {
    static constexpr int PIN_DATA = 4;  // GPIO4 pour DHT11
};