#pragma once
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/time.h"

class DHT11 {
public:
    struct Reading {
        float temperature;
        float humidity;
        bool valid;
    };

    DHT11(uint pin);
    Reading read();
    bool isDataValid() const { return last_reading.valid; }
    float getTemperature() const { return last_reading.temperature; }
    float getHumidity() const { return last_reading.humidity; }

private:
    uint gpio_pin;
    Reading last_reading;
    
    // Constantes de timing DHT11 (en microsecondes)
    static constexpr uint32_t START_SIGNAL_LOW_TIME = 18000;  // 18ms
    static constexpr uint32_t START_SIGNAL_HIGH_TIME = 30;    // 30µs
    static constexpr uint32_t RESPONSE_LOW_TIME = 80;         // 80µs
    static constexpr uint32_t RESPONSE_HIGH_TIME = 80;        // 80µs
    static constexpr uint32_t DATA_LOW_TIME = 50;             // 50µs
    static constexpr uint32_t DATA_HIGH_0_TIME = 26;          // 26-28µs pour bit 0
    static constexpr uint32_t DATA_HIGH_1_TIME = 70;          // 70µs pour bit 1
    
    bool startSignal();
    bool waitForResponse();
    uint8_t readByte();
    uint32_t measurePulseWidth(bool level, uint32_t timeout_us = 100);
    void setOutput();
    void setInput();
    void setHigh();
    void setLow();
    bool readPin();
};