#include "FAT32.h"
#include "SDCard.h"
#include "FAT32_Structures.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <vector>
#include <string>
#include <functional>

/*******************************************************
 * Nom du fichier : FAT32.cpp
 * Auteur         : Guillaume Sahuc
 * Date           : 23 novembre 2025
 * Description    : driver FAT32
 *******************************************************/

 namespace FAT_Utils {

// Positions des caractères LFN
constexpr uint8_t LFN_CHAR_POSITIONS[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
};

}

FAT32::FAT32(SDCard* sd) 
    : sd_card(sd), initialized(false), sectors_per_fat(0), fat_size(0),
      sectors_in_partition(0), sector_size(512), cluster_size(0),
            fat_base(0), root_base(0), data_base(0), main_offset(0), last_cluster(0) {
    memset(read_buffer, 0, sizeof(read_buffer));
    memset(write_buffer, 0, sizeof(write_buffer));
    memset(&master_boot, 0, sizeof(master_boot));
        // Init FAT cache
        fat_cache_sector = 0xFFFFFFFFu;
        fat_cache_valid = false;
        memset(fat_cache, 0, sizeof(fat_cache));
}

FAT32::~FAT32() {
}

bool FAT32::init() {
    if (!sd_card || !sd_card->is_initialized()) {
        printf("FAT32  Carte SD non disponible\n");
        return false;
    }
    
    printf("FAT32  Initialisation...\n");
    
    // Lire le Master Boot Record
    if (!read_master_block()) {
        printf("FAT32  Erreur lecture Master Boot Record\n");
        return false;
    }
    
    initialized = true;
    printf("FAT32  Système initialisé avec succès\n");
    // Démarrer dans le répertoire racine
    current_dir_cluster_ = root_dir_first_cluster;
    
    // Afficher les informations
    view_fat_infos();
    
    return true;
}

// Détection MBR/BPB et lecture BPB (FAT32) sans byte-swap (RP2040 = little-endian)
bool FAT32::read_master_block() {
    printf("FAT32  Lecture du secteur 0...\n");

    // Lire secteur 0
    if (!get_physical_block(0, read_buffer)) {
        printf("Erreur lecture secteur 0\n");
        return false;
    }

    auto sig = (uint16_t)(read_buffer[510] | (read_buffer[511] << 8));
    if (sig != 0xAA55) {
        printf("Signature 0x55AA absente au secteur 0\n");
        return false;
    }

    // Détecter BPB direct (superfloppy) vs MBR
    bool looks_like_bpb = false;
    if ((read_buffer[0] == 0xEB && read_buffer[2] == 0x90) || (read_buffer[0] == 0xE9)) {
        looks_like_bpb = true; // EB xx 90 ou E9 xx xx
    }

    uint32_t bpb_lba = 0;
    if (!looks_like_bpb) {
        // MBR: chercher partition FAT32 (type 0x0B ou 0x0C)
        const uint8_t* p = read_buffer + 0x1BE;
        for (int i = 0; i < 4; i++, p += 16) {
            uint8_t ptype = p[4];
            if (ptype == 0x0B || ptype == 0x0C) {
                uint32_t start_lba = (uint32_t)p[8] | ((uint32_t)p[9] << 8) | ((uint32_t)p[10] << 16) | ((uint32_t)p[11] << 24);
                bpb_lba = start_lba;
                break;
            }
        }

        if (bpb_lba == 0) {
            // Pas de partition FAT32 trouvée, tenter tout de même BPB à 0
            bpb_lba = 0;
        }
    }

    // Lire le secteur BPB
    if (!get_physical_block(bpb_lba, read_buffer)) {
        printf("Erreur lecture BPB à LBA %lu\n", bpb_lba);
        return false;
    }

    // Vérifier signature et jump
    sig = (uint16_t)(read_buffer[510] | (read_buffer[511] << 8));
    if (sig != 0xAA55) {
        printf("Signature 0x55AA absente au BPB\n");
        return false;
    }
    if (!((read_buffer[0] == 0xEB && read_buffer[2] == 0x90) || (read_buffer[0] == 0xE9))) {
        printf("BPB: opcode de saut inattendu (0x%02X 0x%02X 0x%02X)\n", read_buffer[0], read_buffer[1], read_buffer[2]);
    }

    // Parser BPB via structure canonique
    FAT32_BootRecord bpb{};
    memcpy(&bpb, read_buffer, sizeof(FAT32_BootRecord));

    // Déterminer taille FAT et total secteurs
    uint32_t fatsz = (bpb.fat_size_16 != 0) ? bpb.fat_size_16 : bpb.fat_size_32;
    uint32_t tot_sec = (bpb.total_sectors_16 != 0) ? bpb.total_sectors_16 : bpb.total_sectors_32;

    // Renseigner variables de classe
    main_offset = bpb_lba;
    sector_size = bpb.bytes_per_sector;
    cluster_size = bpb.sectors_per_cluster;
    sectors_per_fat = fatsz;
    sectors_in_partition = tot_sec;
    fat_base = main_offset + bpb.reserved_sectors;
    root_dir_first_cluster = bpb.root_cluster;

    // Calcul secteurs data
    uint32_t root_dir_sectors = ((uint32_t)bpb.root_entries * 32U + (sector_size - 1U)) / sector_size;
    first_data_sector = bpb.reserved_sectors + (bpb.fat_count * fatsz) + root_dir_sectors;
    data_base = main_offset + first_data_sector;

    // Clusters disponibles
    uint32_t data_sectors = tot_sec - first_data_sector;
    uint32_t total_clusters = data_sectors / cluster_size;
    last_cluster = total_clusters + 1; // borne supérieure approximative

    // Quelques logs
    printf("FAT32  BPB à LBA %lu, RootClus=%lu, Sec/Clus=%u, Byts/Sec=%u\n",
           (unsigned long)bpb_lba, (unsigned long)root_dir_first_cluster,
           (unsigned)cluster_size, (unsigned)sector_size);

    // commence à l'offset 3 dans le secteur BPB (les 3 premiers octets sont l'instruction jump).
    memset(&master_boot, 0, sizeof(master_boot));
    // OEM name (8 bytes at offset 3 in the boot sector)
    memcpy(master_boot.OEMName, bpb.oem_name, sizeof(master_boot.OEMName));
    master_boot.BytesPerSector = bpb.bytes_per_sector;
    master_boot.SectorsPerCluster = bpb.sectors_per_cluster;
    master_boot.ReservedSectors = bpb.reserved_sectors;
    master_boot.FatCopies = bpb.fat_count;
    master_boot.RootDirectoryEntries = bpb.root_entries;
    master_boot.SectorsLess32MB = bpb.total_sectors_16;
    master_boot.MediaDescriptor = bpb.media_type;
    // Sectors per FAT: prefer 16-bit field if present, otherwise lower 16 bits of 32-bit field
    master_boot.SectorsPerFat = (bpb.fat_size_16 != 0) ? bpb.fat_size_16 : (uint16_t)(bpb.fat_size_32 & 0xFFFF);
    master_boot.SectorsPerTrack = bpb.sectors_per_track;
    master_boot.NumberOfHeads = bpb.head_count;
    master_boot.HiddenSectors = bpb.hidden_sectors;
    master_boot.SectorsInPartition = bpb.total_sectors_32;

    return true;
}

void FAT32::view_fat_infos() {
    printf("=== Informations FAT ===\n");
    printf("OEM NAME = %.8s\n", master_boot.OEMName);
    printf("BytesPerSector = %d\n", master_boot.BytesPerSector);
    printf("SectorsPerCluster = %d\n", master_boot.SectorsPerCluster);
    printf("ReservedSectors = %d\n", master_boot.ReservedSectors);
    printf("FatCopies = %d\n", master_boot.FatCopies);
    printf("RootDirectoryEntries = %d\n", master_boot.RootDirectoryEntries);
    printf("SectorsLess32MB = %d\n", master_boot.SectorsLess32MB);
    printf("MediaDescriptor = %d\n", master_boot.MediaDescriptor);
    printf("SectorsPerFat = %d\n", master_boot.SectorsPerFat);
    printf("SectorsPerTrack = %d\n", master_boot.SectorsPerTrack);
    printf("NumberOfHeads = %d\n", master_boot.NumberOfHeads);
    printf("HiddenSectors = %ld\n", master_boot.HiddenSectors);
    
    view_global_fat_variables();
}

void FAT32::view_global_fat_variables() {
    printf("=== Variables FAT Calculées ===\n");
    printf("FAT_FAT_BASE = %d\n", fat_base);
    printf("FAT_Data_BASE = %d\n", data_base);
    printf("SectorsInPartition = %ld\n", sectors_in_partition);
    printf("FirstDataSector = %lu\n", (unsigned long)first_data_sector);
    printf("Last_Cluster = %d\n", last_cluster);
}

