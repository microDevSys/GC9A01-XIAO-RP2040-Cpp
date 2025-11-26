#include "StorageManager.h"
#include "FAT32.h"
#include "SDCard.h"
#include <cstdio>
#include <cstring>
#include "lib_bmp.h"
#include "Color.h"

/*******************************************************
 * Nom du fichier : StorageManager.cpp
 * Auteur         : Guillaume Sahuc
 * Date           : 26 novembre 2025
 * Description    : driver haut niveau FAT32
 *******************************************************/

// ============================================================================
// CONSTRUCTEUR & DESTRUCTEUR
// ============================================================================

StorageManager::StorageManager(SDCard* card) 
    : sd_card(card), 
      fat32_fs(nullptr), 
      fat32_mounted(false),
      buffer_index(0), 
      current_command(SD_INACTIVE) {
    memset(read_buffer, 0, sizeof(read_buffer));
}

StorageManager::~StorageManager() {
    if (fat32_fs) {
        delete fat32_fs;
        fat32_fs = nullptr;
    }
}

// ============================================================================
// MONTAGE DU SYSTÈME DE FICHIERS
// ============================================================================

bool StorageManager::mount_fat32() {
    if (!sd_card || !sd_card->is_initialized()) {
        printf("SDCard non initialisée\n");
        return false;
    }

    fat32_fs = new FAT32(sd_card);
    if (!fat32_fs) {
        printf("Erreur allocation mémoire FAT32\n");
        fat32_mounted = false;
        return false;
    }

    if (fat32_fs->init()) {
        printf("Système FAT32 initialisé avec succès\n");
        fat32_mounted = true;
        return true;
    }
    
    delete fat32_fs;
    fat32_fs = nullptr;
    fat32_mounted = false;
    return false;
}

// ============================================================================
// RENOMMER UN FICHIER
// ============================================================================

bool StorageManager::rename_file(const char* old_name, const char* new_name) {
    if (!fat32_mounted || !fat32_fs) {
        printf("[ERREUR] FAT32 non monté\n");
        return false;
    }
    
    return fat32_fs->rename_file(old_name, new_name);
}

// ============================================================================
// OPÉRATIONS SUR FICHIERS TEXTE
// ============================================================================

SDCard_Status StorageManager::read_text_file(const char* filename) {
    if (!is_fat32_mounted()) {
        printf("FAT32 non disponible\n");
        return SD_FILE_NOT_FOUND;
    }
    
    current_command = SD_FILE_READING;
    printf("=== Lecture fichier avec FAT32 : %s ===\n", filename);
    
    FAT_ErrorCode fat_result = fat32_fs->file_open(filename, READ);
    if (fat_result != FILE_FOUND) {
        printf("Fichier non trouvé: %s (Erreur FAT: %d)\n", filename, fat_result);
        current_command = SD_INACTIVE;
        return SD_FILE_NOT_FOUND;
    }
    
    printf("Fichier trouvé - Taille: %lu bytes\n", fat32_fs->get_file_size(filename));
    printf("The content of file is:\n");
    
    ReadHandler handler;
    uint16_t bytes_read = fat32_fs->file_read(read_buffer, &handler);
    
    while (bytes_read > 0) {
        read_buffer[bytes_read] = '\0';
        printf("%s", read_buffer);
        bytes_read = fat32_fs->file_read(read_buffer, &handler);
    }
    
    printf("\n<EOF>END OF FILE\n");
    fat32_fs->file_close();
    
    current_command = SD_INACTIVE;
    buffer_index = 0;
    return SD_OK;
}

