#include "SDCard.h"
#include <cstdio>
#include <cstring>

/*******************************************************
 * Nom du fichier : SDCard.cpp
 * Auteur         : Guillaume Sahuc
 * Date           : 13 novembre 2025
 * Description    : driver carte SD sur spi0 RP2040
 *******************************************************/


const char* SD_ERROR_MESSAGES[] = {
    "OK",                           // 0 SD_OK
    "NO SD CARD!",                  // 1 SD_NO_CARD
    "INIT ERROR!",                  // 2 SD_INIT_FAILS
    "FILE NOT FOUND!",              // 3 SD_FILE_NOT_FOUND
    "BAD FILE FORMAT!",             // 4 SD_BAD_FILE_FORMAT
    "INCOMPLETE BUFFER READ!",      // 5 SD_INCOMPLETE_BUFFER_READ
    "UNSUPPORTED COMPRESSION!",     // 6 SD_UNSUPPORTED_COMPRESSION
    "WRITE COMMAND FAILS!",         // 7 SD_WRITE_COMMAND_FAILS
    "WRITE DATA FAILS!",            // 8 SD_WRITE_DATA_FAILS
    "READ COMMAND FAILS!",          // 9 SD_READ_COMMAND_FAILS
    "READ TIMEOUT TOKEN!",          // 10 SD_READ_TIMEOUT_TOKEN
    "READ BAD TOKEN!",              // 11 SD_READ_BAD_TOKEN
    "WRITE TIMEOUT BUSY!",          // 12 SD_WRITE_TIMEOUT_BUSY
    "WRITE STATUS ERROR!",          // 13 SD_WRITE_STATUS_ERROR
    "ERASE ERROR!",                 // 14 SD_ERASE_ERROR
    "UNKNOWN ERROR!",               // 15 SD_UNKNOWN_ERROR
    "TIMEOUT ACMD41!"               // 16 SD_INIT_TIMEOUT_ACMD41
};

SDCard::SDCard() 
    : initialized(false), card_type(0), last_status(SD_OK) {
}

SDCard::~SDCard() {
    if (initialized) {
        spi_cs_deselect();
    }
}

bool SDCard::init() {
    // 1) Configurer les broches et SPI à basse vitesse
    configure_spi_pins_and_cs();
    spi_warmup_slow();

    // 2) Reset et passage en mode IDLE
    if (!reset_to_idle()) return false;

    // 3) Détection/version et sortie d'init
    if (!probe_card_and_initialize()) return false;

    // 4) Taille de bloc standard pour SDSC
    if (!set_standard_blocklen_if_needed()) return false;

    // 5) Basculer en vitesse normale
    switch_to_normal_speed();

    // 6) Marquer initialisé
    initialized = true;
    last_status = SD_OK;

    return true;
}

// --- Helpers d'initialisation factorisés ---
void SDCard::configure_spi_pins_and_cs() {
    // Configuration des GPIO
    gpio_init(SDCardConfig::PIN_CS);
    gpio_set_dir(SDCardConfig::PIN_CS, GPIO_OUT);
    gpio_put(SDCardConfig::PIN_CS, 1); // CS désélectionné

    gpio_set_function(SDCardConfig::PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SDCardConfig::PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SDCardConfig::PIN_MISO, GPIO_FUNC_SPI);
}

void SDCard::spi_warmup_slow() {
    // Initialiser SPI0 à basse vitesse et envoyer des clocks de réveil
    spi_init(spi0, SDCardConfig::SPI_BAUDRATE_INIT);
    spi_cs_deselect();
    spi_clock_delay(80); // Envoyer 80 cycles d'horloge
}

bool SDCard::reset_to_idle() {
    // CMD0: go idle state
    if (!send_cmd0()) {
        last_status = SD_NO_CARD;
        return false;
    }
    return true;
}

bool SDCard::probe_card_and_initialize() {
    bool is_sdhc;
    if (!detect_card_version(is_sdhc)) {
        last_status = SD_INIT_FAILS;
        return false;
    }
    if (!initialize_card(is_sdhc)) {
        last_status = SD_INIT_FAILS;
        return false;
    }
    return true;
}

bool SDCard::set_standard_blocklen_if_needed() {
    // CMD16: Définir taille de bloc (512) pour SDSC uniquement
    if (card_type != CARD_TYPE_SDHC) {
        uint8_t response = send_command(CMD16, SDCardConfig::BLOCK_SIZE);
        if (response != 0x00) {
            last_status = SD_INIT_FAILS;
            return false;
        }
    }
    return true;
}