// (avec support LFN)
FAT_ErrorCode FAT32::list_directory(std::vector<FileListEntry>& file_list) {
    uint16_t index;
    uint32_t cluster = current_dir_cluster_ ? current_dir_cluster_ : root_dir_first_cluster;
    FAT_ErrorCode error_code = ERROR_IDLE;
    root_Entries* file_structure;
    FileEntryType type;
    bool lfn_flag = false;
    // Utiliser un buffer dynamique pour éviter un gros tableau sur la pile
    std::vector<char> filename_buf;
    filename_buf.resize(FAT_Config::MAX_LFN_CHARACTERS);
    // Buffer pour sauvegarder le LFN avant que filename_buf soit écrasé par le nom DOS
    char saved_lfn[FAT_Config::MAX_LFN_CHARACTERS];
    saved_lfn[0] = '\0';
    bool done = false; // allow early exit when end-of-directory marker is found
    
    // printf("=== Liste des fichiers (mode avancé avec LFN) ===\n");
    file_list.clear();
    // Small capacity hint to reduce early reallocations; grows as needed
    file_list.reserve(62);
    
    while (cluster < FAT32_Cluster::EOC_MIN && error_code == ERROR_IDLE && !done) {
        for (uint16_t sec = 0; sec < cluster_size; ++sec) {
            uint32_t lba = data_base + ((cluster - 2) * cluster_size) + sec;
            if (!get_physical_block(lba, read_buffer)) {
                printf("Erreur lecture secteur %lu\n", (unsigned long)lba);
                return error_code;
            }

            file_structure = (root_Entries*)read_buffer;
            index = 0;
            const uint16_t entries_per_sector = sector_size / sizeof(root_Entries);

            while (index < entries_per_sector && error_code == ERROR_IDLE) {
                // Early end-of-directory marker: no valid entries follow in this directory
                if (file_structure->FileName[0] == FAT_Config::FILE_CLEAR) {
                    done = true;
                    break;
                }
                type = fat_filename_parser(file_structure, filename_buf.data());

                switch (type) {
                case _File:
                    {
                        FileListEntry entry;
                        // Toujours copier le nom DOS (c'est ce qui est dans filename_buf pour _File)
                        strncpy(entry.dosFileName, filename_buf.data(), sizeof(entry.dosFileName) - 1);
                        entry.dosFileName[sizeof(entry.dosFileName) - 1] = '\0';
                        
                        if (lfn_flag && saved_lfn[0] != '\0') {
                            // On avait un LFN précédent sauvegardé, le copier
                            strncpy(entry.longFileName, saved_lfn, sizeof(entry.longFileName) - 1);
                            entry.longFileName[sizeof(entry.longFileName) - 1] = '\0';
                            entry.hasLongName = true;
                            lfn_flag = false;
                            saved_lfn[0] = '\0'; // reset
                        } else {
                            entry.hasLongName = false;
                            entry.longFileName[0] = '\0';
                        }
                        entry.type = _File;
                        entry.size = file_structure->SizeofFile;
                        file_list.push_back(entry);
                    }
                    break;
                    
                case _Directory:
                    {
                        FileListEntry entry;
                        // Toujours copier le nom DOS
                        strncpy(entry.dosFileName, filename_buf.data(), sizeof(entry.dosFileName) - 1);
                        entry.dosFileName[sizeof(entry.dosFileName) - 1] = '\0';
                        
                        if (lfn_flag && saved_lfn[0] != '\0') {
                            strncpy(entry.longFileName, saved_lfn, sizeof(entry.longFileName) - 1);
                            entry.longFileName[sizeof(entry.longFileName) - 1] = '\0';
                            entry.hasLongName = true;
                            lfn_flag = false;
                            saved_lfn[0] = '\0'; // reset
                        } else {
                            entry.hasLongName = false;
                            entry.longFileName[0] = '\0';
                        }
                        entry.type = _Directory;
                        entry.size = 0; // Les dossiers n'ont pas de taille
                        file_list.push_back(entry);
                    }
                    break;
                    
                case _LongFileNameOK:
                    // Nom long complet reçu - le sauvegarder avant qu'il soit écrasé
                    strncpy(saved_lfn, filename_buf.data(), sizeof(saved_lfn) - 1);
                    saved_lfn[sizeof(saved_lfn) - 1] = '\0';
                    lfn_flag = true;
                    break;
                    
                case _LongFileNameNOK:
                    // Nom long incomplet, continuer
                    break;
                    
                case _Error:
                default:
                    // Ignorer les entrées invalides
                    break;
                }
                index++;
                file_structure++;
            }
            if (done) break; // stop scanning further sectors in this directory
        }
        if (!done) {
            uint32_t next = fat_entry(cluster, 0, false);
            if (next >= FAT32_Cluster::EOC_MIN || next == 0) break;
            cluster = next;
        }
    }
    
    // Affichage de la liste supprimé pour éviter le double affichage
    
    return error_code;
}

FileEntryType FAT32::fat_filename_parser(root_Entries* dir_entry, char* filename_out) {
    uint8_t i = 0, j = 0;
    DIR_ENT_LFN* lfn_entry = (DIR_ENT_LFN*)dir_entry;
    uint8_t* dir_bytes = (uint8_t*)dir_entry;
    static uint8_t lfn_index = 0;
    
    filename_out[0] = '\0';
    
    if (dir_entry->FileName[0] != FAT_Config::FILE_CLEAR) {
        // Ignorer les entrées effacées et les labels de volume
        if ((dir_entry->FileName[0] != FAT_Config::FILE_ERASED) &&
            !(dir_entry->Attributes & FAT_Config::AT_VOLUME_ID)) {
            
            if (dir_entry->Attributes == FAT_Config::AT_LFN) {
                // Entrée Long File Name 
                if (lfn_entry->Ordinal > 0x40) {
                    lfn_index = ((lfn_entry->Ordinal - 0x41) * 13);
                    filename_out[lfn_index + 13] = '\0';
                } else {
                    lfn_index -= 13;
                }
                
                // Extraire les caractères aux bonnes positions (UTF-16LE -> ASCII basique)
                // Arrêt sur 0x0000 (fin de chaîne) ou 0xFFFF (padding)
                for (i = 0; i <= 12; i++) {
                    uint8_t pos = FAT_Utils::LFN_CHAR_POSITIONS[i];
                    uint8_t lo = dir_bytes[pos];
                    uint8_t hi = dir_bytes[pos + 1];
                    // Fin de chaîne explicite
                    if (lo == 0x00 && hi == 0x00) {
                        filename_out[lfn_index + i] = '\0';
                        break;
                    }
                    // Zone de padding non utilisée
                    if (lo == 0xFF && hi == 0xFF) {
                        break;
                    }
                    // Copie du LSB ASCII (les caractères non-ASCII seront dégradés)
                    filename_out[lfn_index + i] = lo;
                }
                
                if (lfn_index != 0) {
                    return _LongFileNameNOK; // Nom pas complet
                } else {
                    return _LongFileNameOK;  // Nom complet
                }
            } else {
                // Entrée 8.3 standard 
                for (i = 0; (i < 8) && (dir_entry->FileName[i] != 0x20); i++) {
                    filename_out[i] = dir_entry->FileName[i];
                }
                
                // Vérifier si c'est un répertoire (peut avoir d'autres attributs en plus)
                if (dir_entry->Attributes & FAT_Config::AT_DIRECTORY) {
                    filename_out[i++] = '\\';
                    filename_out[i] = '\0';
                    return _Directory;
                } else {
                    filename_out[i++] = '.';
                    for (j = 0; j < 3; j++) {
                        filename_out[i++] = dir_entry->Extension[j];
                    }
                    filename_out[i] = '\0';
                    return _File;
                }
            }
        }
    }
    
    return _Error;
}



bool FAT32::get_physical_block(uint32_t block_num, uint8_t* buffer) {
    return sd_card->read_block(block_num, buffer);
}

bool FAT32::store_physical_block(uint32_t block_num, const uint8_t* buffer) {
    return sd_card->write_block(block_num, buffer);
}

// Byte swap helpers (not strictly needed on little-endian RP2040, but kept for completeness)
uint32_t FAT32::lword_swap(uint32_t data) {
    return ((data & 0xFF) << 24) | ((data & 0xFF00) << 8) | ((data & 0xFF0000) >> 8) | ((data >> 24) & 0xFF);
}

uint16_t FAT32::byte_swap(uint16_t data) {
    return (uint16_t)((data << 8) | (data >> 8));
}

struct DirPos { uint32_t cluster; uint32_t lba; uint16_t index; };

