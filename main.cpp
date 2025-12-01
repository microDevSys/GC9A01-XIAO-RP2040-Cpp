#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <vector>
#include <cstring>
#include <string>

#include "SDCard.h"
#include "StorageManager.h"
#include "TFT.h"
#include "AnimationPlayer.h"
#include "Ball.h"
#include "rgb2.h"

/*******************************************************
 * Nom du fichier : main.cpp
 * Auteur         : Guillaume Sahuc
 * Date           : 26 novembre 2025
 * Description    : test global écran + carte SD + FAT32
 *                avec interface série interactive ( via USB)
 * Version        : 1.1
 * Modifications  :
 *   - 13/11/2025 : Version initiale (release version)
 *   - 26/11/2025 : change FileInfo struct 
 *  
 * à utiliser à vos risques et périls. :)
 * nécessite des tests complémentaires pour validation complète.  
 *******************************************************/

// Objets globaux
TFT* tft = nullptr;
AnimationPlayer* anim_player = nullptr;
std::vector<Ball> balls;
static RGB2 rgb; // LED RGB (R=17, G=16, B=25)

static void wait_for_usb(uint32_t timeout_ms = 4000) {
    stdio_init_all();
    absolute_time_t until = make_timeout_time_ms(timeout_ms);
    while (!stdio_usb_connected()) {
        if (absolute_time_diff_us(get_absolute_time(), until) <= 0) break;
        sleep_ms(10);
    }
    if (stdio_usb_connected()) {
        printf("\n[USB] stdio initialisé (CDC) — hôte connecté\n");
    } else {
        printf("\n[USB] stdio initialisé (CDC) — pas d'hôte (timeout)\n");
    }
}

// Buffer pour les commandes série
static std::string cmd_buffer;
static std::string::size_type cmd_index = 0;

// Fonction pour afficher le menu d'aide
void print_help() {
    printf("\n=== COMMANDES DISPONIBLES ===\n");
    printf("  help              - Affiche ce menu\n");
    printf("  list [path]       - Liste les fichiers (défaut: racine)\n");
    printf("  bmp <file>        - Affiche une image BMP\n");
    printf("  fat32test         - Lance un test complet FAT32\n");
    printf("  format [label]    - Formate la carte en FAT32 (EFFACE TOUT!)\n");
    printf("  anim <dir>        - Lance une animation depuis un répertoire\n");
    printf("  stop              - Arrête l'animation en cours\n");
    printf("  ball [n]          - Ajoute n balles animées (défaut: 1)\n");
    printf("  clearball         - Supprime toutes les balles\n");
    printf("  text <x> <y> <texte> - Affiche du texte à la position (x,y)\n");
    printf("  clear             - Efface l'écran\n");
    printf("  info              - Affiche les infos système\n");
    printf("  rgb <r> <g> <b>   - Pilote la LED RGB (0=OFF, 1=ON)\n");
    printf("=============================\n");
}

// Helpers pour formattage FAT attributes/date
static std::string format_attr_from_fat(uint8_t attr) {
    char buf[8] = "-------"; // R H S V D A -
    if (attr & FAT_Config::AT_READONLY) buf[0] = 'R';
    if (attr & FAT_Config::AT_HIDDEN)   buf[1] = 'H';
    if (attr & FAT_Config::AT_SYSTEM)   buf[2] = 'S';
    if (attr & FAT_Config::AT_VOLUME_ID)buf[3] = 'V';
    if (attr & FAT_Config::AT_DIRECTORY) buf[4] = 'D';
    if (attr & FAT_Config::AT_ARCHIVE)   buf[5] = 'A';
    return std::string(buf);
}

static std::string format_fat_datetime_from_fields(uint16_t date, uint16_t time) {
    if (date == 0 && time == 0) return std::string("----/--/-- --:--");
    uint16_t day = date & 0x1F;
    uint16_t month = (date >> 5) & 0x0F;
    uint16_t year = ((date >> 9) & 0x7F) + 1980;
    uint16_t minutes = (time >> 5) & 0x3F;
    uint16_t hours = (time >> 11) & 0x1F;
    char buf[32];
    snprintf(buf, sizeof(buf), "%04u/%02u/%02u %02u:%02u", (unsigned)year, (unsigned)month, (unsigned)day, (unsigned)hours, (unsigned)minutes);
    return std::string(buf);
}

