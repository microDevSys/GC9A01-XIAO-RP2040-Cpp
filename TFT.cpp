#include "TFT.h"
#include "main.h"
#include <cstring>

/*******************************************************
 * Nom du fichier : TFT.cpp
 * Auteur         : Guillaume Sahuc
 * Date           : 23 novembre 2025
 * Description    : driver GC9A01 on spi0 RP2040
 *******************************************************/

// ===== VARIABLES STATIQUES =====
TFT* TFT::instance = nullptr;

// Framebuffer statique pour éviter l'allocation dynamique (heap insuffisante)
// Aligné sur 4 octets pour de meilleures performances DMA
static uint8_t g_tft_framebuffer[TFTConfig::FB_SIZE_BYTES] __attribute__((aligned(4)));

// ===== CONSTRUCTEUR/DESTRUCTEUR =====
TFT::TFT() : framebuffer(nullptr), dma_chan(-1), dma_irq(0), 
             dma_busy(false), fill_color(0x0000), scroll_x(0), scroll_y(0),
             current_font(FontType::FONT_STANDARD), current_rotation(Rotation::PORTRAIT_0) {
    updateScreenDimensions();
}

TFT::~TFT() {
    // Framebuffer statique: rien à libérer
}

// Taille du framebuffer (en octets)
size_t TFT::getFramebufferSize() const {
    return static_cast<size_t>(TFTConfig::FB_SIZE_BYTES);
}

// ===== INITIALISATION =====
void TFT::init() {
    instance = this;
    
    // Configuration GPIO
    initGPIO();
    
    // Configuration SPI
    initSPI();
    
    // Allocation du framebuffer
    initFramebuffer();
    
    // Configuration DMA
    initDMA();
    
    // Séquence d'initialisation LCD
    initSequence();
}

void TFT::initGPIO() {
    gpio_init(TFTConfig::PIN_DC); 
    gpio_set_dir(TFTConfig::PIN_DC, GPIO_OUT);
    gpio_init(TFTConfig::PIN_RST); 
    gpio_set_dir(TFTConfig::PIN_RST, GPIO_OUT);
    gpio_init(TFTConfig::PIN_CS); 
    gpio_set_dir(TFTConfig::PIN_CS, GPIO_OUT);

    gpio_put(TFTConfig::PIN_CS, 1);
    gpio_put(TFTConfig::PIN_RST, 1);
}

void TFT::initSPI() {
    spi_init(spi0, TFTConfig::SPI_BAUDRATE);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(TFTConfig::PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(TFTConfig::PIN_MOSI, GPIO_FUNC_SPI);
}

void TFT::initFramebuffer() {
    framebuffer = g_tft_framebuffer;
    // Clear initial
    for (int i = 0; i < TFTConfig::FB_SIZE_BYTES; ++i) framebuffer[i] = 0;
}

void TFT::initDMA() {
    dma_chan = dma_claim_unused_channel(true);
    dma_irq = dma_chan;
    irq_set_exclusive_handler(DMA_IRQ_0, TFT::dma_irq_handler_wrapper);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_channel_set_irq0_enabled(dma_chan, true);
}

// ===== COMMUNICATION SPI =====
void TFT::writeCmd(const uint8_t* cmd, size_t len) {
    // S'assurer que le bus SPI est à pleine vitesse pour l'écran
    spi_set_baudrate(spi0, TFTConfig::SPI_BAUDRATE);
    gpio_put(TFTConfig::PIN_DC, 0);
    gpio_put(TFTConfig::PIN_CS, 0);
    spi_write_blocking(spi0, cmd, len);
    gpio_put(TFTConfig::PIN_CS, 1);
}

void TFT::writeData(const uint8_t* data, size_t len) {
    // S'assurer que le bus SPI est à pleine vitesse pour l'écran
    spi_set_baudrate(spi0, TFTConfig::SPI_BAUDRATE);
    gpio_put(TFTConfig::PIN_DC, 1);
    gpio_put(TFTConfig::PIN_CS, 0);
    spi_write_blocking(spi0, data, len);
    gpio_put(TFTConfig::PIN_CS, 1);
}

void TFT::cmdWithData(const uint8_t cmd, const uint8_t* data, size_t datalen) {
    writeCmd(&cmd, 1);
    if (datalen) writeData(data, datalen);
}

void TFT::setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t buf[4];
    
    // Colonne
    buf[0] = (x0 >> 8) & 0xFF; 
    buf[1] = x0 & 0xFF;
    buf[2] = (x1 >> 8) & 0xFF; 
    buf[3] = x1 & 0xFF;
    cmdWithData(0x2A, buf, 4);
    
    // Ligne
    buf[0] = (y0 >> 8) & 0xFF; 
    buf[1] = y0 & 0xFF;
    buf[2] = (y1 >> 8) & 0xFF; 
    buf[3] = y1 & 0xFF;
    cmdWithData(0x2B, buf, 4);
}

