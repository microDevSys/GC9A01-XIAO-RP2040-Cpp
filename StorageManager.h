#pragma once

#include "SDCard.h"
#include "FAT32.h"
#include "lib_bmp.h"
#include "Color.h"
#include <vector>
#include <string>

// Forward declarations
class FAT32;
struct FileInfo;

class StorageManager {
private:
    SDCard* sd_card;
    FAT32* fat32_fs;
    bool fat32_mounted;

public:
    explicit StorageManager(SDCard* card);
    ~StorageManager();

    // Montage du système de fichiers
    bool mount_fat32();
    bool is_fat32_mounted() const { return fat32_mounted && fat32_fs && fat32_fs->is_initialized(); }
    
    // Accès au système FAT32 (pour compatibilité)
    FAT32* get_fat32_fs() const { return fat32_fs; }
    
    // Accès direct à la carte SD
    SDCard* get_sd_card() const { return sd_card; }

    // Opérations sur les fichiers
    SDCard_Status read_text_file(const char* filename);
    SDCard_Status write_text_file(const char* filename, const uint8_t* buffer, uint16_t length);
    bool file_exists(const char* filename);
    uint32_t get_file_size(const char* filename);
    std::vector<FileInfo> list_directory(const char* path = nullptr);
    bool rename_file(const char* old_name, const char* new_name);

    // Opérations BMP
    SDCard_Status read_bmp_file_info(const char* filename, t_bmp* bmp_info);
    // Fonction unifiée: s'adapte automatiquement aux BMP 16-bit (RGB565) et 24-bit (RGB888)
    // - Si pixel_callback_rgb est fourni, les pixels seront fournis en RGB888 (conversion si nécessaire)
    // - Sinon, si pixel_callback_565 est fourni, les pixels seront fournis en RGB565 (conversion si nécessaire)
    SDCard_Status read_bmp_file(
        uint16_t x,
        uint16_t y,
        const char* filename,
        void (*pixel_callback_rgb)(uint16_t x, uint16_t y, Color_RGB color) = nullptr,
        void (*pixel_callback_565)(uint16_t x, uint16_t y, uint16_t color) = nullptr);

    SDCard_Status read_24bit_bmp_file(uint16_t x, uint16_t y, const char* filename,
                                      void (*pixel_callback)(uint16_t x, uint16_t y, Color_RGB color));
    SDCard_Status read_16bit_bmp_file(uint16_t x, uint16_t y, const char* filename,
                                      void (*pixel_callback)(uint16_t x, uint16_t y, uint16_t color));

    // Fonctions avancées FAT32
    SDCard_Status list_directory_advanced();
    void display_fat32_system_info();
    void debug_sector_with_fat32(uint32_t sector_num);
    SDCard_Status run_fat32_test();

private:
    // Buffer pour les opérations fichier
    uint8_t read_buffer[SDCardConfig::READ_BUFFER_SIZE];
    uint16_t buffer_index;
    SDCard_Command current_command;
};