void SDCard::switch_to_normal_speed() {
    // Passer en vitesse normale (inspiré du fichier sd.c)
    spi_set_baudrate(spi0, SDCardConfig::SPI_BAUDRATE_NORMAL);
}

// Méthodes SPI de base inspirées du fichier sd.c
void SDCard::spi_cs_select() {
    // Garantir la bonne vitesse SPI côté SD avant chaque transaction
    // - Pendant l'init: vitesse lente (400 kHz)
    // - Après init: vitesse normale (12 MHz)
    spi_set_baudrate(spi0, initialized ? SDCardConfig::SPI_BAUDRATE_NORMAL : SDCardConfig::SPI_BAUDRATE_INIT);
    gpio_put(SDCardConfig::PIN_CS, 0);
    sleep_us(10);
}

void SDCard::spi_cs_deselect() {
    gpio_put(SDCardConfig::PIN_CS, 1);
    sleep_us(10);
}

uint8_t SDCard::spi_write_read(uint8_t data) {
    uint8_t result;
    spi_write_read_blocking(spi0, &data, &result, 1);
    return result;
}

void SDCard::spi_write_blocking(const uint8_t* src, size_t len) {
    ::spi_write_blocking(spi0, src, len);
}

void SDCard::spi_read_blocking(uint8_t* dst, size_t len) {
    for (size_t i = 0; i < len; i++) {
        dst[i] = spi_write_read(0xFF);
    }
}

void SDCard::spi_clock_delay(uint8_t cycles) {
    // Inspiré de SD_CLKDelay dans sd.c
    for (uint8_t i = 0; i < cycles; i++) {
        spi_write_read(0xFF);
    }
}

// Lecture de bloc 
bool SDCard::read_block(uint32_t block_num, uint8_t* buffer) {
    if (!initialized) {
        last_status = SD_INIT_FAILS;
        return false;
    }
    
    // Pour SDHC, l'adresse est en blocs, sinon en bytes
    uint32_t address = (card_type == CARD_TYPE_SDHC) ? block_num : block_num * SDCardConfig::BLOCK_SIZE;
    
    uint8_t r1 = send_command_keep_cs(CMD17, address);
    if (r1 != 0x00) {
        last_status = SD_READ_COMMAND_FAILS;
        spi_cs_deselect();
        return false;
    }
    // Attendre token 0xFE avec timeout
    uint8_t token = 0xFF;
    if (!wait_start_token(DATA_TOKEN, SDCardConfig::READ_TIMEOUT_MS, token)) {
        last_status = SD_READ_TIMEOUT_TOKEN;
        spi_cs_deselect();
        return false;
    }
    if (token != DATA_TOKEN) {
        last_status = SD_READ_BAD_TOKEN;
        spi_cs_deselect();
        return false;
    }
    // Lire les données
    spi_read_blocking(buffer, SDCardConfig::BLOCK_SIZE);
    // Lire CRC (2 bytes)
    spi_write_read(0xFF);
    spi_write_read(0xFF);
    // Clocks de sortie et CS haut
    spi_write_read(0xFF);
    spi_cs_deselect();
    last_status = SD_OK;
    return true;
}

// Écriture de bloc inspirée de SD_Write_Block dans sd.c
bool SDCard::write_block(uint32_t block_num, const uint8_t* buffer) {
    if (!initialized) {
        last_status = SD_INIT_FAILS;
        return false;
    }
    
    // Adresse selon le type de carte
    uint32_t address = (card_type == CARD_TYPE_SDHC) ? block_num : block_num * SDCardConfig::BLOCK_SIZE;
    
    uint8_t r1 = send_command_keep_cs(CMD24, address);
    if (r1 != 0x00) {
        last_status = SD_WRITE_COMMAND_FAILS;
        spi_cs_deselect();
        return false;
    }
    // Envoyer token et données
    spi_write_read(DATA_TOKEN);
    spi_write_blocking(buffer, SDCardConfig::BLOCK_SIZE);
    // CRC dummy
    spi_write_read(0xFF);
    spi_write_read(0xFF);
    // Réponse data
    uint8_t resp = spi_write_read(0xFF);
    if ((resp & 0x1F) != 0x05) {
        spi_cs_deselect();
        last_status = SD_WRITE_DATA_FAILS;
        return false;
    }
    // Attendre fin du busy
    if (!wait_not_busy(SDCardConfig::WRITE_TIMEOUT_MS)) {
        spi_cs_deselect();
        last_status = SD_WRITE_TIMEOUT_BUSY;
        return false;
    }
    spi_cs_deselect();
    // Vérifier le status avec CMD13 (R2)
    spi_write_read(0xFF);
    spi_cs_select();
    r1 = send_command_keep_cs(CMD13, 0);
    uint8_t r2 = spi_write_read(0xFF);
    spi_cs_deselect();
    spi_write_read(0xFF);
    if (r1 != 0x00 || r2 != 0x00) {
        last_status = SD_WRITE_STATUS_ERROR;
        return false;
    }
    last_status = SD_OK;
    return true;
}