SDCard_Status StorageManager::write_text_file(const char* filename, const uint8_t* buffer, uint16_t length) {
    if (!is_fat32_mounted()) {
        printf("FAT32 non disponible pour écriture\n");
        return SD_FILE_NOT_FOUND;
    }
    
    printf("=== Écriture fichier avec FAT32 : %s (%d bytes) ===\n", filename, length);
    current_command = SD_FILE_WRITING;
    
    FAT_ErrorCode fat_result = fat32_fs->file_open(filename, CREATE);
    if (fat_result != FILE_CREATE_OK && fat_result != FILE_FOUND) {
        printf("Erreur création/ouverture fichier: %s (Erreur FAT: %d)\n", filename, fat_result);
        current_command = SD_INACTIVE;
        return SD_FILE_NOT_FOUND;
    }
    
    fat32_fs->file_write(buffer, length);
    fat32_fs->file_close();
    
    printf("Fichier écrit avec succès: %s\n", filename);
    current_command = SD_INACTIVE;
    return SD_OK;
}

// ============================================================================
// OPÉRATIONS SUR RÉPERTOIRES ET INFORMATIONS FICHIERS
// ============================================================================

bool StorageManager::file_exists(const char* filename) {
    if (!is_fat32_mounted() || !filename) {
        return false;
    }
    
    // Extraire le chemin et le nom de base du fichier
    std::string path(filename);
    std::string dir = "/";
    std::string base = path;
    
    auto pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        dir = path.substr(0, pos);
        if (dir.empty()) dir = "/";
        base = path.substr(pos + 1);
    }
    
    // Sauvegarder et changer de répertoire
    if (!fat32_fs->change_directory(dir.c_str())) {
        fat32_fs->change_directory("/");
        return false;
    }
    
    bool exists = fat32_fs->file_exists(base.c_str());
    fat32_fs->change_directory("/");
    
    return exists;
}

uint32_t StorageManager::get_file_size(const char* filename) {
    if (!is_fat32_mounted() || !filename) {
        return 0;
    }
    
    // Extraire le chemin et le nom de base du fichier
    std::string path(filename);
    std::string dir = "/";
    std::string base = path;
    
    auto pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        dir = path.substr(0, pos);
        if (dir.empty()) dir = "/";
        base = path.substr(pos + 1);
    }
    
    uint32_t size = 0;
    if (fat32_fs->change_directory(dir.c_str())) {
        size = fat32_fs->get_file_size(base.c_str());
    }
    
    fat32_fs->change_directory("/");
    return size;
}

std::vector<FileInfo> StorageManager::list_directory(const char* path) {
    std::vector<FileInfo> result;
    
    if (!is_fat32_mounted()) {
        printf("FAT32 non disponible pour listing\n");
        return result;
    }
    
    // Changer vers le répertoire demandé si nécessaire
    bool need_restore = false;
    if (path && std::strlen(path) > 0 && std::strcmp(path, "/") != 0) {
        if (!fat32_fs->change_directory(path)) {
            printf("Impossible d'accéder au répertoire: %s\n", path);
            return result;
        }
        need_restore = true;
    }

    // Obtenir la liste des fichiers
    std::vector<FileListEntry> fat_files;
    FAT_ErrorCode error = fat32_fs->list_directory(fat_files);
    
    if (error != ERROR_IDLE) {
        printf("Erreur listing FAT32 : %d\n", error);
        if (need_restore) {
            fat32_fs->change_directory("/");
        }
        return result;
    }
    
    // Convertir les entrées FAT32 vers notre structure FileInfo
    for (const auto& fat_file : fat_files) {
        FileInfo info;
        // Utiliser le nom long si disponible, sinon le nom DOS
        if (fat_file.hasLongName && !fat_file.longFileName.empty()) {
            info.name = fat_file.longFileName;
        } else {
            info.name = fat_file.dosFileName;
        }
        info.size = fat_file.size;
        info.is_directory = (fat_file.type == _Directory);
        // Propagate metadata
        info.attributes = fat_file.attributes;
        info.modificationTime = fat_file.modificationTime;
        info.modificationDate = fat_file.modificationDate;
        info.firstCluster = fat_file.firstCluster;
        result.push_back(info);
    }
    
    printf("Listing FAT32 : %zu fichiers trouvés\n", result.size());
    
    // Restaurer le répertoire racine
    if (need_restore) {
        fat32_fs->change_directory("/");
    }
    
    return result;
}

// ============================================================================
// HELPERS INTERNES POUR LECTURE BMP
// ============================================================================

