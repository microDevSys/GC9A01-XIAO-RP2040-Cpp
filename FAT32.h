#pragma once

/*
 * FAT32  - Inspiré du fichier fat.c de Guillaume Sahuc
 * 
 * Améliorations apportées :
 * - Support des noms longs (LFN - Long File Names)
 * - Gestion avancée des clusters et secteurs
 * - Fonctions de listing et navigation
 * - Handlers de lecture/écriture optimisés
 * - Debug et informations système complètes
 */

#include "pico/stdlib.h"
#include <string>
#include <vector>
#include <cstring>

// Forward declaration
class SDCard;

// Constantes inspirées du fichier fat.c
namespace FAT_Config {
    static constexpr uint16_t SECTOR_SIZE = 512;
    static constexpr uint16_t MAX_LFN_CHARACTERS = 255;
    static constexpr uint16_t ROOT_ENTRY_SIZE = 32;
    static constexpr uint16_t ENTRIES_PER_BLOCK = SECTOR_SIZE / ROOT_ENTRY_SIZE; // 16
    
    // Codes de fichiers (inspirés du fat.c)
    static constexpr uint8_t FILE_CLEAR = 0x00;
    static constexpr uint8_t FILE_ERASED = 0xE5;
    
    // Attributs de fichiers
    static constexpr uint8_t AT_READONLY = 0x01;
    static constexpr uint8_t AT_HIDDEN = 0x02;
    static constexpr uint8_t AT_SYSTEM = 0x04;
    static constexpr uint8_t AT_VOLUME_ID = 0x08;
    static constexpr uint8_t AT_DIRECTORY = 0x10;
    static constexpr uint8_t AT_ARCHIVE = 0x20;
    static constexpr uint8_t AT_LFN = 0x0F;
    
    // Clusters spéciaux
    static constexpr uint32_t CLUSTER_FREE = 0x00000000;
    static constexpr uint16_t CLUSTER_EOF_16 = 0xFFFF;
    static constexpr uint32_t CLUSTER_EOF_32 = 0x0FFFFFFF;
}

// Types de fichiers (inspirés du fat.c)
enum FileEntryType {
    _File = 0,
    _Directory = 1,
    _LongFileNameOK = 2,
    _LongFileNameNOK = 3,
    _Error = 4
};

// Fonctions de fichier (inspirées du fat.c)
enum FileFunction {
    READ = 0,
    CREATE = 1,
    MODIFY = 2,
    DELETE = 3,
    OVERWRITE = 4
};

// Codes d'erreur (inspirés du fat.c)
enum FAT_ErrorCode {
    ERROR_IDLE = 0,
    FILE_FOUND = 1,
    FILE_NOT_FOUND = 2,
    FILE_CREATE_OK = 3,
    NO_FILE_ENTRY_AVAILABLE = 4,
    NO_FAT_ENTRY_AVAILABLE = 5,
    NO_MORE_FREE_CLUSTER = 6,
    ERROR_READ_FAIL = 7
};

// Structure Boot Record inspirée du fat.c
struct MasterBoot_Entries {
    uint8_t  OEMName[8];
    uint16_t BytesPerSector;
    uint8_t  SectorsPerCluster;
    uint16_t ReservedSectors;
    uint8_t  FatCopies;
    uint16_t RootDirectoryEntries;
    uint16_t SectorsLess32MB;
    uint8_t  MediaDescriptor;
    uint16_t SectorsPerFat;
    uint16_t SectorsPerTrack;
    uint16_t NumberOfHeads;
    uint32_t HiddenSectors;
    uint32_t SectorsInPartition;
    
    // Pour FAT32
    union {
        struct {
            uint32_t SectorsPerFat32;
            uint16_t ExtFlags;
            uint16_t FSVersion;
            uint32_t RootCluster;
            uint16_t FSInfo;
            uint16_t BackupBootSector;
            uint8_t  Reserved[12];
        } fat32;
    } block;
} __attribute__((packed));

// Structure d'entrée de répertoire (inspirée du fat.c)
struct root_Entries {
    uint8_t  FileName[8];
    uint8_t  Extension[3];
    uint8_t  Attributes;
    uint8_t  _Reserved;
    uint8_t  MiliSeconds;
    uint16_t CreationTime;
    uint16_t CreationDate;
    uint16_t AccessDate;
    uint16_t FirstClusterHigh;     // FAT32 high word
    uint16_t ModificationTime;
    uint16_t ModificationDate;
    uint16_t FirstClusterNumber;   // FAT32 low word
    uint32_t SizeofFile;
} __attribute__((packed));