const char* SDCard::get_error_message(SDCard_Status status) {
    if (status >= 0 && status < sizeof(SD_ERROR_MESSAGES)/sizeof(SD_ERROR_MESSAGES[0])) {
        return SD_ERROR_MESSAGES[status];
    }
    return SD_ERROR_MESSAGES[SD_UNKNOWN_ERROR];
}

void SDCard::print_error_info() {
    printf("Last Status: %s\n", get_error_message(last_status));
}

bool SDCard::send_cmd0() {
    const uint32_t timeout_ms = SDCardConfig::INIT_TIMEOUT_MS; // typically 1000ms
    uint32_t start = to_ms_since_boot(get_absolute_time());
    uint8_t response = 0xFF;

    do {
        // Ensure CS is high between attempts and provide some extra clocks
        spi_cs_deselect();
        spi_write_read(0xFF);

        response = send_command(CMD0, 0);

        // Give a few extra clocks after CMD0
        spi_cs_select();
        for (int i = 0; i < 2; ++i) spi_write_read(0xFF);
        spi_cs_deselect();

        if (response == 0x01) {
            return true; // R1_IDLE_STATE
        }

        // Small delay before retry
        sleep_ms(5);
    } while ((to_ms_since_boot(get_absolute_time()) - start) < timeout_ms);

    // Timeout: if we never received anything other than 0xFF it likely means no card present
    if (response == 0xFF) {
        last_status = SD_NO_CARD;
    } 
    return false;
}

bool SDCard::detect_card_version(bool &is_sdhc) {
    // CMD8 (R7): pattern 0x1AA if V2.0+
    uint8_t r7[4] = {0};
    uint8_t r1 = send_command_r7(CMD8, 0x1AA, r7);
    bool is_v2 = (r1 == 0x01) && (r7[2] == 0x01) && (r7[3] == 0xAA);
    // We use this flag to pass HCS to ACMD41; SDHC decision comes after CMD58
    is_sdhc = is_v2;
    return (r1 == 0x01 || r1 == 0x05); // 0x05 if CMD8 illegal (V1), still proceed
}

bool SDCard::initialize_card(bool is_sdhc) {
    // ACMD41: Initialiser la carte
    uint32_t arg = is_sdhc ? 0x40000000 : 0x00000000;
    
    for (int attempts = 0; attempts < 1000; attempts++) {
        uint8_t response = send_app_command(ACMD41, arg);
        if (response == 0x00) {
            // Read OCR (R3) to determine CCS (bit 30) and finalize type
            uint8_t r3[4] = {0};
            (void)send_command_r3(CMD58, 0, r3);
            uint32_t ocr = ((uint32_t)r3[0] << 24) | ((uint32_t)r3[1] << 16) | ((uint32_t)r3[2] << 8) | (uint32_t)r3[3];
            bool ccs = (ocr & 0x40000000u) != 0;
            if (ccs) {
                card_type = CARD_TYPE_SDHC;
            } else {
                card_type = is_sdhc ? CARD_TYPE_SD_V2 : CARD_TYPE_SD_V1;
            }
            return true;
        }
        if ((attempts % 50) == 0) printf("  ACMD41 en cours (R1=0x%02X, essai %d)\n", response, attempts);
        sleep_ms(10);
    }
    // timeout waiting for ACMD41 to complete
    last_status = SD_INIT_TIMEOUT_ACMD41;
    return false;
}

uint8_t SDCard::send_command(uint8_t cmd, uint32_t arg) {
    return send_command_core(cmd, arg, nullptr, 0, /*keep_cs=*/false);
}

uint8_t SDCard::send_command_keep_cs(uint8_t cmd, uint32_t arg) {
    return send_command_core(cmd, arg, nullptr, 0, /*keep_cs=*/true);
}