namespace {
    // Structure pour gérer le buffer de staging lors de la lecture
    struct StagingBuffer {
        uint8_t data[SDCardConfig::READ_BUFFER_SIZE];
        uint16_t length;
        uint16_t position;
        FAT32* fs;
        ReadHandler* handler;
        
        StagingBuffer(FAT32* filesystem, ReadHandler* h) 
            : length(0), position(0), fs(filesystem), handler(h) {}
        
        bool refill() {
            if (position < length) return true;
            length = fs->file_read(data, handler);
            position = 0;
            return length > 0;
        }
        
        bool readBytes(uint8_t* dst, uint32_t count) {
            while (count > 0) {
                if (position >= length && !refill()) {
                    return false;
                }
                uint32_t available = (uint32_t)(length - position);
                uint32_t to_copy = (available < count) ? available : count;
                memcpy(dst, data + position, to_copy);
                position += (uint16_t)to_copy;
                dst += to_copy;
                count -= to_copy;
            }
            return true;
        }
        
        bool skip(uint32_t count) {
            while (count > 0) {
                if (position >= length && !refill()) {
                    return false;
                }
                uint32_t available = (uint32_t)(length - position);
                uint32_t to_skip = (available < count) ? available : count;
                position += (uint16_t)to_skip;
                count -= to_skip;
            }
            return true;
        }
    };
    
    // Conversions couleurs
    static inline uint16_t rgb888_to_565(const Color_RGB& c) {
        uint16_t r = (uint16_t)(c.red   >> 3);   // 5 bits
        uint16_t g = (uint16_t)(c.green >> 2);   // 6 bits
        uint16_t b = (uint16_t)(c.blue  >> 3);   // 5 bits
        return (uint16_t)((r << 11) | (g << 5) | b);
    }

    static inline Color_RGB rgb565_to_888(uint16_t v) {
        Color_RGB c;
        uint8_t r5 = (uint8_t)((v >> 11) & 0x1F);
        uint8_t g6 = (uint8_t)((v >> 5)  & 0x3F);
        uint8_t b5 = (uint8_t)(v & 0x1F);
        // Étendre 5/6 bits vers 8 bits (approximation habituelle)
        c.red   = (uint8_t)((r5 << 3) | (r5 >> 2));
        c.green = (uint8_t)((g6 << 2) | (g6 >> 4));
        c.blue  = (uint8_t)((b5 << 3) | (b5 >> 2));
        return c;
    }

    // Valider l'en-tête BMP
    bool validate_bmp_header(const t_bmp_header& header, uint16_t expected_bits) {
        if (header.first_header.sType != 0x4D42) {
            printf("Signature BMP invalide: 0x%04X\n", header.first_header.sType);
            return false;
        }
        if (header.second_header.sBitCount != expected_bits) {
            printf("Format BMP non supporté: %u bits (attendu: %u)\n", 
                   header.second_header.sBitCount, expected_bits);
            return false;
        }
        
        // BI_RGB (0) : pas de compression
        // BI_BITFIELDS (3) : masques de bits personnalisés (commun pour 16-bit)
        uint32_t compression = header.second_header.iCompression;
        if (compression != 0 && compression != 3) {
            printf("Compression BMP non supportée: %lu (supporté: 0=BI_RGB, 3=BI_BITFIELDS)\n", 
                   (unsigned long)compression);
            return false;
        }
        
        // Pour 16-bit avec BI_BITFIELDS, on suppose RGB565 (le plus courant)
        if (expected_bits == 16 && compression == 3) {
            printf("Format détecté: BMP 16-bit avec BI_BITFIELDS (RGB565 supposé)\n");
        }
        
        return true;
    }
    
    // Extraire les informations dimensionnelles du BMP
    struct BmpDimensions {
        uint32_t width;
        uint32_t height;
        bool top_down;
        uint32_t bytes_per_row;
        uint32_t padding;
    };
    
