// Simple RGB LED control for Raspberry Pi Pico (RP2040)
// Pin allocation:
//   R -> GPIO17
//   G -> GPIO16
//   B -> GPIO25

#pragma once

#include <cstdint>

class RGB2 {
public:
	// Fixed pin mapping per request
	static constexpr uint8_t PIN_R = 17; // GPIO17
	static constexpr uint8_t PIN_G = 16; // GPIO16
	static constexpr uint8_t PIN_B = 25; // GPIO25

	// By default consider wiring active-low
	explicit RGB2(bool active_low = true);

	// Initialize GPIOs for the RGB LED (sets all channels OFF)
	void init();

	// Set the RGB LED channels as booleans (true = ON, false = OFF)
	void set(bool r, bool g, bool b);

	// Convenience: turn all channels OFF
	void off();

	// Set using 8-bit intensity values; non-zero is treated as ON
	// (This is digital on/off, not PWM brightness.)
	void set(uint8_t r, uint8_t g, uint8_t b);

	// Configure whether the LED channels are active-low (true) or active-high (false)
	void setActiveLow(bool active_low) { active_low_ = active_low; }
	bool isActiveLow() const { return active_low_; }

private:
	bool initialized_ = false;
	bool active_low_ = true;
};