// Structure Long File Name (inspirée du fat.c)
struct DIR_ENT_LFN {
    uint8_t  Ordinal;
    uint16_t Name1[5];
    uint8_t  Attributes;
    uint8_t  Type;
    uint8_t  Checksum;
    uint16_t Name2[6];
    uint16_t FirstCluster;
    uint16_t Name3[2];
} __attribute__((packed));

// Handler de lecture (inspiré du fat.c)
struct ReadHandler {
    uint32_t Dir_Entry;
    uint32_t File_Size;
    uint32_t FAT_Entry;
    uint16_t SectorOffset;
    
    ReadHandler() : Dir_Entry(0), File_Size(0), FAT_Entry(0), SectorOffset(0) {}
};

// Handler d'écriture (inspiré du fat.c)
struct WriteHandler {
    uint8_t  FileName[11];
    uint8_t  Extension[3];
    uint32_t Dir_Entry;
    uint32_t File_Size;
    uint32_t BaseFatEntry;
    uint32_t CurrentFatEntry;
    uint16_t ClusterIndex;
    uint16_t SectorIndex;
    
    WriteHandler() : Dir_Entry(0), File_Size(0), BaseFatEntry(0), 
                    CurrentFatEntry(0), ClusterIndex(0), SectorIndex(0) {
        memset(FileName, 0, sizeof(FileName));
        memset(Extension, 0, sizeof(Extension));
    }
};

// Structure pour liste de fichiers (inspirée du fat.c)
struct FileListEntry {
    std::string longFileName;
    // Max DOS 8.3 name length is 8 + 1 (dot) + 3 = 12 characters.
    // Allocate 13 to include the null terminator and avoid truncating the last character (e.g., missing the last extension letter).
    char dosFileName[13];
    FileEntryType type;
    uint32_t size;
    bool hasLongName;
    // Additional metadata
    uint8_t attributes;
    uint16_t creationTime;
    uint16_t creationDate;
    uint16_t modificationTime;
    uint16_t modificationDate;
    uint32_t firstCluster;
    
    FileListEntry() : type(_Error), size(0), hasLongName(false), longFileName("") {
        memset(dosFileName, 0, sizeof(dosFileName));
        attributes = 0;
        creationTime = creationDate = modificationTime = modificationDate = 0;
        firstCluster = 0;
    }
};

class FAT32 {
private:
    // Buffer for Long File Name (LFN) assembly, accessible by list_directory and fat_filename_parser
    std::string lfn_buffer;
    std::string fn_buffer;
    SDCard* sd_card;
    bool initialized;
    
    // Variables globales inspirées du fat.c
    uint32_t sectors_per_fat;
    uint32_t fat_size;
    uint32_t sectors_in_partition;
    uint16_t sector_size;
    uint16_t cluster_size;
    uint16_t fat_base;
    uint16_t root_base;
    uint16_t data_base;
    uint16_t main_offset;
    uint16_t last_cluster;
    uint32_t root_dir_first_cluster; // FAT32: first cluster of root dir
    uint32_t first_data_sector;      // Relative to volume start (main_offset)
    
    // Buffers (inspirés du fat.c)
    uint8_t read_buffer[FAT_Config::SECTOR_SIZE];
    uint8_t write_buffer[FAT_Config::SECTOR_SIZE];

    // Simple FAT sector cache to reduce SD reads during cluster chain traversal
    uint32_t fat_cache_sector;              // LBA of cached FAT sector or 0xFFFFFFFF if invalid
    bool     fat_cache_valid;               // Cache validity flag
    uint8_t  fat_cache[FAT_Config::SECTOR_SIZE];
    
    // Handlers (inspirés du fat.c)
    ReadHandler read_handler;
    WriteHandler write_handler;
    
    // Boot record
    MasterBoot_Entries master_boot;
    
    // Pointeur de répertoire courant (cluster). Par défaut: racine.
    uint32_t current_dir_cluster_ = 0; // 0 = non initialisé, root utilisé après init
    