    BmpDimensions extract_dimensions(const t_bmp_header& header, uint16_t bytes_per_pixel) {
        BmpDimensions dim;
        int32_t width = header.second_header.iWidth;
        int32_t height = header.second_header.iHeight;
        
        dim.top_down = (height < 0);
        dim.width = (uint32_t)(width < 0 ? -width : width);
        dim.height = (uint32_t)(height < 0 ? -height : height);
        dim.bytes_per_row = dim.width * bytes_per_pixel;
        dim.padding = (4 - (dim.bytes_per_row % 4)) % 4;
        
        return dim;
    }
}

// ============================================================================
// OPÉRATIONS SUR FICHIERS BMP
// ============================================================================

SDCard_Status StorageManager::read_bmp_file(
    uint16_t x,
    uint16_t y,
    const char* filename,
    void (*pixel_callback_rgb)(uint16_t, uint16_t, Color_RGB),
    void (*pixel_callback_565)(uint16_t, uint16_t, uint16_t))
{
    if (!is_fat32_mounted() || (!pixel_callback_rgb && !pixel_callback_565)) {
        printf("FAT32 non disponible ou aucun callback fourni\n");
        return SD_FILE_NOT_FOUND;
    }

    current_command = SD_IMAGE_READING;
    printf("=== Lecture BMP adaptative avec FAT32 : %s ===\n", filename);

    if (!fat32_fs->file_exists(filename)) {
        printf("Fichier BMP non trouvé: %s\n", filename);
        current_command = SD_INACTIVE;
        return SD_FILE_NOT_FOUND;
    }

    FAT_ErrorCode fat_result = fat32_fs->file_open(filename, READ);
    if (fat_result != FILE_FOUND) {
        printf("Erreur ouverture fichier BMP: %s (code %d)\n", filename, fat_result);
        current_command = SD_INACTIVE;
        return SD_FILE_NOT_FOUND;
    }

    ReadHandler handler;
    StagingBuffer staging(fat32_fs, &handler);

    // Lire header
    t_bmp_header header;
    if (!staging.readBytes(reinterpret_cast<uint8_t*>(&header), sizeof(header))) {
        printf("Lecture du header BMP incomplète\n");
        fat32_fs->file_close();
        current_command = SD_INACTIVE;
        return SD_BAD_FILE_FORMAT;
    }

    const uint16_t bpp = header.second_header.sBitCount;
    if (bpp != 16 && bpp != 24) {
        printf("Profondeur de couleur non supportée: %u bpp (seuls 16 et 24 sont supportés)\n", bpp);
        fat32_fs->file_close();
        current_command = SD_INACTIVE;
        return SD_UNSUPPORTED_COMPRESSION;
    }

    // Valider signature/compression et préparer dimensions
    if (bpp == 24) {
        if (!validate_bmp_header(header, 24)) {
            fat32_fs->file_close();
            current_command = SD_INACTIVE;
            return SD_BAD_FILE_FORMAT;
        }

        BmpDimensions dim = extract_dimensions(header, 3);
        printf("BMP Info: %lux%lu, 24 bpp, offset=%lu, padding=%lu (%s)\n",
               (unsigned long)dim.width, (unsigned long)dim.height,
               (unsigned long)header.first_header.iOffBits,
               (unsigned long)dim.padding,
               dim.top_down ? "top-down" : "bottom-up");

        // Avancer au début des pixels
        if (header.first_header.iOffBits > sizeof(t_bmp_header)) {
            uint32_t skip = header.first_header.iOffBits - (uint32_t)sizeof(t_bmp_header);
            if (!staging.skip(skip)) {
                printf("Impossible d'atteindre le début des pixels\n");
                fat32_fs->file_close();
                current_command = SD_INACTIVE;
                return SD_INCOMPLETE_BUFFER_READ;
            }
        }

        uint8_t rowbuf[1024];
        if (dim.bytes_per_row > sizeof(rowbuf)) {
            printf("Ligne trop large (%lu bytes), non supportée\n", (unsigned long)dim.bytes_per_row);
            fat32_fs->file_close();
            current_command = SD_INACTIVE;
            return SD_UNKNOWN_ERROR;
        }

        for (uint32_t row = 0; row < dim.height; ++row) {
            if (!staging.readBytes(rowbuf, dim.bytes_per_row)) {
                printf("Lecture incomplète de la ligne %lu\n", (unsigned long)row);
                fat32_fs->file_close();
                current_command = SD_INACTIVE;
                return SD_INCOMPLETE_BUFFER_READ;
            }

            uint32_t screen_y = dim.top_down ? (y + row) : (y + (dim.height - 1 - row));
            for (uint32_t col = 0; col < dim.width; ++col) {
                uint32_t idx = col * 3u;
                Color_RGB c;
                c.blue  = rowbuf[idx + 0];
                c.green = rowbuf[idx + 1];
                c.red   = rowbuf[idx + 2];

                if (pixel_callback_rgb) {
                    pixel_callback_rgb(x + (uint16_t)col, (uint16_t)screen_y, c);
                } else if (pixel_callback_565) {
                    pixel_callback_565(x + (uint16_t)col, (uint16_t)screen_y, rgb888_to_565(c));
                }
            }

            if (dim.padding && !staging.skip(dim.padding)) {
                printf("Lecture padding incomplète\n");
                fat32_fs->file_close();
                current_command = SD_INACTIVE;
                return SD_INCOMPLETE_BUFFER_READ;
            }
        }

        fat32_fs->file_close();
        current_command = SD_INACTIVE;
        buffer_index = 0;
        return SD_OK;
    } else { // 16 bpp
        if (!validate_bmp_header(header, 16)) {
            fat32_fs->file_close();
            current_command = SD_INACTIVE;
            return SD_BAD_FILE_FORMAT;
        }

        BmpDimensions dim = extract_dimensions(header, 2);
        printf("Lecture BMP 16-bit: %lux%lu à position (%d,%d)\n",
               (unsigned long)dim.width, (unsigned long)dim.height, x, y);
        printf("Offset pixels: %lu, Header size: %lu\n",
               (unsigned long)header.first_header.iOffBits, (unsigned long)sizeof(t_bmp_header));

        // Gestion BI_BITFIELDS: 3 masques de 4 octets après le header
        uint32_t extra_bytes = 0;
        if (header.second_header.iCompression == 3) {
            uint32_t masks[3];
            if (!staging.readBytes(reinterpret_cast<uint8_t*>(masks), 12)) {
                printf("Impossible de lire les masques de couleur BI_BITFIELDS\n");
                fat32_fs->file_close();
                current_command = SD_INACTIVE;
                return SD_INCOMPLETE_BUFFER_READ;
            }
            printf("Masques couleur: R=0x%08lX, G=0x%08lX, B=0x%08lX\n",
                   (unsigned long)masks[0], (unsigned long)masks[1], (unsigned long)masks[2]);
            extra_bytes = 12;
        }

        uint32_t bytes_read_so_far = sizeof(t_bmp_header) + extra_bytes;
        if (header.first_header.iOffBits > bytes_read_so_far) {
            uint32_t skip = header.first_header.iOffBits - bytes_read_so_far;
            printf("Skip vers pixels: %lu octets\n", (unsigned long)skip);
            if (!staging.skip(skip)) {
                printf("Impossible d'atteindre le début des pixels (16-bit)\n");
                fat32_fs->file_close();
                current_command = SD_INACTIVE;
                return SD_INCOMPLETE_BUFFER_READ;
            }
        }

        uint8_t rowbuf[1024];
        if (dim.bytes_per_row > sizeof(rowbuf)) {
            printf("Ligne trop large (%lu bytes), non supportée\n", (unsigned long)dim.bytes_per_row);
            fat32_fs->file_close();
            current_command = SD_INACTIVE;
            return SD_UNKNOWN_ERROR;
        }

        for (uint32_t row = 0; row < dim.height; ++row) {
            if (!staging.readBytes(rowbuf, dim.bytes_per_row)) {
                printf("Lecture incomplète de la ligne %lu\n", (unsigned long)row);
                fat32_fs->file_close();
                current_command = SD_INACTIVE;
                return SD_INCOMPLETE_BUFFER_READ;
            }

            uint32_t screen_y = dim.top_down ? (y + row) : (y + (dim.height - 1 - row));
            for (uint32_t col = 0; col < dim.width; ++col) {
                uint32_t idx = col * 2u;
                uint16_t pix = (uint16_t)rowbuf[idx] | ((uint16_t)rowbuf[idx + 1] << 8);

                if (pixel_callback_565) {
                    pixel_callback_565(x + (uint16_t)col, (uint16_t)screen_y, pix);
                } else if (pixel_callback_rgb) {
                    pixel_callback_rgb(x + (uint16_t)col, (uint16_t)screen_y, rgb565_to_888(pix));
                }
            }

            if (dim.padding && !staging.skip(dim.padding)) {
                printf("Lecture padding incomplète\n");
                fat32_fs->file_close();
                current_command = SD_INACTIVE;
                return SD_INCOMPLETE_BUFFER_READ;
            }
        }

        fat32_fs->file_close();
        current_command = SD_INACTIVE;
        buffer_index = 0;
        return SD_OK;
    }
}