FAT_ErrorCode FAT32::file_open(const char* filename, FileFunction function) {
    if (!initialized || !filename || !*filename) {
        return FILE_NOT_FOUND;
    }

    // Start from root if absolute path, else from current directory
    uint32_t dir_cluster = root_dir_first_cluster;
    const char* p = filename;
    if (*p == '/') {
        // absolute
        ++p;
    } else if (current_dir_cluster_ >= 2) {
        dir_cluster = current_dir_cluster_;
    }

    // Duplicate path for tokenization
    char path_buf[ FAT32_Config::MAX_PATH_LENGTH + 1 ];
    strncpy(path_buf, p, sizeof(path_buf) - 1);
    path_buf[sizeof(path_buf) - 1] = '\0';

    // Helper lambdas
    auto is_end = [](char* t){ return t == nullptr || *t == '\0'; };
    auto trim_dir_suffix = [](char* name){
        size_t n = strlen(name);
        if (n > 0 && (name[n-1] == '/' || name[n-1] == '\\')) name[n-1] = '\0';
    };

    FAT_ErrorCode result = FILE_NOT_FOUND;

    // Tokenize by '/'
    char* ctx = nullptr;
    char* token = strtok_r(path_buf, "/", &ctx);
    if (!token) {
        // Path was "/" or empty after '/'
        return FILE_NOT_FOUND;
    }

    // Iterate components
    while (token) {
        // Handle '.' and '..' minimally
        if (strcmp(token, ".") == 0) {
            token = strtok_r(nullptr, "/", &ctx);
            continue;
        }
        if (strcmp(token, "..") == 0) {
            // Not implemented: no parent tracking without path stack
            return FILE_NOT_FOUND;
        }

        bool last_component = (strtok_r(nullptr, "/", &ctx) == nullptr);
        break; // we'll re-parse with array approach below
    }

    // Re-parse path into vector of parts
    std::vector<std::string> parts;
    {
        char tmp[FAT32_Config::MAX_PATH_LENGTH + 1];
        strncpy(tmp, p, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char* c2 = nullptr;
        char* t2 = strtok_r(tmp, "/", &c2);
        while (t2) {
            if (strcmp(t2, ".") != 0 && *t2) parts.emplace_back(t2);
            t2 = strtok_r(nullptr, "/", &c2);
        }
    }
    if (parts.empty()) return FILE_NOT_FOUND;

    // Walk through directories to reach the parent directory of the target
    for (size_t idx = 0; idx < parts.size(); ++idx) {
        const bool is_last = (idx == parts.size() - 1);
        const std::string& name = parts[idx];

        // Scan the directory cluster chain
        bool found = false;
        uint32_t cluster = dir_cluster;
        // State to carry LFN from preceding entries
        bool lfn_flag = false;
        char lfn_name[FAT_Config::MAX_LFN_CHARACTERS];
        lfn_name[0] = '\0';

        DirPos found_pos{0,0,0};
        while (cluster >= 2 && cluster < FAT32_Cluster::EOC_MIN && !found) {
            for (uint16_t sec = 0; sec < cluster_size && !found; ++sec) {
                uint32_t lba = data_base + ((cluster - 2) * cluster_size) + sec;
                if (!get_physical_block(lba, read_buffer)) {
                    return FILE_NOT_FOUND;
                }
                root_Entries* entries = (root_Entries*)read_buffer;
                const uint16_t ents = sector_size / sizeof(root_Entries);

                for (uint16_t i = 0; i < ents; ++i) {
                    root_Entries* e = &entries[i];
                    if (e->FileName[0] == FAT_Config::FILE_CLEAR) {
                        // End of directory
                        found = false; // ensures not found
                        break;
                    }
                    if (e->FileName[0] == FAT_Config::FILE_ERASED) continue;

                    // Use existing parser to assemble LFN statefully
                    char parsed_name[FAT_Config::MAX_LFN_CHARACTERS];
                    FileEntryType type = fat_filename_parser(e, parsed_name);
                    if (type == _LongFileNameOK) {
                        // parsed_name now holds full LFN for the next SFN entry
                        strncpy(lfn_name, parsed_name, sizeof(lfn_name) - 1);
                        lfn_name[sizeof(lfn_name) - 1] = '\0';
                        lfn_flag = true;
                        continue;
                    } else if (type == _LongFileNameNOK) {
                        // continue collecting
                        continue;
                    } else if (type == _Error) {
                        // skip invalid entries
                        continue;
                    }

                    // For a normal 8.3 entry (file or directory), decide the display name
                    char comp_name[FAT_Config::MAX_LFN_CHARACTERS];
                    comp_name[0] = '\0';
                    if (lfn_flag && lfn_name[0]) {
                        strncpy(comp_name, lfn_name, sizeof(comp_name) - 1);
                        comp_name[sizeof(comp_name) - 1] = '\0';
                    } else {
                        // Build DOS name (without trailing backslash for directories)
                        if (type == _Directory) {
                            int k = 0;
                            for (int c = 0; c < 8 && e->FileName[c] != ' '; ++c) comp_name[k++] = (char)e->FileName[c];
                            comp_name[k] = '\0';
                        } else { // _File
                            int k = 0;
                            for (int c = 0; c < 8 && e->FileName[c] != ' '; ++c) comp_name[k++] = (char)e->FileName[c];
                            if (e->Extension[0] != ' ') {
                                comp_name[k++] = '.';
                                for (int c = 0; c < 3 && e->Extension[c] != ' '; ++c) comp_name[k++] = (char)e->Extension[c];
                            }
                            comp_name[k] = '\0';
                        }
                    }

                    // Reset LFN flag for the next sequence (as we consumed the SFN entry)
                    lfn_flag = false;
                    lfn_name[0] = '\0';

                    // Compare case-insensitively
                    if (FAT_Utils::iequals(comp_name, name.c_str())) {
                        uint32_t first_cluster = ((uint32_t)e->FirstClusterHigh << 16) | (uint32_t)e->FirstClusterNumber;
                        if (is_last) {
                            // Expect a file for READ
                            found_pos = { cluster, lba, i };
                            if (function == READ) {
                                if ((e->Attributes & FAT_Config::AT_DIRECTORY) == 0) {
                                    // Initialize read handler
                                    read_handler.Dir_Entry = 0; // optional
                                    read_handler.File_Size = e->SizeofFile;
                                    read_handler.FAT_Entry = first_cluster;
                                    read_handler.SectorOffset = 0;
                                    return FILE_FOUND;
                                } else {
                                    // matched a directory when expecting a file
                                    return FILE_NOT_FOUND;
                                }
                            } else if (function == DELETE) {
                                if (e->Attributes & FAT_Config::AT_DIRECTORY) {
                                    // refuse deleting directories here
                                    return FILE_NOT_FOUND;
                                }
                                // free cluster chain if any
                                uint32_t c = first_cluster;
                                while (c >= 2 && c < FAT32_Cluster::EOC_MIN) {
                                    uint32_t next = fat_entry(c, 0, false);
                                    (void)fat_entry(c, FAT_Config::CLUSTER_FREE, true);
                                    c = next;
                                }
                                // mark directory entry as erased
                                root_Entries* ee = (root_Entries*)read_buffer;
                                ee[i].FileName[0] = FAT_Config::FILE_ERASED;
                                if (!store_physical_block(lba, (uint8_t*)ee)) {
                                    return FILE_NOT_FOUND;
                                }
                                return FILE_FOUND;
                            } else if (function == OVERWRITE || function == MODIFY) {
                                if (e->Attributes & FAT_Config::AT_DIRECTORY) return FILE_NOT_FOUND;
                                // Truncate for OVERWRITE; for MODIFY we keep as is (start at beginning)
                                if (function == OVERWRITE) {
                                    // free chain
                                    uint32_t c = first_cluster;
                                    while (c >= 2 && c < FAT32_Cluster::EOC_MIN) {
                                        uint32_t next = fat_entry(c, 0, false);
                                        (void)fat_entry(c, FAT_Config::CLUSTER_FREE, true);
                                        c = next;
                                    }
                                    // set cluster to 0 and size 0
                                    root_Entries* ee = (root_Entries*)read_buffer;
                                    ee[i].FirstClusterHigh = 0;
                                    ee[i].FirstClusterNumber = 0;
                                    ee[i].SizeofFile = 0;
                                    if (!store_physical_block(lba, (uint8_t*)ee)) return FILE_NOT_FOUND;
                                    first_cluster = 0;
                                }
                                // initialize write handler
                                write_handler.Dir_Entry = lba; // store lba for updating size
                                write_handler.File_Size = ((root_Entries*)read_buffer)[i].SizeofFile;
                                write_handler.BaseFatEntry = first_cluster;
                                write_handler.CurrentFatEntry = first_cluster;
                                write_handler.ClusterIndex = 0;
                                write_handler.SectorIndex = 0;
                                return FILE_FOUND;
                            } else if (function == CREATE) {
                                // already exists: treat as overwrite/truncate
                                if (e->Attributes & FAT_Config::AT_DIRECTORY) return FILE_NOT_FOUND;
                                // free chain
                                uint32_t c = first_cluster;
                                while (c >= 2 && c < FAT32_Cluster::EOC_MIN) {
                                    uint32_t next = fat_entry(c, 0, false);
                                    (void)fat_entry(c, FAT_Config::CLUSTER_FREE, true);
                                    c = next;
                                }
                                root_Entries* ee = (root_Entries*)read_buffer;
                                ee[i].FirstClusterHigh = 0;
                                ee[i].FirstClusterNumber = 0;
                                ee[i].SizeofFile = 0;
                                if (!store_physical_block(lba, (uint8_t*)ee)) return FILE_NOT_FOUND;
                                write_handler.Dir_Entry = lba;
                                write_handler.File_Size = 0;
                                write_handler.BaseFatEntry = 0;
                                write_handler.CurrentFatEntry = 0;
                                write_handler.ClusterIndex = 0;
                                write_handler.SectorIndex = 0;
                                return FILE_CREATE_OK;
                            }
                        } else {
                            // Descend into directory if it's a directory
                            if (e->Attributes & FAT_Config::AT_DIRECTORY) {
                                if (first_cluster >= 2 && first_cluster < FAT32_Cluster::EOC_MIN) {
                                    dir_cluster = first_cluster;
                                    found = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            if (!found) {
                uint32_t next = fat_entry(cluster, 0, false);
                if (next >= FAT32_Cluster::EOC_MIN || next == 0) break;
                cluster = next;
            }
        }

        if (!found && !is_last) {
            // Intermediate directory not found
            return FILE_NOT_FOUND;
        }
        // If this was an intermediate directory, loop continues with new dir_cluster
        if (!parts.empty() && &name != &parts.back()) {
            // dir_cluster already updated when found == true
            continue;
        }
        // If last and not returned earlier, file not found. Handle create scenarios.
        if (&name == &parts.back()) {
            if (function == READ || function == DELETE || function == MODIFY) {
                return FILE_NOT_FOUND;
            }
            // CREATE or OVERWRITE when not found: create a new entry with 0 size, no cluster yet
            // Find free directory entry slot (or extend directory)
            // Search end-of-directory or erased entry
            uint32_t cluster2 = dir_cluster;
            bool placed = false;
            char dos11[11];
            if (!FAT_Utils::to_dos_8_3(name.c_str(), dos11)) {
                return FILE_NOT_FOUND;
            }
            while (cluster2 >= 2 && cluster2 < FAT32_Cluster::EOC_MIN && !placed) {
                for (uint16_t sec = 0; sec < cluster_size && !placed; ++sec) {
                    uint32_t lba = data_base + ((cluster2 - 2) * cluster_size) + sec;
                    if (!get_physical_block(lba, read_buffer)) return FILE_NOT_FOUND;
                    root_Entries* entries = (root_Entries*)read_buffer;
                    const uint16_t ents = sector_size / sizeof(root_Entries);
                    for (uint16_t i = 0; i < ents; ++i) {
                        if (entries[i].FileName[0] == FAT_Config::FILE_CLEAR || entries[i].FileName[0] == FAT_Config::FILE_ERASED) {
                            // prepare new 8.3 entry
                            memset(&entries[i], 0, sizeof(root_Entries));
                            memcpy(entries[i].FileName, dos11, 8);
                            memcpy(entries[i].Extension, dos11 + 8, 3);
                            entries[i].Attributes = FAT_Config::AT_ARCHIVE;
                            entries[i].FirstClusterHigh = 0;
                            entries[i].FirstClusterNumber = 0;
                            entries[i].SizeofFile = 0;
                            if (!store_physical_block(lba, (uint8_t*)entries)) return FILE_NOT_FOUND;
                            // initialize write handler for subsequent write
                            write_handler.Dir_Entry = lba;
                            write_handler.File_Size = 0;
                            write_handler.BaseFatEntry = 0;
                            write_handler.CurrentFatEntry = 0;
                            write_handler.ClusterIndex = 0;
                            write_handler.SectorIndex = 0;
                            return FILE_CREATE_OK;
                        }
                    }
                }
                // No free slot in this cluster, extend directory by allocating a new cluster
                uint32_t next = fat_entry(cluster2, 0, false);
                if (next >= FAT32_Cluster::EOC_MIN || next == 0) {
                    // allocate new cluster for directory
                    uint16_t freec = fat_search_available_cluster((uint16_t)cluster2);
                    if (freec < 2) return NO_MORE_FREE_CLUSTER;
                    // link chain
                    (void)fat_entry(cluster2, freec, true);
                    (void)fat_entry(freec, FAT32_Cluster::EOC_MIN, true);
                    // zero newly allocated directory cluster
                    {
                        uint8_t z[512]; memset(z, 0, sizeof(z));
                        for (uint16_t s = 0; s < cluster_size; ++s) {
                            uint32_t zlba = data_base + ((freec - 2) * cluster_size) + s;
                            if (!store_physical_block(zlba, z)) return FILE_NOT_FOUND;
                        }
                    }
                    // Place first entry at beginning of new cluster
                    uint32_t lba = data_base + ((freec - 2) * cluster_size) + 0;
                    if (!get_physical_block(lba, read_buffer)) return FILE_NOT_FOUND;
                    root_Entries* entries = (root_Entries*)read_buffer;
                    memset(entries, 0, sector_size);
                    char dos11_2[11]; memcpy(dos11_2, dos11, 11);
                    memcpy(entries[0].FileName, dos11_2, 8);
                    memcpy(entries[0].Extension, dos11_2 + 8, 3);
                    entries[0].Attributes = FAT_Config::AT_ARCHIVE;
                    entries[0].FirstClusterHigh = 0;
                    entries[0].FirstClusterNumber = 0;
                    entries[0].SizeofFile = 0;
                    if (!store_physical_block(lba, (uint8_t*)entries)) return FILE_NOT_FOUND;
                    write_handler.Dir_Entry = lba;
                    write_handler.File_Size = 0;
                    write_handler.BaseFatEntry = 0;
                    write_handler.CurrentFatEntry = 0;
                    write_handler.ClusterIndex = 0;
                    write_handler.SectorIndex = 0;
                    return FILE_CREATE_OK;
                } else {
                    cluster2 = next;
                }
            }
            return FILE_NOT_FOUND;
        }
    }

    return result;
}

void FAT32::file_close() {
    // 1. Finaliser l'écriture si un fichier était ouvert en écriture
    if (write_handler.Dir_Entry != 0) {
        // Mettre à jour la taille finale du fichier dans l'entrée de répertoire
        update_directory_entry_size();
        
        // Vider les buffers de cache FAT si nécessaire
        flush_fat_cache();
        
        // Réinitialiser le write_handler
        write_handler = WriteHandler(); // Reset avec constructeur
        printf("  Handler d'écriture réinitialisé\n");
    }
    
    // 2. Finaliser la lecture si un fichier était ouvert en lecture  
    if (read_handler.Dir_Entry != 0) {
        // Réinitialiser le read_handler
        read_handler = ReadHandler(); // Reset avec constructeur
        printf("  Handler de lecture réinitialisé\n");
    }
    
    // 3. Assurer la synchronisation des données (important pour l'intégrité)
    // Note: En l'absence d'une méthode sync() sur SDCard, on s'assure que
    // tous les buffers sont cohérents
    
    //printf("Fichier fermé avec succès\n");
}

uint16_t FAT32::file_read(uint8_t* buffer, ReadHandler* handler) {
    if (!buffer || !initialized) {
        return 0;
    }
    // Si aucun handler fourni, utiliser l'interne
    ReadHandler* h = handler ? handler : &read_handler;
    // Si un handler externe est fourni mais non initialisé, copier l'état interne
    if (handler) {
        if (handler->File_Size == 0 && handler->FAT_Entry == 0 && read_handler.File_Size != 0) {
            *handler = read_handler;
        }
        h = handler;
    }
    
    // Vérifier s'il y a encore des données à lire
    if (h->File_Size == 0) {
        return 0; // EOF
    }
    
    uint16_t bytes_to_read = FAT_Config::SECTOR_SIZE;
    if (h->File_Size < bytes_to_read) {
        bytes_to_read = h->File_Size;
    }
    
    // Calculer le secteur à lire
    uint32_t cluster_sector = data_base + ((h->FAT_Entry - 2) * cluster_size) + h->SectorOffset;
    
    // Lire le secteur
    if (!get_physical_block(cluster_sector, read_buffer)) {
        return 0;
    }
    
    // Copier données dans buffer utilisateur
    memcpy(buffer, read_buffer, bytes_to_read);
    
    // Mettre à jour handler
    h->File_Size -= bytes_to_read;
    h->SectorOffset++;
    
    // Si on a fini le cluster, passer au suivant
    if (h->SectorOffset >= cluster_size) {
        h->SectorOffset = 0;
        
        // Lire l'entrée FAT pour le cluster suivant
        uint32_t next_cluster = fat_entry(h->FAT_Entry, 0, false);
        h->FAT_Entry = next_cluster;
        
        // Vérifier fin de chaîne
        if (h->FAT_Entry >= FAT32_Cluster::EOC_MIN || h->FAT_Entry < 2) {
            h->File_Size = 0; // EOF
        }
    }

    return bytes_to_read;
}

void FAT32::file_write(const uint8_t* data, uint32_t size) {
    if (!initialized || !data || size == 0) return;

    // Load the directory entry sector from write_handler.Dir_Entry if we need to update size/cluster
    uint32_t dir_lba = write_handler.Dir_Entry;
    if (dir_lba == 0) return; // no file opened for write

    // Ensure starting cluster
    if (write_handler.CurrentFatEntry < 2) {
        // allocate first cluster
        uint16_t newc = fat_search_available_cluster(2);
        if (newc < 2) { printf("Aucun cluster libre\n"); return; }
        // link as EOC start
        (void)fat_entry(newc, FAT32_Cluster::EOC_MIN, true);
        {
            uint8_t z[512]; memset(z, 0, sizeof(z));
            for (uint16_t s = 0; s < cluster_size; ++s) {
                uint32_t zlba = data_base + ((newc - 2) * cluster_size) + s;
                if (!store_physical_block(zlba, z)) return; // abort on error
            }
        }
        write_handler.BaseFatEntry = newc;
        write_handler.CurrentFatEntry = newc;
        write_handler.ClusterIndex = 0;
        write_handler.SectorIndex = 0;
        // update directory entry first cluster
        if (get_physical_block(dir_lba, read_buffer)) {
            root_Entries* entries = (root_Entries*)read_buffer;
            // We stored sector lba only; need to find exact entry index — we stored it in SectorIndex misused? Not available.
            // Heuristic: find first entry with non-empty name and matching size/zero cluster; we cannot robustly detect.
            // Simpler: update the first entry that has size == write_handler.File_Size and zero cluster; iterate all 16 entries.
            const uint16_t ents = sector_size / sizeof(root_Entries);
            for (uint16_t i = 0; i < ents; ++i) {
                root_Entries &e = entries[i];
                if (e.FileName[0] == FAT_Config::FILE_CLEAR) break;
                if (e.FileName[0] == FAT_Config::FILE_ERASED) continue;
                if ((e.Attributes & FAT_Config::AT_LFN) == FAT_Config::AT_LFN) continue;
                // Update if size matches and cluster is zero
                if (e.SizeofFile == write_handler.File_Size && e.FirstClusterHigh == 0 && e.FirstClusterNumber == 0) {
                    e.FirstClusterHigh = (uint16_t)((write_handler.BaseFatEntry >> 16) & 0xFFFF);
                    e.FirstClusterNumber = (uint16_t)(write_handler.BaseFatEntry & 0xFFFF);
                    if (!store_physical_block(dir_lba, (uint8_t*)entries)) {
                        printf("Erreur MAJ entrée répertoire\n");
                    }
                    break;
                }
            }
        }
    }

    uint32_t remaining = size;
    const uint8_t* p = data;
    while (remaining > 0) {
        // Compute LBA for current sector
        uint32_t cur_cluster = write_handler.CurrentFatEntry;
        uint32_t lba = data_base + ((cur_cluster - 2) * cluster_size) + write_handler.SectorIndex;

        // If we are at a fresh sector and writing less than a full sector, RMW the sector
        uint32_t chunk = std::min<uint32_t>(remaining, sector_size);
        if (chunk != sector_size) {
            if (!get_physical_block(lba, read_buffer)) return;
            memcpy(read_buffer, p, chunk);
            if (!store_physical_block(lba, read_buffer)) return;
        } else {
            if (!store_physical_block(lba, p)) return;
        }

        p += chunk;
        remaining -= chunk;
        write_handler.File_Size += chunk;
        write_handler.SectorIndex++;

        if (write_handler.SectorIndex >= cluster_size) {
            // Need next cluster
            write_handler.SectorIndex = 0;
            uint32_t next = fat_entry(cur_cluster, 0, false);
            if (next < 2 || next >= FAT32_Cluster::EOC_MIN) {
                // allocate one
                uint16_t newc = fat_search_available_cluster((uint16_t)cur_cluster);
                if (newc < 2) { printf("Aucun cluster libre\n"); return; }
                (void)fat_entry(cur_cluster, newc, true);
                (void)fat_entry(newc, FAT32_Cluster::EOC_MIN, true);
                {
                    uint8_t z[512]; memset(z, 0, sizeof(z));
                    for (uint16_t s = 0; s < cluster_size; ++s) {
                        uint32_t zlba = data_base + ((newc - 2) * cluster_size) + s;
                        if (!store_physical_block(zlba, z)) return; // abort on error
                    }
                }
                write_handler.CurrentFatEntry = newc;
            } else {
                write_handler.CurrentFatEntry = next;
            }
        }
    }

    // Update directory entry size on disk
    if (get_physical_block(dir_lba, read_buffer)) {
        root_Entries* entries = (root_Entries*)read_buffer;
        const uint16_t ents = sector_size / sizeof(root_Entries);
        for (uint16_t i = 0; i < ents; ++i) {
            root_Entries &e = entries[i];
            if (e.FileName[0] == FAT_Config::FILE_CLEAR) break;
            if (e.FileName[0] == FAT_Config::FILE_ERASED) continue;
            if ((e.Attributes & FAT_Config::AT_LFN) == FAT_Config::AT_LFN) continue;
            // Heuristic: choose first with non-zero first cluster matching BaseFatEntry if set
            uint32_t fc = ((uint32_t)e.FirstClusterHigh << 16) | e.FirstClusterNumber;
            if ((write_handler.BaseFatEntry != 0 && fc == write_handler.BaseFatEntry) || (write_handler.BaseFatEntry == 0 && fc != 0)) {
                e.SizeofFile = write_handler.File_Size;
                store_physical_block(dir_lba, (uint8_t*)entries);
                break;
            }
        }
    }
}

uint16_t FAT32::fat_search_available_cluster(uint16_t current_cluster) {
    // Linear scan of FAT for a free entry; start after current_cluster if possible
    uint32_t start = (current_cluster >= 2) ? current_cluster : 2;
    for (uint32_t c = start; c < (uint32_t)last_cluster; ++c) {
        uint32_t v = fat_entry(c, 0, false);
        if (v == FAT_Config::CLUSTER_FREE) return (uint16_t)c;
    }
    // wrap-around
    for (uint32_t c = 2; c < start; ++c) {
        uint32_t v = fat_entry(c, 0, false);
        if (v == FAT_Config::CLUSTER_FREE) return (uint16_t)c;
    }
    return 0; // none
}

uint32_t FAT32::fat_entry(uint32_t cluster_num, uint32_t fat_value, bool write_entry) {
    // FAT32: Each entry is 4 bytes, upper 4 bits reserved (masked out)
    // Returns the current value of the FAT entry for cluster_num
    // If write_entry=true, writes fat_value to that entry first
    
    if (!initialized || cluster_num < 2) {
        return 0xFFFFFFFF; // Invalid cluster
    }
    
    // Calculate which FAT sector contains this cluster's entry
    // FAT entry offset in bytes from start of FAT
    uint32_t fat_offset = cluster_num * 4;
    // Sector within FAT (relative to fat_base)
    uint32_t fat_sector_offset = fat_offset / sector_size;
    // Byte offset within that sector
    uint32_t entry_offset = fat_offset % sector_size;
    
    // Absolute LBA of the FAT sector
    uint32_t fat_lba = fat_base + fat_sector_offset;
    
    // Check if we need to read a new sector (cache miss)
    if (!fat_cache_valid || fat_cache_sector != fat_lba) {
        // Read FAT sector into cache
        if (!get_physical_block(fat_lba, fat_cache)) {
            fat_cache_valid = false;
            return 0xFFFFFFFF;
        }
        fat_cache_sector = fat_lba;
        fat_cache_valid = true;
    }
    
    // Read the 32-bit FAT entry from cache (byte-by-byte to avoid alignment issues)
    uint32_t current_value = 0;
    current_value  = (uint32_t)fat_cache[entry_offset + 0];
    current_value |= (uint32_t)fat_cache[entry_offset + 1] << 8;
    current_value |= (uint32_t)fat_cache[entry_offset + 2] << 16;
    current_value |= (uint32_t)fat_cache[entry_offset + 3] << 24;
    
    uint32_t raw_value = current_value;
    current_value &= 0x0FFFFFFF; // Mask upper 4 bits (reserved in FAT32)
    
    // If writing, update the entry and write back to SD
    if (write_entry) {
        // Preserve upper 4 bits, write lower 28 bits
        uint32_t masked_value = (raw_value & 0xF0000000) | (fat_value & 0x0FFFFFFF);
        
        // Write back byte-by-byte
        fat_cache[entry_offset + 0] = (uint8_t)(masked_value & 0xFF);
        fat_cache[entry_offset + 1] = (uint8_t)((masked_value >> 8) & 0xFF);
        fat_cache[entry_offset + 2] = (uint8_t)((masked_value >> 16) & 0xFF);
        fat_cache[entry_offset + 3] = (uint8_t)((masked_value >> 24) & 0xFF);
        
        // Write cache back to SD
        if (!store_physical_block(fat_lba, fat_cache)) {
            fat_cache_valid = false;
            return 0xFFFFFFFF;
        }
        
        // Optionally write to backup FAT (FAT32 typically has 2 copies)
        // For now, we only update the first FAT table
        // To be complete, we should write to fat_base + sectors_per_fat as well
        
        return masked_value & 0x0FFFFFFF;
    }
    
    return current_value;
}

void FAT32::update_directory_entry_size() {
    // Mettre à jour la taille du fichier dans l'entrée de répertoire
    // Cette fonction finalise l'écriture en s'assurant que la taille est correcte
    if (write_handler.Dir_Entry == 0) {
        return; // Aucun fichier ouvert en écriture
    }
    
    uint32_t dir_lba = write_handler.Dir_Entry;
    
    if (get_physical_block(dir_lba, read_buffer)) {
        root_Entries* entries = (root_Entries*)read_buffer;
        const uint16_t ents = sector_size / sizeof(root_Entries);
        bool found = false;
        
        for (uint16_t i = 0; i < ents && !found; ++i) {
            root_Entries &e = entries[i];
            if (e.FileName[0] == FAT_Config::FILE_CLEAR) break;
            if (e.FileName[0] == FAT_Config::FILE_ERASED) continue;
            if ((e.Attributes & FAT_Config::AT_LFN) == FAT_Config::AT_LFN) continue;
            
            // Identifier l'entrée correspondante par son cluster de base
            uint32_t fc = ((uint32_t)e.FirstClusterHigh << 16) | e.FirstClusterNumber;
            if (write_handler.BaseFatEntry != 0 && fc == write_handler.BaseFatEntry) {
                // Mettre à jour la taille finale du fichier
                e.SizeofFile = write_handler.File_Size;
                
                // Écrire le secteur modifié sur la carte SD
                if (store_physical_block(dir_lba, (uint8_t*)entries)) {
                    printf("  Taille fichier mise à jour: %ld bytes\n", write_handler.File_Size);
                } else {
                    printf("  Erreur: Impossible de mettre à jour la taille du fichier\n");
                }
                found = true;
            }
        }
        
        if (!found) {
            printf("  Attention: Entrée de répertoire non trouvée pour mise à jour taille\n");
        }
    } else {
        printf("  Erreur: Impossible de lire le secteur de répertoire\n");
    }
}

void FAT32::flush_fat_cache() {
    // Vider le cache FAT en s'assurant que toutes les données sont écrites
    // Si le cache est valide et a été modifié, on s'assure qu'il est synchronisé
    if (fat_cache_valid) {
        // Le cache FAT est automatiquement écrit lors des modifications
        // dans fat_entry(), donc pas besoin de réécriture explicite ici.
        // On peut optionnellement invalider le cache pour forcer une relecture
        printf("  Cache FAT synchronisé\n");
    }
    
    // Optionnel: invalider le cache pour forcer une relecture à la prochaine opération
    // fat_cache_valid = false;
    // fat_cache_sector = 0xFFFFFFFFu;
}

// Méthodes utilitaires
bool FAT32::create_file(const char* filename) {
    return file_open(filename, CREATE) == FILE_CREATE_OK;
}

bool FAT32::delete_file(const char* filename) {
    return file_open(filename, DELETE) == FILE_FOUND;
}

bool FAT32::file_exists(const char* filename) {
    return file_open(filename, READ) == FILE_FOUND;
}

uint32_t FAT32::get_file_size(const char* filename) {
    if (file_open(filename, READ) == FILE_FOUND) {
        return read_handler.File_Size;
    }
    return 0;
}

void FAT32::print_master_boot_info() {
    view_fat_infos();
}

void FAT32::print_sector_hex(uint32_t sector_num) {
    if (get_physical_block(sector_num, read_buffer)) {
        printf("=== Secteur %ld (hex) ===\n", sector_num);
        for (int i = 0; i < 512; i += 16) {
            printf("%04X: ", i);
            for (int j = 0; j < 16; j++) {
                printf("%02X ", read_buffer[i + j]);
            }
            printf("| ");
            for (int j = 0; j < 16; j++) {
                uint8_t c = read_buffer[i + j];
                printf("%c", (c >= 32 && c < 127) ? c : '.');
            }
            printf("\n");
        }
    }
}

void FAT32::print_fat_chain(uint32_t start_cluster) {
    if (!initialized || start_cluster < 2) {
        printf("Cluster de départ invalide: %lu\n", (unsigned long)start_cluster);
        return;
    }
    
    printf("=== Chaîne FAT depuis cluster %lu ===\n", (unsigned long)start_cluster);
    uint32_t cluster = start_cluster;
    uint32_t count = 0;
    const uint32_t max_chain = 1000; // Limite pour éviter les boucles infinies
    
    while (cluster >= 2 && cluster < FAT32_Cluster::EOC_MIN && count < max_chain) {
        printf("Cluster %lu", (unsigned long)cluster);
        
        uint32_t next = fat_entry(cluster, 0, false);
        
        if (next >= FAT32_Cluster::EOC_MIN) {
            printf(" -> EOC (End of Chain: 0x%08lX)\n", (unsigned long)next);
            break;
        } else if (next == FAT32_Cluster::FREE) {
            printf(" -> FREE (0x00000000) - ERREUR: cluster libre dans la chaîne!\n");
            break;
        } else if (next >= FAT32_Cluster::BAD && next < FAT32_Cluster::EOC_MIN) {
            printf(" -> BAD/RESERVED (0x%08lX)\n", (unsigned long)next);
            break;
        } else if (next < 2) {
            printf(" -> INVALIDE (0x%08lX)\n", (unsigned long)next);
            break;
        } else {
            printf(" -> %lu\n", (unsigned long)next);
        }
        
        cluster = next;
        count++;
    }
    
    if (count >= max_chain) {
        printf("ATTENTION: Chaîne trop longue (> %lu clusters) - possible boucle!\n", (unsigned long)max_chain);
    }
    
    printf("Total: %lu clusters dans la chaîne\n", (unsigned long)count + 1);
}

uint32_t FAT32::get_free_space() {
    uint32_t free_clusters = count_free_clusters();
    return free_clusters * cluster_size * sector_size;
}

uint32_t FAT32::get_total_space() {
    // Total usable space (bytes) = total_clusters * cluster_size * sector_size
    // where total_clusters is derived from data sectors after reserved/FAT/root areas
    if (!initialized || cluster_size == 0 || sector_size == 0) return 0;
    uint32_t data_sectors = 0;
    if (sectors_in_partition > first_data_sector) {
        data_sectors = sectors_in_partition - first_data_sector;
    }
    uint32_t total_clusters = (cluster_size > 0) ? (data_sectors / cluster_size) : 0;
    return total_clusters * cluster_size * sector_size;
}

float FAT32::get_free_space_percent() {
    uint32_t total = get_total_space();
    if (total == 0) return 0.0f;
    
    uint32_t free = get_free_space();
    return (float)free / (float)total * 100.0f;
}

uint32_t FAT32::count_free_clusters() {
    // Parcourir la table FAT pour compter les clusters libres
    if (!initialized) {
        return 0;
    }
    
    uint32_t free_count = 0;
    uint32_t total_clusters = last_cluster;
    
    printf("Comptage des clusters libres (total: %lu)...\n", total_clusters);
    
    // Parcourir tous les secteurs de la FAT
    for (uint32_t sector = fat_base; sector < fat_base + sectors_per_fat; sector++) {
        if (!get_physical_block(sector, read_buffer)) {
            printf("Erreur lecture secteur FAT %lu\n", sector);
            continue;
        }
        
        uint32_t* fat_table = (uint32_t*)read_buffer;
        uint32_t entries_per_sector = sector_size / 4;
        
        for (uint32_t entry = 0; entry < entries_per_sector; entry++) {
            uint32_t cluster_value = fat_table[entry] & 0x0FFFFFFF;
            
            if (cluster_value == FAT_Config::CLUSTER_FREE) {
                free_count++;
            }
        }
    }
    
    printf("Clusters libres trouvés: %lu\n", free_count);
    return free_count;
}

bool FAT32::change_directory(const char* dir_name) {
    if (!initialized) return false;
    if (!dir_name || !*dir_name) return false;

    // Gérer racine
    if (strcmp(dir_name, "/") == 0) {
        current_dir_cluster_ = root_dir_first_cluster;
        return true;
    }

    // Dupliquer chemin pour tokenization simple
    char path_buf[256];
    strncpy(path_buf, dir_name, sizeof(path_buf) - 1);
    path_buf[sizeof(path_buf) - 1] = '\0';

    // Si chemin absolu, repartir de la racine
    uint32_t start_cluster = root_dir_first_cluster;
    const char* p = path_buf;
    if (*p == '/') p++;

    // Tokeniser par '/'
    char* ctx = nullptr;
    char* token = strtok_r((char*)p, "/", &ctx);
    uint32_t current = start_cluster;

    while (token) {
        // Parcourir le répertoire courant pour trouver le sous-dossier "token"
        bool found = false;
        uint32_t cluster = current;
        while (cluster >= 2 && cluster < FAT32_Cluster::EOC_MIN && !found) {
            for (uint16_t sec = 0; sec < cluster_size && !found; ++sec) {
                uint32_t lba = data_base + ((cluster - 2) * cluster_size) + sec;
                if (!get_physical_block(lba, read_buffer)) {
                    return false;
                }
                root_Entries* file_structure = (root_Entries*)read_buffer;
                uint16_t entries_per_sector = sector_size / sizeof(root_Entries);
                // Pour matcher sur LFN, on relit les entrées précédentes LFN si besoin
                // Ici on se contente de matcher sur nom DOS 8.3 (limitation simple)
                for (uint16_t i = 0; i < entries_per_sector; ++i) {
                    auto &e = file_structure[i];
                    if (e.FileName[0] == FAT_Config::FILE_CLEAR) break;
                    if (e.FileName[0] == FAT_Config::FILE_ERASED) continue;
                    if (!(e.Attributes & FAT_Config::AT_DIRECTORY)) continue;
                    if (e.Attributes == FAT_Config::AT_LFN) continue;

                    // Construire nom 8.3 lisible
                    char dos_name[13] = {0};
                    int k = 0;
                    for (int c = 0; c < 8 && e.FileName[c] != ' '; ++c) dos_name[k++] = (char)e.FileName[c];
                    dos_name[k] = '\0';
                    // Compare case-insensitive avec token (sans slash)
                    auto ieq = [](char a, char b){ return std::toupper((unsigned char)a) == std::toupper((unsigned char)b); };
                    bool match = true;
                    size_t tlen = strlen(token);
                    if (tlen != strlen(dos_name)) match = false;
                    else {
                        for (size_t m = 0; m < tlen; ++m) {
                            if (!ieq(token[m], dos_name[m])) { match = false; break; }
                        }
                    }
                    if (match) {
                        uint32_t next_cluster = ((uint32_t)e.FirstClusterHigh << 16) | (uint32_t)e.FirstClusterNumber;
                        if (next_cluster >= 2 && next_cluster < FAT32_Cluster::EOC_MIN) {
                            current = next_cluster;
                            found = true;
                            break;
                        }
                    }
                }
            }
            if (!found) {
                uint32_t next = fat_entry(cluster, 0, false);
                if (next >= FAT32_Cluster::EOC_MIN || next == 0) break;
                cluster = next;
            }
        }
        if (!found) return false;
        token = strtok_r(nullptr, "/", &ctx);
    }

    current_dir_cluster_ = current;
    return true;
}

bool FAT32::rename_file(const char* old_name, const char* new_name) {
    if (!initialized || !old_name || !new_name) {
        return false;
    }
    // Extract basenames
    auto basename = [](const char* path) -> const char* {
        const char* slash = strrchr(path, '/');
        return slash ? slash + 1 : path;
    };
    const char* old_base = basename(old_name);
    const char* new_base = basename(new_name);

    // Convert new name to 8.3
    char dos11_new[11];
    if (!FAT_Utils::to_dos_8_3(new_base, dos11_new)) {
        return false;
    }

    // Open old file to locate its directory sector
    FAT_ErrorCode fr = file_open(old_name, MODIFY);
    if (fr != FILE_FOUND && fr != FILE_CREATE_OK) {
        return false;
    }

    uint32_t dir_lba = write_handler.Dir_Entry;
    if (dir_lba == 0) return false;
    if (!get_physical_block(dir_lba, read_buffer)) return false;
    root_Entries* entries = (root_Entries*)read_buffer;
    const uint16_t ents = sector_size / sizeof(root_Entries);

    // Ensure no collision with new name 
    for (uint16_t i = 0; i < ents; ++i) {
        root_Entries &e = entries[i];
        if (e.FileName[0] == FAT_Config::FILE_CLEAR) break;
        if (e.FileName[0] == FAT_Config::FILE_ERASED) continue;
        if (e.Attributes == FAT_Config::AT_LFN) continue;
        
        // Build current entry name
        char nm[13] = {0}; int k = 0;
        for (int c = 0; c < 8 && e.FileName[c] != ' '; ++c) nm[k++] = (char)e.FileName[c];
        if (e.Extension[0] != ' ') { nm[k++] = '.'; for (int c = 0; c < 3 && e.Extension[c] != ' '; ++c) nm[k++] = (char)e.Extension[c]; }
        // Check for conflict with other files
        if (FAT_Utils::iequals(nm, new_base)) {
            // already exists
            return false;
        }
    }

    // Find old entry in this sector and update its name
    for (uint16_t i = 0; i < ents; ++i) {
        root_Entries &e = entries[i];
        if (e.FileName[0] == FAT_Config::FILE_CLEAR) break;
        if (e.FileName[0] == FAT_Config::FILE_ERASED) continue;
        if (e.Attributes == FAT_Config::AT_LFN) continue;
        char nm[13] = {0}; int k = 0;
        for (int c = 0; c < 8 && e.FileName[c] != ' '; ++c) nm[k++] = (char)e.FileName[c];
        if (e.Extension[0] != ' ') { nm[k++] = '.'; for (int c = 0; c < 3 && e.Extension[c] != ' '; ++c) nm[k++] = (char)e.Extension[c]; }
        if (FAT_Utils::iequals(nm, old_base)) {
            memcpy(e.FileName, dos11_new, 8);
            memcpy(e.Extension, dos11_new + 8, 3);
            bool success = store_physical_block(dir_lba, (uint8_t*)entries);
            return success;
        }
    }
    return false;
}

bool FAT32::create_directory(const char* dir_name) {
    if (!initialized || !dir_name) return false;
    // Only create in current directory if name has no '/'
    if (strchr(dir_name, '/')) return false;
    char dos11[11];
    if (!FAT_Utils::to_dos_8_3(dir_name, dos11)) return false;

    uint32_t parent = current_dir_cluster_ ? current_dir_cluster_ : root_dir_first_cluster;
    // Allocate cluster for new directory
    uint16_t newc = fat_search_available_cluster((uint16_t)parent);
    if (newc < 2) return false;
    (void)fat_entry(newc, FAT32_Cluster::EOC_MIN, true);
    // Zero cluster
    {
        uint8_t z[512]; memset(z, 0, sizeof(z));
        for (uint16_t s = 0; s < cluster_size; ++s) {
            uint32_t zlba = data_base + ((newc - 2) * cluster_size) + s;
            if (!store_physical_block(zlba, z)) return false;
        }
    }
    // Write '.' and '..'
    uint32_t lba0 = data_base + ((newc - 2) * cluster_size) + 0;
    if (!get_physical_block(lba0, read_buffer)) return false;
    root_Entries* entsp = (root_Entries*)read_buffer;
    memset(entsp, 0, sector_size);
    // '.'
    memcpy(entsp[0].FileName, ".       ", 8);
    memcpy(entsp[0].Extension, "   ", 3);
    entsp[0].Attributes = FAT_Config::AT_DIRECTORY;
    entsp[0].FirstClusterHigh = (uint16_t)((newc >> 16) & 0xFFFF);
    entsp[0].FirstClusterNumber = (uint16_t)(newc & 0xFFFF);
    entsp[0].SizeofFile = 0;
    // '..'
    memcpy(entsp[1].FileName, "..      ", 8);
    memcpy(entsp[1].Extension, "   ", 3);
    entsp[1].Attributes = FAT_Config::AT_DIRECTORY;
    entsp[1].FirstClusterHigh = (uint16_t)((parent >> 16) & 0xFFFF);
    entsp[1].FirstClusterNumber = (uint16_t)(parent & 0xFFFF);
    entsp[1].SizeofFile = 0;
    if (!store_physical_block(lba0, (uint8_t*)entsp)) return false;

    // Add entry into parent directory
    uint32_t cluster = parent;
    while (cluster >= 2 && cluster < FAT32_Cluster::EOC_MIN) {
        for (uint16_t sec = 0; sec < cluster_size; ++sec) {
            uint32_t lba = data_base + ((cluster - 2) * cluster_size) + sec;
            if (!get_physical_block(lba, read_buffer)) return false;
            root_Entries* e = (root_Entries*)read_buffer;
            const uint16_t count = sector_size / sizeof(root_Entries);
            for (uint16_t i = 0; i < count; ++i) {
                if (e[i].FileName[0] == FAT_Config::FILE_CLEAR || e[i].FileName[0] == FAT_Config::FILE_ERASED) {
                    memset(&e[i], 0, sizeof(root_Entries));
                    memcpy(e[i].FileName, dos11, 8);
                    memcpy(e[i].Extension, dos11 + 8, 3);
                    e[i].Attributes = FAT_Config::AT_DIRECTORY;
                    e[i].FirstClusterHigh = (uint16_t)((newc >> 16) & 0xFFFF);
                    e[i].FirstClusterNumber = (uint16_t)(newc & 0xFFFF);
                    e[i].SizeofFile = 0;
                    return store_physical_block(lba, (uint8_t*)e);
                }
            }
        }
        uint32_t next = fat_entry(cluster, 0, false);
        if (next >= FAT32_Cluster::EOC_MIN || next == 0) {
            // Extend parent directory
            uint16_t freec = fat_search_available_cluster((uint16_t)cluster);
            if (freec < 2) return false;
            (void)fat_entry(cluster, freec, true);
            (void)fat_entry(freec, FAT32_Cluster::EOC_MIN, true);
            // zero new parent dir cluster
            uint8_t z[512]; memset(z, 0, sizeof(z));
            for (uint16_t s = 0; s < cluster_size; ++s) {
                uint32_t zlba = data_base + ((freec - 2) * cluster_size) + s;
                if (!store_physical_block(zlba, z)) return false;
            }
            // place entry at first slot
            uint32_t lba = data_base + ((freec - 2) * cluster_size) + 0;
            if (!get_physical_block(lba, read_buffer)) return false;
            root_Entries* e = (root_Entries*)read_buffer;
            memset(e, 0, sector_size);
            memcpy(e[0].FileName, dos11, 8);
            memcpy(e[0].Extension, dos11 + 8, 3);
            e[0].Attributes = FAT_Config::AT_DIRECTORY;
            e[0].FirstClusterHigh = (uint16_t)((newc >> 16) & 0xFFFF);
            e[0].FirstClusterNumber = (uint16_t)(newc & 0xFFFF);
            e[0].SizeofFile = 0;
            return store_physical_block(lba, (uint8_t*)e);
        }
        cluster = next;
    }
    return false;
}

std::vector<std::string> FAT32::get_directory_tree() {
    std::vector<std::string> list;
    if (!initialized) return list;
    uint32_t cluster = current_dir_cluster_ ? current_dir_cluster_ : root_dir_first_cluster;
    while (cluster >= 2 && cluster < FAT32_Cluster::EOC_MIN) {
        for (uint16_t sec = 0; sec < cluster_size; ++sec) {
            uint32_t lba = data_base + ((cluster - 2) * cluster_size) + sec;
            if (!get_physical_block(lba, read_buffer)) return list;
            root_Entries* e = (root_Entries*)read_buffer;
            const uint16_t ents = sector_size / sizeof(root_Entries);
            for (uint16_t i = 0; i < ents; ++i) {
                if (e[i].FileName[0] == FAT_Config::FILE_CLEAR) return list;
                if (e[i].FileName[0] == FAT_Config::FILE_ERASED) continue;
                if (e[i].Attributes == FAT_Config::AT_LFN) continue;
                char name[13] = {0}; int k = 0;
                for (int c = 0; c < 8 && e[i].FileName[c] != ' '; ++c) name[k++] = (char)e[i].FileName[c];
                if (!(e[i].Attributes & FAT_Config::AT_DIRECTORY) && e[i].Extension[0] != ' ') {
                    name[k++] = '.'; for (int c = 0; c < 3 && e[i].Extension[c] != ' '; ++c) name[k++] = (char)e[i].Extension[c];
                }
                list.emplace_back(std::string(name));
            }
        }
        uint32_t next = fat_entry(cluster, 0, false);
        if (next >= FAT32_Cluster::EOC_MIN || next == 0) break;
        cluster = next;
    }
    return list;
}

void FAT32::cleanup_deleted_files() {
    if (!initialized) {
        printf("FAT32 non initialisé\n");
        return;
    }
    
    printf("=== Nettoyage des fichiers supprimés ===\n");
    uint32_t total_freed = 0;
    uint32_t entries_compacted = 0;
    
    // Helper lambda to compact a single directory cluster
    auto compact_directory_sector = [&](uint32_t lba) -> bool {
        if (!get_physical_block(lba, read_buffer)) return false;
        
        root_Entries* entries = (root_Entries*)read_buffer;
        const uint16_t ents = sector_size / sizeof(root_Entries);
        bool modified = false;
        
        // Find erased entries and shift valid entries up
        uint16_t write_idx = 0;
        for (uint16_t read_idx = 0; read_idx < ents; ++read_idx) {
            // Stop at end-of-directory marker
            if (entries[read_idx].FileName[0] == FAT_Config::FILE_CLEAR) {
                // Fill rest with clear markers
                for (uint16_t i = write_idx; i < ents; ++i) {
                    if (entries[i].FileName[0] != FAT_Config::FILE_CLEAR) {
                        memset(&entries[i], 0, sizeof(root_Entries));
                        entries[i].FileName[0] = FAT_Config::FILE_CLEAR;
                        modified = true;
                    }
                }
                break;
            }
            
            // Skip erased entries (don't copy them)
            if (entries[read_idx].FileName[0] == FAT_Config::FILE_ERASED) {
                modified = true;
                entries_compacted++;
                continue;
            }
            
            // Copy valid entry if we've skipped any erased ones
            if (write_idx != read_idx) {
                memcpy(&entries[write_idx], &entries[read_idx], sizeof(root_Entries));
                modified = true;
            }
            write_idx++;
        }
        
        if (modified) {
            return store_physical_block(lba, read_buffer);
        }
        return true;
    };
    
    // Recursive helper to scan directory tree
    std::function<void(uint32_t)> scan_directory = [&](uint32_t dir_cluster) {
        if (dir_cluster < 2 || dir_cluster >= FAT32_Cluster::EOC_MIN) return;
        
        std::vector<uint32_t> subdirs;
        uint32_t cluster = dir_cluster;
        
        while (cluster >= 2 && cluster < FAT32_Cluster::EOC_MIN) {
            for (uint16_t sec = 0; sec < cluster_size; ++sec) {
                uint32_t lba = data_base + ((cluster - 2) * cluster_size) + sec;
                
                // First pass: identify subdirectories before compacting
                if (!get_physical_block(lba, read_buffer)) continue;
                root_Entries* entries = (root_Entries*)read_buffer;
                const uint16_t ents = sector_size / sizeof(root_Entries);
                
                for (uint16_t i = 0; i < ents; ++i) {
                    if (entries[i].FileName[0] == FAT_Config::FILE_CLEAR) break;
                    if (entries[i].FileName[0] == FAT_Config::FILE_ERASED) continue;
                    if (entries[i].Attributes == FAT_Config::AT_LFN) continue;
                    
                    // Check for subdirectory (but skip . and ..)
                    if (entries[i].Attributes & FAT_Config::AT_DIRECTORY) {
                        if (entries[i].FileName[0] != '.') {
                            uint32_t subdir = ((uint32_t)entries[i].FirstClusterHigh << 16) | entries[i].FirstClusterNumber;
                            if (subdir >= 2 && subdir < FAT32_Cluster::EOC_MIN) {
                                subdirs.push_back(subdir);
                            }
                        }
                    } else {
                        // Check for orphaned file (cluster chain broken)
                        uint32_t first_cluster = ((uint32_t)entries[i].FirstClusterHigh << 16) | entries[i].FirstClusterNumber;
                        if (first_cluster >= 2 && first_cluster < FAT32_Cluster::EOC_MIN) {
                            uint32_t val = fat_entry(first_cluster, 0, false);
                            // If first cluster is marked free, this is orphaned
                            if (val == FAT_Config::CLUSTER_FREE) {
                                printf("Fichier orphelin détecté: ");
                                for (int c = 0; c < 8 && entries[i].FileName[c] != ' '; ++c) {
                                    printf("%c", entries[i].FileName[c]);
                                }
                                printf(".");
                                for (int c = 0; c < 3 && entries[i].Extension[c] != ' '; ++c) {
                                    printf("%c", entries[i].Extension[c]);
                                }
                                printf(" (marqué comme supprimé)\n");
                                entries[i].FileName[0] = FAT_Config::FILE_ERASED;
                                store_physical_block(lba, read_buffer);
                                total_freed++;
                            }
                        }
                    }
                }
                
                // Second pass: compact this sector
                compact_directory_sector(lba);
            }
            
            uint32_t next = fat_entry(cluster, 0, false);
            if (next >= FAT32_Cluster::EOC_MIN || next == 0) break;
            cluster = next;
        }
        
        // Recursively scan subdirectories
        for (uint32_t subdir : subdirs) {
            scan_directory(subdir);
        }
    };
    
    // Start from root directory
    uint32_t root_cluster = root_dir_first_cluster;
    printf("Scan du répertoire racine (cluster %lu)...\n", (unsigned long)root_cluster);
    scan_directory(root_cluster);
    
    printf("Nettoyage terminé:\n");
    printf("  - %lu entrées supprimées compactées\n", (unsigned long)entries_compacted);
    printf("  - %lu fichiers orphelins détectés\n", (unsigned long)total_freed);
}

// Implémentations des utilitaires
namespace FAT_Utils {
    
void print_file_attributes(uint8_t attributes) {
    printf("Attributs: ");
    if (attributes & FAT_Config::AT_READONLY) printf("R");
    if (attributes & FAT_Config::AT_HIDDEN) printf("H");
    if (attributes & FAT_Config::AT_SYSTEM) printf("S");
    if (attributes & FAT_Config::AT_VOLUME_ID) printf("V");
    if (attributes & FAT_Config::AT_DIRECTORY) printf("D");
    if (attributes & FAT_Config::AT_ARCHIVE) printf("A");
    if (attributes == FAT_Config::AT_LFN) printf("LFN");
    printf(" (0x%02X)\n", attributes);
}

bool is_valid_filename(const char* filename) {
    if (!filename || strlen(filename) == 0 || strlen(filename) > 255) {
        return false;
    }
    
    // Vérifier les caractères interdits
    const char* invalid_chars = "\\/:*?\"<>|";
    for (size_t i = 0; i < strlen(filename); i++) {
        if (strchr(invalid_chars, filename[i])) {
            return false;
        }
    }
    
    return true;
}

// Convertit un nom style "Name.ext" vers un nom DOS 8.3 sur 11 octets (8 nom + 3 ext)
// - Uppercase ASCII
// - Remplit avec des espaces
// - Ignore les points multiples après le premier
// - Refuse les caractères interdits FAT
// Retourne true si conversion possible
bool to_dos_8_3(const char* name, char out11[11]) {
    if (!name) return false;
    // Caractères interdits selon FAT
    const char* invalid = "\"*/:<>?\\|+,. ;=[]"; // on tolère 1 point comme séparateur
    // Préparer buffers
    char base[9] = {0};
    char ext[4]  = {0};

    // Séparer base et extension
    const char* dot = strchr(name, '.');
    size_t base_len = 0;
    size_t ext_len = 0;
    if (dot) {
        base_len = (size_t)(dot - name);
        ext_len = strlen(dot + 1);
    } else {
        base_len = strlen(name);
        ext_len = 0;
    }

    // Copier en supprimant espaces et en uppercasant
    auto up = [](unsigned char c){ return (char)std::toupper(c); };
    // Base
    size_t bi = 0;
    for (size_t i = 0; i < base_len && bi < 8; ++i) {
        char c = name[i];
        if (c == ' ') continue; // ignorer espaces
        if (strchr(invalid, c)) return false;
        base[bi++] = up((unsigned char)c);
    }
    // Si base dépasse 8 chars ou contient seulement espaces -> invalide
    if (bi == 0 || (dot ? base_len : strlen(name)) > 255) {
        // nom vide ou bien trop long globalement
        // Noter: la limite 255 est déjà contrôlée ailleurs
    }

    // Extension
    size_t ei = 0;
    if (dot) {
        for (size_t i = 0; i < ext_len && ei < 3; ++i) {
            char c = dot[1 + i];
            if (c == ' ') continue;
            if (strchr(invalid, c)) return false;
            ext[ei++] = up((unsigned char)c);
        }
    }

    // Si la base tient sur 8 ou moins et ext sur 3 ou moins, remplir out11
    // Remplissage par espaces
    for (int i = 0; i < 8; ++i) out11[i] = (i < (int)bi) ? base[i] : ' ';
    for (int i = 0; i < 3; ++i) out11[8 + i] = (i < (int)ei) ? ext[i] : ' ';
    return true;
}

// Comparaison ASCII insensible à la casse pour chaînes C null-terminées
bool iequals(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (std::toupper((unsigned char)*a) != std::toupper((unsigned char)*b)) return false;
        ++a; ++b;
    }
    return *a == *b;
}

} // namespace FAT_Utils