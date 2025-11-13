#pragma once

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <string>
#include <vector>
#include <cstring>

// Codes d'erreur
enum SDCard_Status {
    SD_OK = 0,
    SD_NO_CARD = 1,
    SD_INIT_FAILS = 2,
    SD_FILE_NOT_FOUND = 3,
    SD_BAD_FILE_FORMAT = 4,
    SD_INCOMPLETE_BUFFER_READ = 5,
    SD_UNSUPPORTED_COMPRESSION = 6,
    SD_WRITE_COMMAND_FAILS = 7,
    SD_WRITE_DATA_FAILS = 8,
    SD_READ_COMMAND_FAILS = 9,
    SD_READ_TIMEOUT_TOKEN = 10,
    SD_READ_BAD_TOKEN = 11,
    SD_WRITE_TIMEOUT_BUSY = 12,
    SD_WRITE_STATUS_ERROR = 13,
    SD_ERASE_ERROR = 14,
    SD_UNKNOWN_ERROR = 15,
    SD_INIT_TIMEOUT_ACMD41 = 16
};

// États de fonctionnement de la SD
enum SDCard_Command {
    SD_INACTIVE = 0,
    SD_FILE_READING = 1,
    SD_FILE_WRITING = 2,
    SD_IMAGE_READING = 3
};

// Configuration pour la carte SD
struct SDCardConfig {
    static constexpr int SPI_PORT = 0;
    static constexpr int PIN_SCK = 2;
    static constexpr int PIN_MOSI = 3;
    static constexpr int PIN_MISO = 4;
    static constexpr int PIN_CS = 6;
    static constexpr int SPI_BAUDRATE_INIT = 400000;    // 400kHz pour init
    static constexpr int SPI_BAUDRATE_NORMAL = 12000000; // 12MHz pour opérations
    static constexpr int BLOCK_SIZE = 512;
    static constexpr int READ_BUFFER_SIZE = 512;        // Taille du buffer de lecture
    // Timeouts (ms)
    static constexpr uint32_t INIT_TIMEOUT_MS = 1000;
    static constexpr uint32_t READ_TIMEOUT_MS = 300;
    static constexpr uint32_t WRITE_TIMEOUT_MS = 600;
    static constexpr uint32_t ERASE_TIMEOUT_MS = 3000;
};

// Structure pour les informations de fichier
struct FileInfo {
    char name[256];
    uint32_t size;
    bool is_directory;
};

class SDCard {
private:
    bool initialized;
    uint8_t card_type;
    SDCard_Status last_status;
    
    // Types de cartes SD
    static constexpr uint8_t CARD_TYPE_SD_V1 = 0;
    static constexpr uint8_t CARD_TYPE_SD_V2 = 1;
    static constexpr uint8_t CARD_TYPE_SDHC = 2;
    
    // Commandes SD
    static constexpr uint8_t CMD0 = 0;
    static constexpr uint8_t CMD1 = 1;
    static constexpr uint8_t CMD8 = 8;
    static constexpr uint8_t CMD9 = 9;
    static constexpr uint8_t CMD10 = 10;
    static constexpr uint8_t CMD12 = 12;
    static constexpr uint8_t CMD13 = 13;
    static constexpr uint8_t CMD16 = 16;
    static constexpr uint8_t CMD17 = 17;
    static constexpr uint8_t CMD18 = 18;
    static constexpr uint8_t CMD24 = 24;
    static constexpr uint8_t CMD25 = 25;
    static constexpr uint8_t CMD32 = 32;
    static constexpr uint8_t CMD33 = 33;
    static constexpr uint8_t CMD38 = 38;
    static constexpr uint8_t CMD55 = 55;
    static constexpr uint8_t CMD58 = 58;
    static constexpr uint8_t ACMD23 = 23;
    static constexpr uint8_t ACMD41 = 41;

    // Tokens
    static constexpr uint8_t DATA_TOKEN = 0xFE;
    static constexpr uint8_t WRITE_MULTIPLE_TOKEN = 0xFC;
    static constexpr uint8_t STOP_TRAN_TOKEN = 0xFD;

    // Méthodes privées de base SPI
    void spi_cs_select();
    void spi_cs_deselect();
    uint8_t spi_write_read(uint8_t data);
    void spi_write_blocking(const uint8_t* src, size_t len);
    void spi_read_blocking(uint8_t* dst, size_t len);
    void spi_clock_delay(uint8_t cycles);
    
    // Méthodes d'initialisation
    uint8_t send_command(uint8_t cmd, uint32_t arg);
    // Variant that keeps CS asserted after getting R1 (used for data transfers)
    uint8_t send_command_keep_cs(uint8_t cmd, uint32_t arg);
    // Variants that read additional 32-bit response words (keep CS while reading):
    uint8_t send_command_r7(uint8_t cmd, uint32_t arg, uint8_t out[4]);
    uint8_t send_command_r3(uint8_t cmd, uint32_t arg, uint8_t out[4]);
    uint8_t send_app_command(uint8_t cmd, uint32_t arg);
    // Core helper to factorize command sending
    uint8_t send_command_core(uint8_t cmd, uint32_t arg, uint8_t* out, size_t out_len, bool keep_cs);
    bool send_cmd0();
    bool detect_card_version(bool &is_sdhc);
    bool initialize_card(bool is_sdhc);
    bool wait_ready(uint32_t timeout_ms = 500);
    bool wait_not_busy(uint32_t timeout_ms);
    bool wait_start_token(uint8_t expected_token, uint32_t timeout_ms, uint8_t &token_out);
    bool read_card_ocr(uint32_t &ocr);
    
    // Partial read state
    bool in_block_ = false;
    uint32_t block_ = 0;
    uint16_t offset_ = 0;
    bool partial_block_read_ = false;

    // Helpers pour factoriser l'initialisation
    void configure_spi_pins_and_cs();
    void spi_warmup_slow();
    bool reset_to_idle();
    bool probe_card_and_initialize();
    bool set_standard_blocklen_if_needed();
    void switch_to_normal_speed();
    
public:
    SDCard();
    ~SDCard();
    
    // Méthodes d'initialisation et état
    bool init();
    bool is_initialized() const { return initialized; }
    SDCard_Status get_last_status() const { return last_status; }
    
    // Méthodes de base pour les blocs
    bool read_block(uint32_t block_num, uint8_t* buffer);
    bool write_block(uint32_t block_num, const uint8_t* buffer);
    // Partial/slice read helpers
    bool read_data(uint32_t block, uint16_t offset, uint16_t count, uint8_t* dst);
    void read_end();
    void partial_block_read(uint8_t value) { read_end(); partial_block_read_ = (value != 0); }
    // Multi-block read/write
    bool read_start(uint32_t block);
    bool read_stop();
    bool write_start(uint32_t block, uint32_t eraseCount = 0);
    bool write_data(const uint8_t* src);
    bool write_stop();
    // Register access and capacity
    bool read_register(uint8_t cmd, void* buf16);
    uint32_t card_size();
    bool erase_single_block_enable();
    bool erase(uint32_t firstBlock, uint32_t lastBlock);
    uint8_t is_busy();
    bool test_basic_read();
    
    // Formatage FAT32
    bool format_fat32(const char* volume_label = "PICO_SD");
  
    // Messages d'erreur
    const char* get_error_message(SDCard_Status status);
    void print_error_info();
};

// Fonctions globales utilitaires inspirées du fichier sd.c
extern const char* SD_ERROR_MESSAGES[];
void SD_print_buffer_hex(const uint8_t* buffer, size_t length, size_t bytes_per_line = 16);
void SD_delay_ms(uint32_t ms);