// Fonction pour traiter les commandes
void process_command(const char* cmd, StorageManager* storage) {
    // Copie pour tokenisation
    std::string cmd_copy(cmd);
    
    // Extraire la commande principale
    char* token = strtok(&cmd_copy[0], " ");
    if (!token) return;
    
    // Convertir en minuscules
    for (char* p = token; *p; ++p) *p = tolower(*p);
    
    // === HELP ===
    if (strcmp(token, "help") == 0) {
        print_help();
    }
    
    // === LIST ===
    else if (strcmp(token, "list") == 0) {
        const char* path = strtok(nullptr, " ");
        if (!path) path = "/";
        
        printf("\n=== Contenu de '%s' ===\n", path);
        std::vector<FileInfo> files = storage->list_directory(path);
        
        if (files.empty()) {
            printf("  (vide ou erreur)\n");
        } else {
            printf("\nType    Taille        Date/Heure        Attr     Nom\n");
            printf("----    ----------    ----------------- -------  ----\n");
            for (const auto& file : files) {
                // Copier le nom et retirer le backslash final pour les répertoires
                std::string clean_name = file.name;
                if (!clean_name.empty() && clean_name.back() == '\\') {
                    clean_name.pop_back();
                }
                // Attributes and datetime formatter
                // Use static helper functions to minimize lambda allocations

                const std::string attr_s = format_attr_from_fat(file.attributes);
                const std::string dt_s = format_fat_datetime_from_fields(file.modificationDate, file.modificationTime);

                if (file.is_directory) {
                    printf("DIR     %-12s  %s  %s  %s\n", "-", dt_s.c_str(), attr_s.c_str(), clean_name.c_str());
                } else {
                    printf("FILE    %-12u  %s  %s  %s\n", file.size, dt_s.c_str(), attr_s.c_str(), clean_name.c_str());
                }
            }
        }
        printf("=== %zu entrée(s) trouvée(s) ===\n", files.size());
    }
    
    // === BMP ===
    else if (strcmp(token, "bmp") == 0) {
        const char* filename = strtok(nullptr, " ");
        if (!filename) {
            printf("[ERREUR] Usage: bmp <fichier.bmp>\n");
            return;
        }
        
        if (!tft) {
            printf("[ERREUR] Écran TFT non initialisé\n");
            return;
        }
        
        printf("[INFO] Chargement de '%s'...\n", filename);
        
        // Callback 16 bits pour afficher les pixels (le TFT attend du RGB565)
        auto pixel_callback_565 = [](uint16_t x, uint16_t y, uint16_t color) {
            if (tft) tft->setPixel(x, y, color);
        };
        
        // Utiliser la lecture unifiée (gère BMP 16/24 bits automatiquement)
        SDCard_Status status = storage->read_bmp_file(0, 0, filename, nullptr, pixel_callback_565);
        
        if (status == SD_OK) {
            tft->sendFrame(); // Envoyer le framebuffer à l'écran
            printf("[OK] Image affichée\n");
        } else {
            printf("[ERREUR] Échec du chargement (code: %d)\n", (int)status);
        }
    }

    // === FAT32 TEST ===
    else if (strcmp(token, "fat32test") == 0) {
        printf("[INFO] Lancement du test FAT32...\n");
        SDCard_Status st = storage->run_fat32_test();
        if (st == SD_OK) {
            printf("[OK] Test FAT32 terminé avec succès\n");
        } else {
            printf("[ERREUR] Test FAT32 terminé avec des erreurs (code: %d)\n", (int)st);
        }
    }
    
    // === FORMAT ===
    else if (strcmp(token, "format") == 0) {
        const char* label = strtok(nullptr, " ");
        if (!label) label = "PICO_SD";
        
        printf("\n");
        printf("╔═══════════════════════════════════════════╗\n");
        printf("║            ⚠️  AVERTISSEMENT  ⚠️            ║\n");
        printf("╠═══════════════════════════════════════════╣\n");
        printf("║   Cette opération va EFFACER TOUTES LES   ║\n");
        printf("║   DONNÉES de la carte SD et la formater   ║\n");
        printf("║   en FAT32 avec le label: %-16s║\n", label);
        printf("║                                           ║\n");
        printf("║   Cette action est IRRÉVERSIBLE!          ║\n");
        printf("╚═══════════════════════════════════════════╝\n");
        printf("\n");
        printf("Tapez 'YES' en MAJUSCULES pour confirmer: ");
        
        // Attendre la confirmation
        char confirm[64] = {0};
        int idx = 0;
        while (true) {
            int c = getchar_timeout_us(10000000); // 10 sec timeout
            if (c == PICO_ERROR_TIMEOUT) {
                printf("\n[INFO] Timeout - Formatage annulé\n");
                return;
            }
            if (c == '\n' || c == '\r') {
                confirm[idx] = '\0';
                break;
            }
            if (c >= 32 && c < 127 && idx < 63) {
                confirm[idx++] = (char)c;
                printf("%c", c);
            }
        }
        printf("\n");
        
        if (strcmp(confirm, "YES") != 0) {
            printf("[INFO] Formatage annulé (confirmation incorrecte)\n");
            return;
        }
        
        printf("\n[INFO] Démarrage du formatage FAT32...\n");
        
        // Accès direct à la SDCard depuis le StorageManager
        SDCard* sd = storage->get_sd_card();
        if (!sd) {
            printf("[ERREUR] Impossible d'accéder à la carte SD\n");
            return;
        }
        
        // Formatage
        bool success = sd->format_fat32(label);
        
        if (success) {
            printf("\n[OK] ✓ Formatage terminé avec succès!\n");
            printf("[INFO] Vous devez redémarrer le système pour remonter la partition.\n");
        } else {
            printf("\n[ERREUR] ✗ Échec du formatage\n");
            sd->print_error_info();
        }
    }
    
    // === ANIM ===
    else if (strcmp(token, "anim") == 0) {
        const char* dirname = strtok(nullptr, " ");
        if (!dirname) {
            printf("[ERREUR] Usage: anim <répertoire>\n");
            return;
        }
        
        if (!anim_player) {
            printf("[ERREUR] AnimationPlayer non initialisé\n");
            return;
        }
        
        printf("[INFO] Chargement de l'animation '%s'...\n", dirname);
        
        // Charger l'animation en mode auto-détection
        bool success = anim_player->load_animation_auto_detect(dirname, dirname);
        
        if (success) {
            printf("[OK] Animation chargée, lecture en cours...\n");
            anim_player->play_animation(dirname);
        } else {
            printf("[ERREUR] Échec du chargement de l'animation\n");
        }
    }
    
    // === STOP ===
    else if (strcmp(token, "stop") == 0) {
        if (anim_player) {
            anim_player->stop();
            printf("[INFO] Animation arrêtée\n");
        } else {
            printf("[INFO] Aucune animation en cours\n");
        }
    }
    
    // === BALL ===
    else if (strcmp(token, "ball") == 0) {
        const char* count_str = strtok(nullptr, " ");
        int count = count_str ? atoi(count_str) : 1;
        
        if (count < 1 || count > 100) {
            printf("[ERREUR] Nombre de balles invalide (1-100)\n");
            return;
        }
        
        if (!tft) {
            printf("[ERREUR] Écran TFT non initialisé\n");
            return;
        }
        
        for (int i = 0; i < count; i++) {
            balls.emplace_back(TFTConfig::WIDTH, TFTConfig::HEIGHT);
        }
        
        printf("[INFO] %d balle(s) ajoutée(s) (total: %zu)\n", count, balls.size());
    }
    
    // === CLEARBALL ===
    else if (strcmp(token, "clearball") == 0) {
        balls.clear();
        printf("[INFO] Toutes les balles ont été supprimées\n");
    }
    
    // === TEXT ===
    else if (strcmp(token, "text") == 0) {
        const char* x_str = strtok(nullptr, " ");
        const char* y_str = strtok(nullptr, " ");
        const char* text = strtok(nullptr, ""); // Récupère le reste de la ligne
        
        if (!x_str || !y_str || !text) {
            printf("[ERREUR] Usage: text <x> <y> <texte>\n");
            return;
        }
        
        if (!tft) {
            printf("[ERREUR] Écran TFT non initialisé\n");
            return;
        }
        
        int x = atoi(x_str);
        int y = atoi(y_str);
        
        // Supprimer l'espace initial si présent
        while (*text == ' ') text++;
        
        tft->drawText(x, y, text, 0xFFFF); // Blanc
        tft->sendFrame();
        printf("[INFO] Texte affiché à (%d, %d): \"%s\"\n", x, y, text);
    }
    
    // === CLEAR ===
    else if (strcmp(token, "clear") == 0) {
        if (tft) {
            tft->clear();
            balls.clear(); // Supprimer toutes les balles
            printf("[INFO] Écran effacé et balles supprimées\n");
        } else {
            printf("[ERREUR] Écran TFT non initialisé\n");
        }
    }
    
    // === INFO ===
    else if (strcmp(token, "info") == 0) {
        printf("\n=== INFORMATIONS SYSTÈME ===\n");
        if (storage->is_fat32_mounted()) {
            printf("  Carte SD: Montée (FAT32)\n");
            storage->display_fat32_system_info();
        } else {
            printf("  Carte SD: Non montée\n");
        }
        if (tft) {
            printf("  Écran TFT: Initialisé (%dx%d)\n", TFTConfig::WIDTH, TFTConfig::HEIGHT);
        } else {
            printf("  Écran TFT: Non initialisé\n");
        }
        printf("===========================\n");
    }

    // === RGB ===
    else if (strcmp(token, "rgb") == 0) {
        const char* r_str = strtok(nullptr, " ");
        const char* g_str = strtok(nullptr, " ");
        const char* b_str = strtok(nullptr, " ");
        if (!r_str || !g_str || !b_str) {
            printf("[ERREUR] Usage: rgb <r> <g> <b> (0/1)\n");
            return;
        }
        int r = atoi(r_str); r = r ? 1 : 0;
        int g = atoi(g_str); g = g ? 1 : 0;
        int b = atoi(b_str); b = b ? 1 : 0;
        rgb.set(r != 0, g != 0, b != 0);
        printf("[INFO] LED RGB => R:%d G:%d B:%d\n", r, g, b);
    }
    
    // === COMMANDE INCONNUE ===
    else {
        printf("[ERREUR] Commande inconnue: '%s'\n", token);
        printf("Tapez 'help' pour voir les commandes disponibles.\n");
    }
}