SDCard_Status StorageManager::read_24bit_bmp_file(uint16_t x, uint16_t y, const char* filename,
                                                  void (*pixel_callback)(uint16_t x, uint16_t y, Color_RGB color)) {
    // Wrapper rétro-compatible vers la fonction unifiée
    return read_bmp_file(x, y, filename, pixel_callback, nullptr);
}

SDCard_Status StorageManager::read_16bit_bmp_file(uint16_t x, uint16_t y, const char* filename,
                                                  void (*pixel_callback)(uint16_t x, uint16_t y, uint16_t color)) {
    // Wrapper rétro-compatible vers la fonction unifiée
    return read_bmp_file(x, y, filename, nullptr, pixel_callback);
}

// ============================================================================
// FONCTIONS AVANCÉES DE DIAGNOSTIC ET TEST
// ============================================================================

SDCard_Status StorageManager::list_directory_advanced() {
    if (!is_fat32_mounted()) {
        printf("FAT32 non disponible\n");
        return SD_FILE_NOT_FOUND;
    }
    
    printf("=== LISTING AVANCÉ FAT32 (avec LFN) ===\n");
    
    std::vector<FileListEntry> files;
    FAT_ErrorCode result_advanced = fat32_fs->list_directory(files);
    
    if (result_advanced != ERROR_IDLE) {
        printf("Erreur listing avancé: %d\n", result_advanced);
        return SD_FILE_NOT_FOUND;
    }
    
    printf("\n=== ANALYSE DÉTAILLÉE ===\n");
    
    int file_count = 0;
    int dir_count = 0;
    uint32_t total_size = 0;
    
    for (size_t i = 0; i < files.size(); i++) {
        const FileListEntry& entry = files[i];
        
        printf("\n--- Entrée %zu ---\n", i + 1);
        
        if (entry.hasLongName && !entry.longFileName.empty()) {
            printf("Nom long: %s\n", entry.longFileName.c_str());
            printf("Nom DOS:  %s\n", entry.dosFileName);
        } else {
            printf("Nom DOS:  %s\n", entry.dosFileName);
        }
        
        // Attributes display helper
        char attr_buf[8] = "-------"; // R H S V D A -
        if (entry.attributes & FAT_Config::AT_READONLY) attr_buf[0] = 'R';
        if (entry.attributes & FAT_Config::AT_HIDDEN)   attr_buf[1] = 'H';
        if (entry.attributes & FAT_Config::AT_SYSTEM)   attr_buf[2] = 'S';
        if (entry.attributes & FAT_Config::AT_VOLUME_ID)attr_buf[3] = 'V';
        if (entry.attributes & FAT_Config::AT_DIRECTORY) attr_buf[4] = 'D';
        if (entry.attributes & FAT_Config::AT_ARCHIVE)   attr_buf[5] = 'A';

        // Format FAT date/time
        auto format_fat_datetime = [](uint16_t date, uint16_t time) -> std::string {
            if (date == 0 && time == 0) return std::string("----/--/-- --:--:--");
            uint16_t day = date & 0x1F;
            uint16_t month = (date >> 5) & 0x0F;
            uint16_t year = ((date >> 9) & 0x7F) + 1980;
            uint16_t seconds = (time & 0x1F) * 2;
            uint16_t minutes = (time >> 5) & 0x3F;
            uint16_t hours = (time >> 11) & 0x1F;
            char buf[64];
            snprintf(buf, sizeof(buf), "%04u/%02u/%02u %02u:%02u:%02u", (unsigned)year, (unsigned)month, (unsigned)day, (unsigned)hours, (unsigned)minutes, (unsigned)seconds);
            return std::string(buf);
        };

        switch (entry.type) {
            case _File:
                  printf("Type:     Fichier\n");
                  printf("Taille:   %lu bytes (%.2f KB)\n", entry.size, (float)entry.size / 1024.0f);
                  printf("Attributs: %s  Cluster: %lu\n", attr_buf, (unsigned long)entry.firstCluster);
                  printf("Modifié:  %s\n", format_fat_datetime(entry.modificationDate, entry.modificationTime).c_str());
                file_count++;
                total_size += entry.size;
                break;
                
            case _Directory:
                printf("Type:     Répertoire\n");
                printf("Attributs: %s  Cluster: %lu\n", attr_buf, (unsigned long)entry.firstCluster);
                printf("Modifié:  %s\n", format_fat_datetime(entry.modificationDate, entry.modificationTime).c_str());
                dir_count++;
                break;
                
            default:
                printf("Type:     Inconnu (%d)\n", entry.type);
                break;
        }
    }
    
    printf("\n=== RÉSUMÉ FINAL ===\n");
    printf("Fichiers trouvés:    %d\n", file_count);
    printf("Répertoires trouvés: %d\n", dir_count);
    printf("Taille totale:       %lu bytes (%.2f KB, %.2f MB)\n", 
           total_size, 
           (float)total_size / 1024.0f,
           (float)total_size / (1024.0f * 1024.0f));
    
    return SD_OK;
}