uint8_t SDCard::send_app_command(uint8_t cmd, uint32_t arg) {
    uint8_t response = send_command(CMD55, 0);
    if (response > 1) return response;
    return send_command(cmd, arg);
}

bool SDCard::wait_ready(uint32_t timeout_ms) {
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - start) < timeout_ms) {
        if (spi_write_read(0xFF) == 0xFF) {
            return true;
        }
        sleep_us(100);
    }
    return false;
}

bool SDCard::wait_not_busy(uint32_t timeout_ms) {
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - start) < timeout_ms) {
        if (spi_write_read(0xFF) == 0xFF) return true;
    }
    return false;
}

bool SDCard::wait_start_token(uint8_t expected_token, uint32_t timeout_ms, uint8_t &token_out) {
    uint32_t start = to_ms_since_boot(get_absolute_time());
    token_out = 0xFF;
    do {
        token_out = spi_write_read(0xFF);
        if (token_out != 0xFF) break;
    } while ((to_ms_since_boot(get_absolute_time()) - start) < timeout_ms);
    return (token_out == expected_token);
}

bool SDCard::read_card_ocr(uint32_t &ocr) {
    uint8_t r3[4] = {0};
    uint8_t r1 = send_command_r3(CMD58, 0, r3);
    if (r1 > 1) return false;
    ocr = ((uint32_t)r3[0] << 24) | ((uint32_t)r3[1] << 16) | ((uint32_t)r3[2] << 8) | (uint32_t)r3[3];
    return true;
}

uint8_t SDCard::send_command_r7(uint8_t cmd, uint32_t arg, uint8_t out[4]) {
    return send_command_core(cmd, arg, out, 4, /*keep_cs=*/false);
}

uint8_t SDCard::send_command_r3(uint8_t cmd, uint32_t arg, uint8_t out[4]) {
    return send_command_core(cmd, arg, out, 4, /*keep_cs=*/false);
}

uint8_t SDCard::send_command_core(uint8_t cmd, uint32_t arg, uint8_t* out, size_t out_len, bool keep_cs) {
    spi_cs_select();
    if (cmd != CMD0) {
        if (!wait_ready()) {
            spi_cs_deselect();
            return 0xFF;
        }
    }

    // Build command packet
    uint8_t command[6];
    command[0] = 0x40 | cmd;
    command[1] = (arg >> 24) & 0xFF;
    command[2] = (arg >> 16) & 0xFF;
    command[3] = (arg >> 8) & 0xFF;
    command[4] = arg & 0xFF;
    // CRC rules: valid CRC is only mandatory in idle for CMD0/CMD8
    if (cmd == CMD0)      command[5] = 0x95;
    else if (cmd == CMD8) command[5] = 0x87;
    else                  command[5] = 0x01; // dummy CRC + end bit

    spi_write_blocking(command, 6);

    // Read R1 (retry up to 20 bytes)
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 20; i++) {
        r1 = spi_write_read(0xFF);
        if (!(r1 & 0x80)) break;
    }

    // If additional response bytes are requested (R3/R7), read them now
    if (out && out_len) {
        for (size_t i = 0; i < out_len; ++i) {
            out[i] = spi_write_read(0xFF);
        }
    }

    if (!keep_cs) {
        spi_cs_deselect();
        // Provide at least 8 extra clocks between commands
        spi_write_read(0xFF);
    }
    return r1;
}

bool SDCard::test_basic_read() {
    uint8_t test_buffer[SDCardConfig::BLOCK_SIZE];
    return read_block(0, test_buffer);
}

// Partial/slice read similar to Sd2Card::readData
bool SDCard::read_data(uint32_t block, uint16_t offset, uint16_t count, uint8_t* dst) {
    if (count == 0) return true;
    if (!initialized) return false;
    if ((uint32_t)offset + (uint32_t)count > SDCardConfig::BLOCK_SIZE) return false;

    if (!in_block_ || block != block_ || offset < offset_) {
        // Start a new block read
        block_ = block;
        uint32_t address = (card_type == CARD_TYPE_SDHC) ? block : block * SDCardConfig::BLOCK_SIZE;
        uint8_t r1 = send_command_keep_cs(CMD17, address);
        if (r1 != 0x00) { spi_cs_deselect(); return false; }
        uint8_t token;
        if (!wait_start_token(DATA_TOKEN, SDCardConfig::READ_TIMEOUT_MS, token)) { spi_cs_deselect(); return false; }
        offset_ = 0;
        in_block_ = true;
    }
    // Skip to desired offset
    while (offset_ < offset) { spi_write_read(0xFF); offset_++; }
    // Read data
    for (uint16_t i = 0; i < count; i++) { dst[i] = spi_write_read(0xFF); }
    offset_ += count;
    if (!partial_block_read_ || offset_ >= SDCardConfig::BLOCK_SIZE) {
        read_end();
    }
    return true;
}