// Fonction pour lire et traiter les commandes série
void handle_serial_input(StorageManager* storage) {
    int c = getchar_timeout_us(0); // Non bloquant
    
    if (c == PICO_ERROR_TIMEOUT) return;
    
    if (c == '\n' || c == '\r') {
        if (cmd_index > 0) {
            printf("\n"); // Nouvelle ligne
            
            // Traiter la commande
            process_command(cmd_buffer.c_str(), storage);
            
            // Réinitialiser le buffer
            cmd_buffer.clear();
            cmd_index = 0;
            printf("\n> "); // Prompt
        }
    } else if (c == 127 || c == 8) { // Backspace
        if (cmd_index > 0) {
            cmd_index--;
            printf("\b \b"); // Effacer le caractère à l'écran
        }
    } else if (c >= 32 && c < 127) { // Caractères imprimables
        cmd_buffer += (char)c;
        cmd_index++;
        printf("%c", c); // Echo
    }
}

int main() {
    wait_for_usb();

    printf("\n=== SYSTÈME DE COMMANDES INTERACTIF ===\n");
    printf("Tapez 'help' pour voir les commandes disponibles.\n");

    // Initialiser la carte SD
    SDCard sd;
    if (!sd.init()) {
        SDCard_Status st = sd.get_last_status();
        printf("[ERREUR] Initialisation SD échouée : %s (code: %d)\n", sd.get_error_message(st), (int)st);
        return -1;
    }
    printf("[OK] SD initialisée\n");

    StorageManager storage(&sd);
    if (!storage.mount_fat32()) {
        printf("[ERREUR] Montage FAT32 échoué\n");
        return -2;
    }
    printf("[OK] FAT32 monté\n");

    // Initialiser l'écran TFT (utilise la configuration dans main.h)
    tft = new TFT();
    tft->init();
    tft->clear();
    printf("[OK] TFT initialisé (%dx%d)\n", TFTConfig::WIDTH, TFTConfig::HEIGHT);

    
    // Initialiser l'AnimationPlayer
    anim_player = new AnimationPlayer(&storage, tft);
    printf("[OK] AnimationPlayer initialisé\n");

    printf("\n> "); // Premier prompt
    // Boucle principale
    cmd_buffer.reserve(128);
    while (true) {
        // Gérer les entrées série
        handle_serial_input(&storage);
        
        // Mettre à jour et afficher les balles
        if (!balls.empty() && tft) {
            for (auto& ball : balls) {
                // Effacer l'ancienne position
                tft->drawFillCircle((int)ball.x, (int)ball.y, ball.radius, COLOR_16BITS_BLACK);

                // Mettre à jour la position
                ball.update(TFTConfig::WIDTH, TFTConfig::HEIGHT);

                // Dessiner à la nouvelle position
                tft->drawFillCircle((int)ball.x, (int)ball.y, ball.radius, ball.color);
            }
            
            // Envoyer la frame complète (restauré comportement original)
            tft->sendFrame();
        }
        
        // Mettre à jour l'animation si elle est en cours
        if (anim_player) {
            anim_player->update();
        }
        
        sleep_ms(1); // Petite pause pour ne pas saturer le CPU
    }
    
    return 0;
}
