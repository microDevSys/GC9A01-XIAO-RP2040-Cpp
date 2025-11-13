#include "DHT11.h"
#include <cstdio>

/*******************************************************
 * Nom du fichier : DHT11.cpp
 * Auteur         : Guillaume Sahuc
 * Date           : 13 novembre 2025
 * Description    : driver capteur température & humidité DHT11
 *******************************************************/

DHT11::DHT11(uint pin) : gpio_pin(pin) {
    gpio_init(gpio_pin);
    last_reading = {0.0f, 0.0f, false};
}

DHT11::Reading DHT11::read() {
    Reading reading = {0.0f, 0.0f, false};
    
    // Envoyer signal de démarrage
    if (!startSignal()) {
        printf("DHT11: Échec du signal de démarrage\n");
        last_reading = reading;
        return reading;
    }
    
    // Attendre la réponse du DHT11
    if (!waitForResponse()) {
        printf("DHT11: Pas de réponse du capteur\n");
        last_reading = reading;
        return reading;
    }
    
    // Lire les 5 bytes de données (40 bits)
    uint8_t data[5] = {0};
    for (int i = 0; i < 5; i++) {
        data[i] = readByte();
    }
    
    // Vérifier le checksum
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        printf("DHT11: Erreur checksum (calculé: %d, reçu: %d)\n", checksum, data[4]);
        last_reading = reading;
        return reading;
    }
    
    // Convertir les données
    reading.humidity = (float)data[0] + (float)data[1] / 10.0f;
    reading.temperature = (float)data[2] + (float)data[3] / 10.0f;
    reading.valid = true;
    
    printf("DHT11: T=%.1f°C, H=%.1f%% (données: %02X %02X %02X %02X %02X)\n", 
           reading.temperature, reading.humidity, 
           data[0], data[1], data[2], data[3], data[4]);
    
    last_reading = reading;
    return reading;
}

bool DHT11::startSignal() {
    // 1. Envoyer signal de démarrage : LOW pendant 18ms
    setOutput();
    setLow();
    sleep_us(START_SIGNAL_LOW_TIME);
    
    // 2. Mettre HIGH pendant 30µs
    setHigh();
    sleep_us(START_SIGNAL_HIGH_TIME);
    
    // 3. Passer en mode input pour écouter la réponse
    setInput();
    
    return true;
}

bool DHT11::waitForResponse() {
    // Attendre que le DHT11 tire la ligne LOW (réponse)
    uint32_t timeout = 100; // 100µs timeout
    while (readPin() && timeout--) {
        sleep_us(1);
    }
    if (timeout == 0) return false;
    
    // Mesurer la durée LOW (doit être ~80µs)
    uint32_t low_time = measurePulseWidth(false, 100);
    if (low_time < 60 || low_time > 100) return false;
    
    // Mesurer la durée HIGH (doit être ~80µs)
    uint32_t high_time = measurePulseWidth(true, 100);
    if (high_time < 60 || high_time > 100) return false;
    
    return true;
}

uint8_t DHT11::readByte() {
    uint8_t byte_value = 0;
    
    for (int bit = 7; bit >= 0; bit--) {
        // Attendre la fin du LOW (début de bit)
        while (!readPin()) {
            sleep_us(1);
        }
        
        // Mesurer la durée du HIGH pour déterminer si c'est un 0 ou 1
        uint32_t high_duration = measurePulseWidth(true, 100);
        
        if (high_duration > 40) {  // Plus de 40µs = bit 1
            byte_value |= (1 << bit);
        }
        // Moins de 40µs = bit 0 (rien à faire, déjà 0)
    }
    
    return byte_value;
}

uint32_t DHT11::measurePulseWidth(bool level, uint32_t timeout_us) {
    uint32_t start_time = time_us_32();
    uint32_t current_time;
    
    // Attendre que le niveau change au niveau souhaité
    while (readPin() != level) {
        current_time = time_us_32();
        if ((current_time - start_time) > timeout_us) {
            return 0; // Timeout
        }
    }
    
    // Mesurer la durée du niveau
    uint32_t pulse_start = time_us_32();
    while (readPin() == level) {
        current_time = time_us_32();
        if ((current_time - pulse_start) > timeout_us) {
            return timeout_us; // Timeout, retourner la durée max
        }
    }
    
    return time_us_32() - pulse_start;
}

void DHT11::setOutput() {
    gpio_set_dir(gpio_pin, GPIO_OUT);
}

void DHT11::setInput() {
    gpio_set_dir(gpio_pin, GPIO_IN);
    gpio_pull_up(gpio_pin); // Pull-up pour la ligne de données
}

void DHT11::setHigh() {
    gpio_put(gpio_pin, 1);
}

void DHT11::setLow() {
    gpio_put(gpio_pin, 0);
}

bool DHT11::readPin() {
    return gpio_get(gpio_pin);
}