void SDCard::read_end() {
    if (in_block_) {
        // Skip remaining bytes in block + CRC
        while (offset_++ < SDCardConfig::BLOCK_SIZE + 2) { spi_write_read(0xFF); }
        spi_cs_deselect();
        in_block_ = false;
    }
}

bool SDCard::read_start(uint32_t block) {
    if (!initialized) return false;
    uint32_t address = (card_type == CARD_TYPE_SDHC) ? block : block * SDCardConfig::BLOCK_SIZE;
    uint8_t r1 = send_command_keep_cs(CMD18, address);
    if (r1 != 0x00) { spi_cs_deselect(); return false; }
    // The first data token will be waited by the caller using read_data or manual loop
    in_block_ = false; offset_ = 0; block_ = block;
    return true;
}

bool SDCard::read_stop() {
    // Send CMD12 to stop transmission
    uint8_t r1 = send_command(CMD12, 0);
    (void)r1; // ignore errors for now
    return true;
}

bool SDCard::write_start(uint32_t block, uint32_t eraseCount) {
    if (!initialized) return false;
    // Pre-erase blocks (optional)
    (void)eraseCount;
    // Try ACMD23 if eraseCount > 0
    if (eraseCount > 0) {
        uint8_t r = send_app_command(ACMD23, eraseCount);
        (void)r;
    }
    uint32_t address = (card_type == CARD_TYPE_SDHC) ? block : block * SDCardConfig::BLOCK_SIZE;
    uint8_t r1 = send_command_keep_cs(CMD25, address);
    if (r1 != 0x00) { spi_cs_deselect(); return false; }
    return true;
}

bool SDCard::write_data(const uint8_t* src) {
    // Send multiple write token and data
    spi_write_read(WRITE_MULTIPLE_TOKEN);
    spi_write_blocking(src, SDCardConfig::BLOCK_SIZE);
    spi_write_read(0xFF); // CRC
    spi_write_read(0xFF);
    uint8_t resp = spi_write_read(0xFF);
    if ((resp & 0x1F) != 0x05) { spi_cs_deselect(); return false; }
    if (!wait_not_busy(SDCardConfig::WRITE_TIMEOUT_MS)) { spi_cs_deselect(); return false; }
    return true;
}

bool SDCard::write_stop() {
    if (!wait_not_busy(SDCardConfig::WRITE_TIMEOUT_MS)) return false;
    spi_write_read(STOP_TRAN_TOKEN);
    if (!wait_not_busy(SDCardConfig::WRITE_TIMEOUT_MS)) return false;
    spi_cs_deselect();
    return true;
}

bool SDCard::read_register(uint8_t cmd, void* buf) {
    uint8_t r1 = send_command_keep_cs(cmd, 0);
    if (r1 > 1) { spi_cs_deselect(); return false; }
    uint8_t token;
    if (!wait_start_token(DATA_TOKEN, SDCardConfig::READ_TIMEOUT_MS, token)) { spi_cs_deselect(); return false; }
    uint8_t* dst = reinterpret_cast<uint8_t*>(buf);
    for (uint16_t i = 0; i < 16; i++) { dst[i] = spi_write_read(0xFF); }
    spi_write_read(0xFF); spi_write_read(0xFF); // CRC
    spi_cs_deselect();
    return true;
}

uint32_t SDCard::card_size() {
    uint8_t csd[16];
    if (!read_register(CMD9, csd)) return 0;
    // Determine CSD structure version
    uint8_t csd_structure = (csd[0] >> 6) & 0x03;
    if (csd_structure == 1) {
        // CSD v2.0
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
        return (c_size + 1) << 10; // number of 512B blocks
    } else if (csd_structure == 0) {
        // CSD v1.0
        uint8_t read_bl_len = csd[5] & 0x0F;
        uint16_t c_size = ((uint16_t)(csd[6] & 0x03) << 10) | ((uint16_t)csd[7] << 2) | ((csd[8] >> 6) & 0x03);
        uint8_t c_size_mult = ((csd[9] & 0x03) << 1) | ((csd[10] >> 7) & 0x01);
        uint32_t blocknr = (uint32_t)(c_size + 1) << (c_size_mult + 2);
        uint32_t block_len = 1UL << read_bl_len;
        return (blocknr * block_len) / 512;
    }
    return 0;
}

