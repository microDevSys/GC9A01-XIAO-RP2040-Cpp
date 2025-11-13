// C++ class-based implementation
#include "rgb2.h"
#include "pico/stdlib.h"

/*******************************************************
 * Nom du fichier : main.cpp
 * Auteur         : Guillaume Sahuc
 * Date           : 13 novembre 2025
 * Description    : driver led RGB2 sur XIAO SEEED RP2040
 *******************************************************/

namespace {
    inline void init_pin(uint8_t pin) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
    }
}

RGB2::RGB2(bool active_low) : initialized_(false), active_low_(active_low) {
    init();
}

void RGB2::init() {
    if (initialized_) return;
    init_pin(PIN_R);
    init_pin(PIN_G);
    init_pin(PIN_B);
    // Ensure OFF state on init
    const int off_level = active_low_ ? 1 : 0;
    gpio_put(PIN_R, off_level);
    gpio_put(PIN_G, off_level);
    gpio_put(PIN_B, off_level);
    initialized_ = true;
}

void RGB2::set(bool r, bool g, bool b) {
    if (!initialized_) init();
    const int on_level  = active_low_ ? 0 : 1;
    const int off_level = active_low_ ? 1 : 0;
    gpio_put(PIN_R, r ? on_level : off_level);
    gpio_put(PIN_G, g ? on_level : off_level);
    gpio_put(PIN_B, b ? on_level : off_level);
}

void RGB2::off() {
    set(false, false, false);
}

void RGB2::set(uint8_t r, uint8_t g, uint8_t b) {
    // Treat any non-zero as ON (digital control only)
    set(r != 0, g != 0, b != 0);
}