    // Méthodes privées inspirées du fat.c
    uint32_t lword_swap(uint32_t data);
    uint16_t byte_swap(uint16_t data);
    bool get_physical_block(uint32_t block_num, uint8_t* buffer);
    bool store_physical_block(uint32_t block_num, const uint8_t* buffer);
    
    // Gestion FAT (inspirée du fat.c)
    uint16_t fat_search_available_cluster(uint16_t current_cluster);
    // For FAT32, FAT entries are 32-bit (upper 4 bits reserved). Use 32-bit types.
    uint32_t fat_entry(uint32_t fat_entry, uint32_t fat_value, bool write_entry);
    
    // Parsing de noms (inspiré du fat.c)
    FileEntryType fat_filename_parser(root_Entries* dir_entry);
    
    // Fonctions utilitaires pour file_close
    void update_directory_entry_size();
    void flush_fat_cache();
    
public:
    FAT32(SDCard* sd);
    ~FAT32();
    
    // Initialisation (inspirée de FAT_Read_Master_Block)
    bool init();
    bool is_initialized() const { return initialized; }
    
    // Lecture du Master Boot Record (inspirée du fat.c)
    bool read_master_block();
    void view_fat_infos();
    void view_global_fat_variables();
    
    // Opérations sur fichiers (inspirées du fat.c)
    FAT_ErrorCode file_open(const char* filename, FileFunction function);
    void file_close();
    uint16_t file_read(uint8_t* buffer, ReadHandler* handler);
    void file_write(const uint8_t* data, uint32_t size);
    
    // Listing de répertoire (support LFN)
    FAT_ErrorCode list_directory(std::vector<FileListEntry>& file_list);
    
    // Support Long File Names (inspiré du fat.c)
    bool supports_lfn() const { return true; }
    
    // Utilitaires de debug (inspirées du fat.c)
    void print_master_boot_info();
    void print_sector_hex(uint32_t sector_num);
    void print_fat_chain(uint32_t start_cluster);
    
    // Getters pour compatibilité
    uint32_t get_fat_base() const { return fat_base; }
    uint32_t get_root_base() const { return root_base; }
    uint32_t get_data_base() const { return data_base; }
    uint16_t get_sector_size() const { return sector_size; }
    uint16_t get_cluster_size() const { return cluster_size; }
    uint32_t get_root_dir_cluster() const { return root_dir_first_cluster; }
    uint32_t get_current_dir_cluster() const { return current_dir_cluster_; }
    
    // Méthodes avancées inspirées du fat.c
    bool create_file(const char* filename);
    bool delete_file(const char* filename);
    bool file_exists(const char* filename);
    uint32_t get_file_size(const char* filename);
    bool rename_file(const char* old_name, const char* new_name);
    
    // Navigation dans l'arborescence
    // Changer de répertoire. Supporte "/" pour racine et chemins simples avec '/'
    bool change_directory(const char* dir_name);
    bool create_directory(const char* dir_name);
    std::vector<std::string> get_directory_tree();
    
    // Informations système
    uint32_t get_free_space();
    uint32_t get_total_space();
    float get_free_space_percent();
    
    // Défragmentation et maintenance
    uint32_t count_free_clusters();
    void cleanup_deleted_files();
};

// Fonctions utilitaires globales inspirées du fat.c
namespace FAT_Utils {
    // Constantes pour les positions des caractères LFN
    // Détails d’implémentation internes déplacés dans FAT32.cpp
    // (la constante n’est pas utilisée en dehors, on évite donc l’exposition ici)
    
   
    // Validation de noms
    bool is_valid_filename(const char* filename);
    bool iequals(const char* a, const char* b);
    bool to_dos_8_3(const char* name, char out11[11]);
    
    // Utilitaires de date/heure FAT
    uint16_t system_time_to_fat_time();
    uint16_t system_date_to_fat_date();
    void fat_time_to_system_time(uint16_t fat_time, uint16_t fat_date, 
                                 int& year, int& month, int& day, 
                                 int& hour, int& minute, int& second);
    
    // Debug et affichage
    void print_file_attributes(uint8_t attributes);
    void print_file_entry(const root_Entries* entry);
    void print_lfn_entry(const DIR_ENT_LFN* lfn_entry);
}