bool SDCard::erase_single_block_enable() {
    // Not all cards expose this cleanly; attempt read CSD and check ERASE_BLK_EN bit
    uint8_t csd[16];
    if (!read_register(CMD9, csd)) return false;
    // ERASE_BLK_EN is bit 46 in CSD v1; for simplicity, assume supported if bit set in csd[10] bit 0
    bool enable = (csd[10] & 0x01) != 0;
    return enable;
}

bool SDCard::erase(uint32_t firstBlock, uint32_t lastBlock) {
    // Use byte addressing for SDSC
    if (card_type != CARD_TYPE_SDHC) {
        firstBlock <<= 9;
        lastBlock <<= 9;
    }
    uint8_t r1;
    r1 = send_command(CMD32, firstBlock); if (r1) { last_status = SD_ERASE_ERROR; return false; }
    r1 = send_command(CMD33, lastBlock); if (r1) { last_status = SD_ERASE_ERROR; return false; }
    r1 = send_command(CMD38, 0); if (r1) { last_status = SD_ERASE_ERROR; return false; }
    if (!wait_not_busy(SDCardConfig::ERASE_TIMEOUT_MS)) { last_status = SD_ERASE_ERROR; return false; }
    return true;
}

uint8_t SDCard::is_busy() {
    spi_cs_select();
    uint8_t b = spi_write_read(0xFF);
    spi_cs_deselect();
    return (b != 0xFF);
}