void StorageManager::display_fat32_system_info() {
    if (!is_fat32_mounted()) {
        printf("FAT32 non disponible\n");
        return;
    }
    
    printf("=== INFORMATIONS SYSTÈME FAT32 ===\n");
    fat32_fs->view_fat_infos();
    
    printf("\n=== INFORMATIONS ESPACE DISQUE ===\n");
    uint32_t total_space = fat32_fs->get_total_space();
    uint32_t free_space = fat32_fs->get_free_space();
    float free_percent = fat32_fs->get_free_space_percent();
    
    printf("Espace total: %lu bytes (%.2f MB)\n", 
           total_space, (float)total_space / (1024.0f * 1024.0f));
    printf("Espace libre: %lu bytes (%.2f MB)\n", 
           free_space, (float)free_space / (1024.0f * 1024.0f));
    printf("Pourcentage libre: %.1f%%\n", free_percent);
    
    printf("\n=== PARAMÈTRES TECHNIQUES ===\n");
    printf("Taille secteur:  %d bytes\n", fat32_fs->get_sector_size());
    printf("Taille cluster:  %d secteurs\n", fat32_fs->get_cluster_size());
    printf("Base FAT:        secteur %lu\n", fat32_fs->get_fat_base());
    printf("Base Root:       secteur %lu\n", fat32_fs->get_root_base());
    printf("Base Data:       secteur %lu\n", fat32_fs->get_data_base());
    printf("Support LFN:     %s\n", fat32_fs->supports_lfn() ? "OUI" : "NON");
}

