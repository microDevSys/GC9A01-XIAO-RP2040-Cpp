#pragma once

#include <cstdint>
#include <cstring>

// Structures FAT32 selon la spécification Microsoft

#pragma pack(push, 1)

// Boot Record (Premier secteur de la partition)
struct FAT32_BootRecord {
    uint8_t  jump[3];           // 0x00: Jump instruction
    char     oem_name[8];       // 0x03: OEM name
    uint16_t bytes_per_sector;  // 0x0B: Bytes per sector (généralement 512)
    uint8_t  sectors_per_cluster; // 0x0D: Sectors per cluster
    uint16_t reserved_sectors;  // 0x0E: Reserved sectors before FAT
    uint8_t  fat_count;         // 0x10: Number of FATs (généralement 2)
    uint16_t root_entries;      // 0x11: Root directory entries (0 pour FAT32)
    uint16_t total_sectors_16;  // 0x13: Total sectors (0 si > 65535)
    uint8_t  media_type;        // 0x15: Media type (0xF8 pour disque dur)
    uint16_t fat_size_16;       // 0x16: Sectors per FAT (0 pour FAT32)
    uint16_t sectors_per_track; // 0x18: Sectors per track
    uint16_t head_count;        // 0x1A: Number of heads
    uint32_t hidden_sectors;    // 0x1C: Hidden sectors
    uint32_t total_sectors_32;  // 0x20: Total sectors (si > 65535)
    
    // FAT32 specific (offset 0x24)
    uint32_t fat_size_32;       // 0x24: Sectors per FAT
    uint16_t ext_flags;         // 0x28: Extended flags
    uint16_t fs_version;        // 0x2A: File system version
    uint32_t root_cluster;      // 0x2C: Root directory cluster
    uint16_t fs_info;           // 0x30: FS Info sector
    uint16_t backup_boot;       // 0x32: Backup boot sector
    uint8_t  reserved[12];      // 0x34: Reserved
    uint8_t  drive_number;      // 0x40: Drive number
    uint8_t  reserved1;         // 0x41: Reserved
    uint8_t  boot_signature;    // 0x42: Extended boot signature
    uint32_t volume_serial;     // 0x43: Volume serial number
    char     volume_label[11];  // 0x47: Volume label
    char     fs_type[8];        // 0x52: File system type "FAT32   "
    uint8_t  boot_code[420];    // 0x5A: Boot code
    uint16_t signature;         // 0x1FE: 0xAA55
};

// Entrée de répertoire (32 bytes)
struct FAT32_DirectoryEntry {
    char     filename[11];      // 0x00: Nom de fichier (8.3 format)
    uint8_t  attributes;        // 0x0B: Attributs
    uint8_t  reserved;          // 0x0C: Réservé
    uint8_t  create_time_fine;  // 0x0D: Création (dixièmes de seconde)
    uint16_t create_time;       // 0x0E: Heure de création
    uint16_t create_date;       // 0x10: Date de création
    uint16_t access_date;       // 0x12: Date de dernier accès
    uint16_t cluster_high;      // 0x14: Cluster high word
    uint16_t modify_time;       // 0x16: Heure de modification
    uint16_t modify_date;       // 0x18: Date de modification
    uint16_t cluster_low;       // 0x1A: Cluster low word
    uint32_t file_size;         // 0x1C: Taille du fichier
};

// Entrée LFN (Long File Name) - 32 bytes
struct FAT32_LFNEntry {
    uint8_t  sequence;          // 0x00: Séquence number
    uint16_t name1[5];          // 0x01: Caractères 1-5 (UTF-16)
    uint8_t  attributes;        // 0x0B: 0x0F pour LFN
    uint8_t  type;              // 0x0C: Type (0 pour LFN)
    uint8_t  checksum;          // 0x0D: Checksum du nom court
    uint16_t name2[6];          // 0x0E: Caractères 6-11 (UTF-16)
    uint16_t cluster;           // 0x1A: Toujours 0 pour LFN
    uint16_t name3[2];          // 0x1C: Caractères 12-13 (UTF-16)
};

#pragma pack(pop)

// Attributs de fichier
namespace FAT32_Attributes {
    constexpr uint8_t READ_ONLY = 0x01;
    constexpr uint8_t HIDDEN    = 0x02;
    constexpr uint8_t SYSTEM    = 0x04;
    constexpr uint8_t VOLUME_ID = 0x08;
    constexpr uint8_t DIRECTORY = 0x10;
    constexpr uint8_t ARCHIVE   = 0x20;
    constexpr uint8_t LFN       = 0x0F; // READ_ONLY | HIDDEN | SYSTEM | VOLUME_ID
}

// Valeurs spéciales de cluster
namespace FAT32_Cluster {
    constexpr uint32_t FREE         = 0x00000000;
    constexpr uint32_t RESERVED_MIN = 0x0FFFFFF0;
    constexpr uint32_t BAD          = 0x0FFFFFF7;
    constexpr uint32_t EOC_MIN      = 0x0FFFFFF8; // End of Chain
    constexpr uint32_t EOC_MAX      = 0x0FFFFFFF;
}

// Configuration par défaut
namespace FAT32_Config {
    constexpr uint32_t SECTOR_SIZE = 512;
    constexpr uint32_t MAX_PATH_LENGTH = 260;
    constexpr uint32_t MAX_FILENAME_LENGTH = 255;
}