// ===== GESTION DMA =====
void TFT::dma_irq_handler_wrapper() {
    if (instance) instance->dmaHandler();
}

void TFT::dmaHandler() {
    dma_hw->ints0 = 1u << dma_chan;
    dma_busy = false;
}

void TFT::sendFrame() {
    if (!framebuffer) return;
    
    // S'assurer que le bus SPI est à pleine vitesse pour le transfert d'image
    spi_set_baudrate(spi0, TFTConfig::SPI_BAUDRATE);

    setWindow(0, 0, screen_width - 1, screen_height - 1);
    uint8_t cmd = 0x2C;
    writeCmd(&cmd, 1);
    
    gpio_put(TFTConfig::PIN_DC, 1);
    gpio_put(TFTConfig::PIN_CS, 0);

    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, DREQ_SPI0_TX);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);

    dma_channel_configure(dma_chan, &c, &spi_get_hw(spi0)->dr, 
                         framebuffer, TFTConfig::FB_SIZE_BYTES, true);

    dma_busy = true;
    dma_channel_wait_for_finish_blocking(dma_chan);
    dma_busy = false;

    gpio_put(TFTConfig::PIN_CS, 1);
}

void TFT::blitRGB565FullFrame(const uint8_t* src) {
    if (!framebuffer || !src) return;
    std::memcpy(framebuffer, src, TFTConfig::FB_SIZE_BYTES);
}

// ===== GESTION DU FRAMEBUFFER =====
void TFT::fill(uint16_t color) {
    fill_color = color;
    for (int i = 0; i < TFTConfig::FB_SIZE_BYTES; i += 2) {
        framebuffer[i] = (color >> 8) & 0xFF;
        framebuffer[i+1] = color & 0xFF;
    }
}

void TFT::clear() {
    // Efface en noir (RGB565)
    fill(COLOR_16BITS_BLACK);
    // Envoie immédiatement à l'écran
    sendFrame();
}

void TFT::setPixel(int x, int y, uint16_t color) {
   
    int screen_x = x - scroll_x;
    int screen_y = y - scroll_y;
    
    // Vérifier les limites selon les dimensions logiques de l'écran
    if (screen_x < 0 || screen_x >= screen_width || 
        screen_y < 0 || screen_y >= screen_height) return;
    
    int pos = (screen_y * screen_width + screen_x) * 2;
    framebuffer[pos] = (color >> 8) & 0xFF;
    framebuffer[pos + 1] = color & 0xFF;
}

void TFT::setFillColor(uint16_t color) {
    fill_color = color;
}

// ===== PRIMITIVES DE DESSIN =====
void TFT::drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = y0;
    
    while (true) {
        setPixel(x, y, color);
        if (x == x1 && y == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

void TFT::drawRect(int x, int y, int w, int h, uint16_t color) {
    drawLine(x, y, x + w - 1, y, color);
    drawLine(x, y + h - 1, x + w - 1, y + h - 1, color);
    drawLine(x, y, x, y + h - 1, color);
    drawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void TFT::fillRect(int x, int y, int w, int h, uint16_t color) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            setPixel(px, py, color);
        }
    }
}