void StorageManager::debug_sector_with_fat32(uint32_t sector_num) {
    printf("=== DEBUG SECTEUR %lu avec FAT32 ===\n", sector_num);
    
    if (!is_fat32_mounted()) {
        printf("FAT32 non disponible - Debug raw\n");
        uint8_t buffer[512];
        if (sd_card->read_block(sector_num, buffer)) {
            SD_print_buffer_hex(buffer, 512, 16);
        } else {
            printf("Erreur lecture secteur %lu\n", sector_num);
        }
        return;
    }
    
    fat32_fs->print_sector_hex(sector_num);
    
    // Analyse du type de secteur
    if (sector_num == 0) {
        printf("\nAnalyse: Secteur 0 - Master Boot Record (MBR)\n");
    } else if (fat32_fs->get_fat_base() > 0 && 
               sector_num >= fat32_fs->get_fat_base() && 
               sector_num < fat32_fs->get_root_base()) {
        printf("\nAnalyse: Secteur de table FAT\n");
    } else if (fat32_fs->get_root_base() > 0 && 
               sector_num >= fat32_fs->get_root_base() && 
               sector_num < fat32_fs->get_data_base()) {
        printf("\nAnalyse: Secteur de répertoire racine\n");
    } else if (fat32_fs->get_data_base() > 0 && 
               sector_num >= fat32_fs->get_data_base()) {
        printf("\nAnalyse: Secteur de données\n");
    }
}