// Fonctions globales utilitaires
void SD_print_buffer_hex(const uint8_t* buffer, size_t length, size_t bytes_per_line) {
    for (size_t i = 0; i < length; i += bytes_per_line) {
        printf("%04X: ", (unsigned int)i);
        
        // Afficher hex
        for (size_t j = 0; j < bytes_per_line && (i + j) < length; j++) {
            printf("%02X ", buffer[i + j]);
        }
        
        // Espacement
        for (size_t j = length - i; j < bytes_per_line; j++) {
            printf("   ");
        }
        
        // Afficher ASCII
        printf("| ");
        for (size_t j = 0; j < bytes_per_line && (i + j) < length; j++) {
            uint8_t c = buffer[i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("\n");
    }
}

void SD_delay_ms(uint32_t ms) {
    sleep_ms(ms);
}

// Formatage FAT32
bool SDCard::format_fat32(const char* volume_label) {
    if (!initialized) {
        last_status = SD_INIT_FAILS;
        return false;
    }
    
    printf("Début du formatage FAT32...\n");
    
    // Obtenir la taille de la carte en blocs
    uint32_t total_sectors = card_size();
    if (total_sectors == 0) {
        printf("Erreur: impossible de lire la taille de la carte\n");
        return false;
    }
    
    printf("Taille de la carte: %lu secteurs (%lu MB)\n", 
           total_sectors, (total_sectors * 512UL) / (1024UL * 1024UL));
    
    // Calcul des paramètres FAT32
    uint32_t reserved_sectors = 32;  // Standard pour FAT32
    uint8_t num_fats = 2;            // 2 copies de la FAT
    uint32_t sectors_per_cluster = 8; // 4KB clusters (8 * 512)
    
    // Ajuster sectors_per_cluster selon la taille
    if (total_sectors > 32 * 1024 * 1024 / 512) {      // > 16GB
        sectors_per_cluster = 64;  // 32KB
    } else if (total_sectors > 16 * 1024 * 1024 / 512) { // > 8GB
        sectors_per_cluster = 32;  // 16KB
    } else if (total_sectors > 8 * 1024 * 1024 / 512) {  // > 4GB
        sectors_per_cluster = 16;  // 8KB
    }
    
    // Calculer la taille de la FAT
    uint32_t data_sectors = total_sectors - reserved_sectors;
    uint32_t clusters = data_sectors / sectors_per_cluster;
    uint32_t fat_size = ((clusters + 2) * 4 + 511) / 512; // 4 bytes par entrée, arrondi
    
    uint32_t partition_start = 2048;  // Alignement standard 1MB
    uint32_t fat_start = partition_start + reserved_sectors;
    uint32_t data_start = fat_start + (num_fats * fat_size);
    uint32_t root_dir_cluster = 2;
    
    printf("Paramètres:\n");
    printf("  Secteurs par cluster: %lu\n", sectors_per_cluster);
    printf("  Secteurs réservés: %lu\n", reserved_sectors);
    printf("  Taille FAT: %lu secteurs\n", fat_size);
    printf("  Début partition: %lu\n", partition_start);
    printf("  Début données: %lu\n", data_start);
    
    // Buffer de travail
    uint8_t buffer[512];
    
    // 1. Créer le MBR (secteur 0)
    printf("Écriture du MBR...\n");
    memset(buffer, 0, 512);
    
    // Signature MBR
    buffer[510] = 0x55;
    buffer[511] = 0xAA;
    
    // Partition 1 (offset 446)
    buffer[446] = 0x80;  // Bootable
    buffer[447] = 0x00;  // CHS start (ignoré)
    buffer[448] = 0x00;
    buffer[449] = 0x00;
    buffer[450] = 0x0C;  // Type: FAT32 LBA
    buffer[451] = 0xFF;  // CHS end (ignoré)
    buffer[452] = 0xFF;
    buffer[453] = 0xFF;
    
    // LBA start (little endian)
    buffer[454] = partition_start & 0xFF;
    buffer[455] = (partition_start >> 8) & 0xFF;
    buffer[456] = (partition_start >> 16) & 0xFF;
    buffer[457] = (partition_start >> 24) & 0xFF;
    
    // Partition size
    uint32_t partition_size = total_sectors - partition_start;
    buffer[458] = partition_size & 0xFF;
    buffer[459] = (partition_size >> 8) & 0xFF;
    buffer[460] = (partition_size >> 16) & 0xFF;
    buffer[461] = (partition_size >> 24) & 0xFF;
    
    if (!write_block(0, buffer)) {
        printf("Erreur écriture MBR\n");
        return false;
    }
    
    // 2. Créer le Boot Sector (secteur partition_start)
    printf("Écriture du Boot Sector...\n");
    memset(buffer, 0, 512);
    
    // Jump instruction
    buffer[0] = 0xEB;
    buffer[1] = 0x58;
    buffer[2] = 0x90;
    
    // OEM Name
    memcpy(buffer + 3, "custom01", 8);
    
    // BIOS Parameter Block
    buffer[11] = 0x00; buffer[12] = 0x02; // Bytes per sector (512)
    buffer[13] = sectors_per_cluster;      // Sectors per cluster
    buffer[14] = reserved_sectors & 0xFF;  // Reserved sectors
    buffer[15] = (reserved_sectors >> 8) & 0xFF;
    buffer[16] = num_fats;                 // Number of FATs
    buffer[17] = 0x00; buffer[18] = 0x00;  // Root entries (0 for FAT32)
    buffer[19] = 0x00; buffer[20] = 0x00;  // Total sectors 16-bit (0 for FAT32)
    buffer[21] = 0xF8;                     // Media descriptor
    buffer[22] = 0x00; buffer[23] = 0x00;  // FAT size 16-bit (0 for FAT32)
    buffer[24] = 0x3F; buffer[25] = 0x00;  // Sectors per track
    buffer[26] = 0xFF; buffer[27] = 0x00;  // Number of heads
    
    // Hidden sectors (partition start)
    buffer[28] = partition_start & 0xFF;
    buffer[29] = (partition_start >> 8) & 0xFF;
    buffer[30] = (partition_start >> 16) & 0xFF;
    buffer[31] = (partition_start >> 24) & 0xFF;
    
    // Total sectors 32-bit
    buffer[32] = partition_size & 0xFF;
    buffer[33] = (partition_size >> 8) & 0xFF;
    buffer[34] = (partition_size >> 16) & 0xFF;
    buffer[35] = (partition_size >> 24) & 0xFF;
    
    // FAT32 specific
    buffer[36] = fat_size & 0xFF;          // FAT size 32-bit
    buffer[37] = (fat_size >> 8) & 0xFF;
    buffer[38] = (fat_size >> 16) & 0xFF;
    buffer[39] = (fat_size >> 24) & 0xFF;
    buffer[40] = 0x00; buffer[41] = 0x00;  // Flags
    buffer[42] = 0x00; buffer[43] = 0x00;  // Version
    buffer[44] = root_dir_cluster & 0xFF;  // Root cluster
    buffer[45] = (root_dir_cluster >> 8) & 0xFF;
    buffer[46] = (root_dir_cluster >> 16) & 0xFF;
    buffer[47] = (root_dir_cluster >> 24) & 0xFF;
    buffer[48] = 0x01; buffer[49] = 0x00;  // FSInfo sector
    buffer[50] = 0x06; buffer[51] = 0x00;  // Backup boot sector
    
    // Reserved
    for (int i = 52; i < 64; i++) buffer[i] = 0x00;
    
    // Extended boot signature
    buffer[64] = 0x00;  // Drive number
    buffer[65] = 0x00;  // Reserved
    buffer[66] = 0x29;  // Extended boot signature
    
    // Volume serial number (random)
    uint32_t serial = to_ms_since_boot(get_absolute_time());
    buffer[67] = serial & 0xFF;
    buffer[68] = (serial >> 8) & 0xFF;
    buffer[69] = (serial >> 16) & 0xFF;
    buffer[70] = (serial >> 24) & 0xFF;
    
    // Volume label (11 bytes, padded with spaces)
    char label[12];
    snprintf(label, 12, "%-11s", volume_label);
    memcpy(buffer + 71, label, 11);
    
    // File system type
    memcpy(buffer + 82, "FAT32   ", 8);
    
    // Boot signature
    buffer[510] = 0x55;
    buffer[511] = 0xAA;
    
    if (!write_block(partition_start, buffer)) {
        printf("Erreur écriture Boot Sector\n");
        return false;
    }
    
    // 3. Créer le FSInfo (secteur partition_start + 1)
    printf("Écriture FSInfo...\n");
    memset(buffer, 0, 512);
    
    buffer[0] = 0x52; buffer[1] = 0x52; buffer[2] = 0x61; buffer[3] = 0x41; // Signature
    buffer[484] = 0x72; buffer[485] = 0x72; buffer[486] = 0x41; buffer[487] = 0x61;
    
    // Free cluster count (0xFFFFFFFF = unknown)
    buffer[488] = 0xFF; buffer[489] = 0xFF; buffer[490] = 0xFF; buffer[491] = 0xFF;
    
    // Next free cluster (0xFFFFFFFF = unknown)
    buffer[492] = 0xFF; buffer[493] = 0xFF; buffer[494] = 0xFF; buffer[495] = 0xFF;
    
    buffer[510] = 0x55;
    buffer[511] = 0xAA;
    
    if (!write_block(partition_start + 1, buffer)) {
        printf("Erreur écriture FSInfo\n");
        return false;
    }
    
    // 4. Initialiser les FATs
    printf("Initialisation des FATs...\n");
    memset(buffer, 0, 512);
    
    // Premier secteur de FAT avec les entrées réservées
    buffer[0] = 0xF8; buffer[1] = 0xFF; buffer[2] = 0xFF; buffer[3] = 0x0F; // Entrée 0
    buffer[4] = 0xFF; buffer[5] = 0xFF; buffer[6] = 0xFF; buffer[7] = 0xFF; // Entrée 1
    buffer[8] = 0xFF; buffer[9] = 0xFF; buffer[10] = 0xFF; buffer[11] = 0x0F; // Entrée 2 (root, EOC)
    
    // Écrire le premier secteur des deux FATs
    if (!write_block(fat_start, buffer)) {
        printf("Erreur écriture FAT1\n");
        return false;
    }
    if (!write_block(fat_start + fat_size, buffer)) {
        printf("Erreur écriture FAT2\n");
        return false;
    }
    
    // Remplir le reste des FATs avec des zéros
    memset(buffer, 0, 512);
    for (uint32_t i = 1; i < fat_size; i++) {
        if ((i % 100) == 0) {
            printf("  FAT: %lu/%lu\r", i, fat_size);
        }
        if (!write_block(fat_start + i, buffer)) {
            printf("\nErreur écriture FAT1 secteur %lu\n", i);
            return false;
        }
        if (!write_block(fat_start + fat_size + i, buffer)) {
            printf("\nErreur écriture FAT2 secteur %lu\n", i);
            return false;
        }
    }
    printf("\n");
    
    // 5. Initialiser le répertoire racine
    printf("Initialisation du répertoire racine...\n");
    memset(buffer, 0, 512);
    
    for (uint32_t i = 0; i < sectors_per_cluster; i++) {
        if (!write_block(data_start + i, buffer)) {
            printf("Erreur écriture root dir\n");
            return false;
        }
    }
    
    printf("Formatage FAT32 terminé avec succès!\n");
    last_status = SD_OK;
    return true;
}