void TFT::drawCircle(int xc, int yc, int r, uint16_t color) {
    int x = 0;
    int y = r;
    int d = 3 - 2 * r;
    
    while (y >= x) {
        setPixel(xc + x, yc + y, color);
        setPixel(xc - x, yc + y, color);
        setPixel(xc + x, yc - y, color);
        setPixel(xc - x, yc - y, color);
        setPixel(xc + y, yc + x, color);
        setPixel(xc - y, yc + x, color);
        setPixel(xc + y, yc - x, color);
        setPixel(xc - y, yc - x, color);
        
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

void TFT::drawFillCircle(int xc, int yc, int r, uint16_t color) {
    int x = 0;
    int y = r;
    int d = 3 - 2 * r;
    
    auto fillHorizontalLines = [&](int cx, int cy, int x, int y) {
        drawLine(cx - x, cy + y, cx + x, cy + y, color);
        drawLine(cx - x, cy - y, cx + x, cy - y, color);
        drawLine(cx - y, cy + x, cx + y, cy + x, color);
        drawLine(cx - y, cy - x, cx + y, cy - x, color);
    };
    
    while (y >= x) {
        fillHorizontalLines(xc, yc, x, y);
        x++;
        
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

void TFT::drawSmallCircle(int xc, int yc, int r, uint16_t color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) {
                int px = xc + x;
                int py = yc + y;
                // Le framebuffer est toujours WIDTH x HEIGHT (320x480) physique
                if (px >= 0 && px < TFTConfig::WIDTH && 
                    py >= 0 && py < TFTConfig::HEIGHT) {
                    int pos = (py * TFTConfig::WIDTH + px) * 2;
                    framebuffer[pos] = (color >> 8) & 0xFF;
                    framebuffer[pos + 1] = color & 0xFF;
                }
            }
        }
    }
}

// ===== GESTION DES POLICES =====
void TFT::setFont(FontType font) {
    current_font = font;
}

FontType TFT::getFont() const {
    return current_font;
}

const uint8_t* TFT::getFontData(char c) {
    uint8_t char_code = (uint8_t)c;
    
    switch (current_font) {
        case FontType::FONT_MINI:
            return &font_mini_4x6[char_code * 6];
        case FontType::FONT_STANDARD:
            return &ufont_1[char_code * 12];
        case FontType::ARIAL_32: {
            const arial_S32_CharInfo& info = getArialCharInfo(c);
            return &arial_S32_Bitmaps[info.offset];
        }
        default:
            return &font_mini_4x6[char_code * 6];
    }
}

const arial_S32_CharInfo& TFT::getArialCharInfo(char c) {
    return arial_S32_Info[(uint8_t)c];
}

int TFT::getFontWidth() {
    switch (current_font) {
        case FontType::FONT_MINI:
            return 4;
        case FontType::FONT_STANDARD:
            return 8;
        case FontType::ARIAL_32: {
            const arial_S32_CharInfo& info = getArialCharInfo(' ');
            return info.w;
        }
        default:
            return 4;
    }
}

int TFT::getFontHeight() {
    switch (current_font) {
        case FontType::FONT_MINI:
            return 6;
        case FontType::FONT_STANDARD:
            return 12;
        case FontType::ARIAL_32: {
            const arial_S32_CharInfo& info = getArialCharInfo('A');
            return info.h;
        }
        default:
            return 6;
    }
}

int TFT::getCharWidth(char c) {
    switch (current_font) {
        case FontType::FONT_MINI:
        case FontType::FONT_STANDARD:
            return getFontWidth();
        case FontType::ARIAL_32: {
            const arial_S32_CharInfo& info = getArialCharInfo(c);
            return info.w;
        }
        default:
            return getFontWidth();
    }
}

int TFT::getTextWidth(const char* text) {
    int total_width = 0;
    int spacing = 1;
    
    for (int i = 0; text[i] != '\0'; i++) {
        total_width += getCharWidth(text[i]);
        if (text[i + 1] != '\0') {
            total_width += spacing;
        }
    }
    
    return total_width;
}

// ===== RENDU DE TEXTE =====
void TFT::drawChar(int x, int y, char c, uint16_t color) {
    if (current_font == FontType::ARIAL_32) {
        drawArialChar(x, y, c, color);
        return;
    }
    
    const uint8_t* char_data = getFontData(c);
    int font_width = getFontWidth();
    int font_height = getFontHeight();
    
    for (int row = 0; row < font_height; row++) {
        uint8_t line = char_data[row];
        for (int col = 0; col < font_width; col++) {
            if (line & (0x80 >> col)) {
                setPixel(x + col, y + row, color);
            }
        }
    }
}

void TFT::drawText(int x, int y, const char* text, uint16_t color) {
    int char_x = x;
    int spacing = 1;
    
    for (int i = 0; text[i] != '\0'; i++) {
        drawChar(char_x, y, text[i], color);
        int char_width = getCharWidth(text[i]);
        char_x += char_width + spacing;
    }
}


void TFT::drawArialChar(int x, int y, char c, uint16_t color) {
    const arial_S32_CharInfo& charInfo = getArialCharInfo(c);
    
    uint32_t bitmap_offset = charInfo.offset;
    const uint8_t* bitmap = &arial_S32_Bitmaps[bitmap_offset];
    
    int char_width = charInfo.w;
    int char_height = charInfo.h;
    
    for (int row = 0; row < char_height; row++) {
        for (int col = 0; col < char_width; col++) {
            int bit_index = row * char_width + col;
            int byte_index = bit_index / 8;
            int bit_pos = 7 - (bit_index % 8);
            
            if (bitmap[byte_index] & (1 << bit_pos)) {
                setPixel(x + col, y + row, color);
            }
        }
    }
}

// ===== GESTION DU SCROLL =====
void TFT::setScrollOffset(int x_offset, int y_offset) {
    scroll_x = x_offset;
    scroll_y = y_offset;
}

void TFT::scroll(int dx, int dy) {
    scroll_x += dx;
    scroll_y += dy;
}

void TFT::scrollUp(int lines) {
    int line_height = getFontHeight() + 1;
    scroll_y += lines * line_height;
}

void TFT::scrollDown(int lines) {
    int line_height = getFontHeight() + 1;
    scroll_y -= lines * line_height;
    if (scroll_y < 0) scroll_y = 0;
}

void TFT::scrollLeft(int pixels) {
    scroll_x += pixels;
}

void TFT::scrollRight(int pixels) {
    scroll_x -= pixels;
    if (scroll_x < 0) scroll_x = 0;
}

// ===== GESTION DE LA ROTATION =====
void TFT::setRotation(Rotation rotation) {
    current_rotation = rotation;
    updateScreenDimensions();
    
    uint8_t madctl_value;
    switch (rotation) {
        case Rotation::PORTRAIT_0:   madctl_value = 0x08; break;  // 0°
        case Rotation::LANDSCAPE_90: madctl_value = 0x68; break;  // 90°
        case Rotation::PORTRAIT_180: madctl_value = 0xC8; break;  // 180°
        case Rotation::LANDSCAPE_270: madctl_value = 0xA8; break; // 270°
        default: madctl_value = 0x08; break;
    }
    
    cmdWithData(0x36, &madctl_value, 1);
    
    printf("TFT rotation set to %d° (%dx%d)\n", 
           (int)rotation * 90, screen_width, screen_height);
}

Rotation TFT::getRotation() const {
    return current_rotation;
}

int TFT::getScreenWidth() const {
    return screen_width;
}

int TFT::getScreenHeight() const {
    return screen_height;
}

void TFT::updateScreenDimensions() {
    switch (current_rotation) {
        case Rotation::PORTRAIT_0:
        case Rotation::PORTRAIT_180:
            screen_width = TFTConfig::WIDTH;
            screen_height = TFTConfig::HEIGHT;
            break;
        case Rotation::LANDSCAPE_90:
        case Rotation::LANDSCAPE_270:
            screen_width = TFTConfig::HEIGHT;
            screen_height = TFTConfig::WIDTH;
            break;
    }
}

// ===== FONCTIONS SPÉCIALISÉES =====
void TFT::drawBalls(const std::vector<Ball>& balls) {
    for (const auto& ball : balls) {
        drawSmallCircle((int)ball.x, (int)ball.y, ball.radius, ball.color);
    }
}

void TFT::drawSecondsMarkers() {
    // Utiliser les dimensions logiques de l'écran (déjà calculées selon rotation)
    int cx = screen_width / 2;
    int cy = screen_height / 2;
    int r = (screen_width < screen_height ? screen_width : screen_height) / 2 - 10;
    
    for (int i = 0; i < 60; i++) {
        float angle = (float)i * 2.0f * M_PI / 60.0f;
        int x = cx + (int)(r * cosf(angle));
        int y = cy + (int)(r * sinf(angle));
        int dot_radius = (i % 5 == 0) ? 5 : 2;
        drawSmallCircle(x, y, dot_radius, COLOR_16BITS_WHITE);
    }
}

// ===== SÉQUENCE D'INITIALISATION LCD =====
void TFT::initSequence() {
    // Reset sequence
    gpio_put(TFTConfig::PIN_RST, 0);
    sleep_ms(20);
    gpio_put(TFTConfig::PIN_RST, 1);
    sleep_ms(20);

    uint8_t data[12];

    cmdWithData(0xEF, nullptr, 0);
    data[0] = 0xEB; data[1] = 0x14; 
    cmdWithData(0xEB, data, 2);

    cmdWithData(0xFE, nullptr, 0);
    cmdWithData(0xEF, nullptr, 0);

    data[0] = 0xEB; data[1] = 0x14; 
    cmdWithData(0xEB, data, 2);

    data[0] = 0x40; cmdWithData(0x84, data, 1);
    data[0] = 0xFF; cmdWithData(0x85, data, 1);
    data[0] = 0xFF; cmdWithData(0x86, data, 1);
    data[0] = 0xFF; cmdWithData(0x87, data, 1);
    data[0] = 0x0A; cmdWithData(0x88, data, 1);
    data[0] = 0x21; cmdWithData(0x89, data, 1);
    data[0] = 0x00; cmdWithData(0x8A, data, 1);
    data[0] = 0x80; cmdWithData(0x8B, data, 1);
    data[0] = 0x01; cmdWithData(0x8C, data, 1);
    data[0] = 0x01; cmdWithData(0x8D, data, 1);
    data[0] = 0xFF; cmdWithData(0x8E, data, 1);
    data[0] = 0xFF; cmdWithData(0x8F, data, 1);

    data[0] = 0x00; data[1] = 0x20; 
    cmdWithData(0xB6, data, 2);

    uint8_t madctl = 0x08;
    cmdWithData(0x36, &madctl, 1);

    data[0] = 0x05; cmdWithData(0x3A, data, 1);

    data[0] = 0x08; data[1] = 0x08; data[2] = 0x08; data[3] = 0x08; 
    cmdWithData(0x90, data, 4);

    data[0] = 0x06; cmdWithData(0xBD, data, 1);
    data[0] = 0x00; cmdWithData(0xBC, data, 1);

    data[0] = 0x60; data[1] = 0x01; data[2] = 0x04; 
    cmdWithData(0xFF, data, 3);

    data[0] = 0x13; cmdWithData(0xC3, data, 1);
    data[0] = 0x13; cmdWithData(0xC4, data, 1);
    data[0] = 0x22; cmdWithData(0xC9, data, 1);
    data[0] = 0x11; cmdWithData(0xBE, data, 1);

    data[0] = 0x10; data[1] = 0x0E; 
    cmdWithData(0xE1, data, 2);

    data[0] = 0x21; data[1] = 0x0C; data[2] = 0x02; 
    cmdWithData(0xDF, data, 3);

    data[0] = 0x45; data[1] = 0x09; data[2] = 0x08; 
    data[3] = 0x08; data[4] = 0x26; data[5] = 0x2A; 
    cmdWithData(0xF0, data, 6);

    data[0] = 0x43; data[1] = 0x70; data[2] = 0x72; 
    data[3] = 0x36; data[4] = 0x37; data[5] = 0x6F; 
    cmdWithData(0xF1, data, 6);

    data[0] = 0x45; data[1] = 0x09; data[2] = 0x08; 
    data[3] = 0x08; data[4] = 0x26; data[5] = 0x2A; 
    cmdWithData(0xF2, data, 6);

    data[0] = 0x43; data[1] = 0x70; data[2] = 0x72; 
    data[3] = 0x36; data[4] = 0x37; data[5] = 0x6F; 
    cmdWithData(0xF3, data, 6);

    data[0] = 0x1B; data[1] = 0x0B; 
    cmdWithData(0xED, data, 2);

    data[0] = 0x77; cmdWithData(0xAE, data, 1);
    data[0] = 0x63; cmdWithData(0xCD, data, 1);

    data[0] = 0x07; data[1] = 0x07; data[2] = 0x04; 
    data[3] = 0x0E; data[4] = 0x0F; data[5] = 0x09;
    data[6] = 0x07; data[7] = 0x08; data[8] = 0x03; 
    cmdWithData(0x70, data, 9);

    data[0] = 0x34; cmdWithData(0xE8, data, 1);

    data[0] = 0x18; data[1] = 0x0D; data[2] = 0x71; 
    data[3] = 0xED; data[4] = 0x70; data[5] = 0x70;
    data[6] = 0x18; data[7] = 0x0F; data[8] = 0x71; 
    data[9] = 0xEF; data[10] = 0x70; data[11] = 0x70;
    cmdWithData(0x62, data, 12);

    data[0] = 0x18; data[1] = 0x11; data[2] = 0x71; 
    data[3] = 0xF1; data[4] = 0x70; data[5] = 0x70;
    data[6] = 0x18; data[7] = 0x13; data[8] = 0x71; 
    data[9] = 0xF3; data[10] = 0x70; data[11] = 0x70;
    cmdWithData(0x63, data, 12);

    data[0] = 0x28; data[1] = 0x29; data[2] = 0xF1; 
    data[3] = 0x01; data[4] = 0xF1; data[5] = 0x00; data[6] = 0x07;
    cmdWithData(0x64, data, 7);

    data[0] = 0x3C; data[1] = 0x00; data[2] = 0xCD; 
    data[3] = 0x67; data[4] = 0x45; data[5] = 0x45;
    data[6] = 0x10; data[7] = 0x00; data[8] = 0x00; data[9] = 0x00;
    cmdWithData(0x66, data, 10);

    data[0] = 0x00; data[1] = 0x3C; data[2] = 0x00; 
    data[3] = 0x00; data[4] = 0x00; data[5] = 0x01;
    data[6] = 0x54; data[7] = 0x10; data[8] = 0x32; data[9] = 0x98;
    cmdWithData(0x67, data, 10);

    data[0] = 0x10; data[1] = 0x85; data[2] = 0x80; 
    data[3] = 0x00; data[4] = 0x00; data[5] = 0x4E; data[6] = 0x00;
    cmdWithData(0x74, data, 7);

    data[0] = 0x3E; data[1] = 0x07; 
    cmdWithData(0x98, data, 2);

    cmdWithData(0x35, nullptr, 0);
    cmdWithData(0x21, nullptr, 0);

    // Sleep Out
    cmdWithData(0x11, nullptr, 0);
    sleep_ms(120);

    // Display ON
    cmdWithData(0x29, nullptr, 0);
    sleep_ms(20);
}