SDCard_Status StorageManager::run_fat32_test() {
    printf("=== TEST COMPLET FAT32 ===\n");
    
    if (!is_fat32_mounted()) {
        printf("ÉCHEC: FAT32 non disponible\n");
        return SD_FILE_NOT_FOUND;
    }
    
    SDCard_Status overall_status = SD_OK;
    
    // Test 1: Informations système
    printf("\n1. Test informations système...\n");
    display_fat32_system_info();
    
    // Test 2: Listing avancé
    printf("\n2. Test listing avancé...\n");
    SDCard_Status list_status = list_directory_advanced();
    if (list_status != SD_OK) {
        printf("ATTENTION: Listing échoué\n");
        overall_status = list_status;
    }
    
    // Test 3: Création et écriture de fichier
    printf("\n3. Test création et écriture fichier...\n");
    const char* test_content = "=== TEST FAT32 StorageManager ===\n"
                               "Fichier créé par StorageManager\n"
                               "Date: 2025-11-02\n"
                               "\n"
                               "Contenu de test:\n"
                               "- Ligne 1: Test d'écriture\n"
                               "- Ligne 2: Système FAT32 opérationnel\n"
                               "- Ligne 3: Support LFN activé\n"
                               "- Ligne 4: Pico SDK + RPiPico\n"
                               "\n"
                               "Fin du fichier de test.\n";
    
    printf("Écriture de %d octets dans TEST_FAT.TXT...\n", (int)strlen(test_content));
    SDCard_Status write_status = write_text_file("TEST_FAT.TXT", 
                                                 (const uint8_t*)test_content, 
                                                 strlen(test_content));
    if (write_status != SD_OK) {
        printf("ATTENTION: Création/écriture fichier échouée\n");
        overall_status = write_status;
    } else {
        printf("✓ Fichier créé et écrit avec succès\n");
    }
    
    // Test 4: Lecture de fichier
    printf("\n4. Test lecture fichier...\n");
    SDCard_Status read_status = read_text_file("TEST_FAT.TXT");
    if (read_status != SD_OK) {
        printf("ATTENTION: Lecture fichier échouée\n");
        overall_status = read_status;
    }
    
    // Test 5: Debug secteurs
    printf("\n5. Test debug secteurs...\n");
    printf("Debug MBR (secteur 0):\n");
    debug_sector_with_fat32(0);
    
    if (fat32_fs->get_fat_base() > 0) {
        printf("\nDebug premier secteur FAT:\n");
        debug_sector_with_fat32(fat32_fs->get_fat_base());
    }
    
    // Résumé final
    printf("\n=== RÉSULTAT TEST COMPLET ===\n");
    if (overall_status == SD_OK) {
        printf("✅ TOUS LES TESTS RÉUSSIS\n");
        printf("FAT32 fonctionne parfaitement\n");
    } else {
        printf("⚠️ CERTAINS TESTS ONT ÉCHOUÉ\n");
        printf("Statut final: %d\n", overall_status);
    }
    
    return overall_status;
}