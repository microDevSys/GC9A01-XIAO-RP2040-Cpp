#include "AnimationPlayer.h"
#include "FAT32.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>

/*******************************************************
 * Nom du fichier : AnimationPlayer.cpp
 * Auteur         : Guillaume Sahuc
 * Date           : 13 novembre 2025
 * Description    : implémentation d'affichage suscessif de fichier raw (16bits color)
 *******************************************************/

AnimationPlayer::AnimationPlayer(StorageManager* storage, TFT* tft) 
    : storage_manager(storage), tft_display(tft), current_animation_index(-1), 
      current_frame_index(0), last_frame_time(0), performance_mode(0),
      next_frame_cache(nullptr), next_frame_cache_size(0), next_frame_cached_index(-1) {
}

AnimationPlayer::~AnimationPlayer() {
    // Nettoyer toutes les animations
    for (auto anim : animations) {
        delete anim;
    }
    animations.clear();
    
    // Libérer le cache de frame
    if (next_frame_cache) {
        delete[] next_frame_cache;
        next_frame_cache = nullptr;
    }
}

bool AnimationPlayer::load_animation(const char* directory_path, const char* name) {
    if (!directory_path) {
        return false;
    }

    if (strstr(directory_path, ".raw") != nullptr) {
        return false;
    }
    
    // Limite le nombre total d'animations pour éviter les dépassements mémoire
    const size_t MAX_ANIMATIONS = 10;
    if (animations.size() >= MAX_ANIMATIONS) {
        printf("Warning: Limite de %zu animations atteinte. Utilisez clear_all_animations() d'abord.\n", MAX_ANIMATIONS);
        return false;
    }
    
    printf("Chargement animation depuis: %s\n", directory_path);

    Animation* anim = new Animation();
    anim->name = name ? name : directory_path;

    if (!storage_manager || !storage_manager->is_fat32_mounted()) {
        // Frames de test
        const uint16_t colors[] = {COLOR_16BITS_RED, COLOR_16BITS_GREEN, COLOR_16BITS_BLUE, COLOR_16BITS_YELLOW};
        const int num_test_frames = 4;
        for (int i = 0; i < num_test_frames; i++) {
            AnimationFrame* frame = new AnimationFrame();
            frame->size = TFTConfig::WIDTH * TFTConfig::HEIGHT * TFTConfig::BYTES_PER_PIXEL;
            frame->delay_ms = 50;
            frame->data = new uint8_t[frame->size];
            if (!frame->data) { delete frame; delete anim; return false; }
            uint16_t* pixels = (uint16_t*)frame->data;
            for (int j = 0; j < TFTConfig::WIDTH * TFTConfig::HEIGHT; j++) pixels[j] = colors[i];
            anim->frames.push_back(frame);
        }
        animations.push_back(anim);
        return true;
    }

    printf("Mode génération automatique de noms de fichiers (FR_XXX.RAW)\n");
    
    std::vector<std::string> raw_names;
    const size_t MAX_ANIMATION_FILES = 10; // Limite ultra-conservative
    
    // Générer les noms FR_000.RAW à FR_009.RAW (ou moins selon la limite)
    for (size_t i = 0; i < MAX_ANIMATION_FILES; i++) {
        char filename[16];
        snprintf(filename, sizeof(filename), "FR_%03zu.RAW", i);
        raw_names.push_back(std::string(filename));
        printf("Ajouté: %s\n", filename);
    }
    
    printf("Générés %zu noms de fichiers\n", raw_names.size());
    
    if (raw_names.empty()) {
        delete anim;
        return false;
    }
    // Pas besoin de trier, déjà en ordre

    // Conserver les chemins complets pour lecture à la volée
    const uint32_t frame_size = TFTConfig::WIDTH * TFTConfig::HEIGHT * TFTConfig::BYTES_PER_PIXEL;
    anim->stream_from_dir_files = true;
    anim->frame_size_bytes = frame_size;
    anim->frame_paths.clear();
    
    // Réserver la mémoire à l'avance pour éviter les réallocations
    anim->frame_paths.reserve(raw_names.size());
    printf("Mémoire réservée pour %zu chemins de fichiers\n", raw_names.size());
    
    for (const auto& n : raw_names) {
        std::string full_path = std::string(directory_path) + "/" + n;
        anim->frame_paths.push_back(full_path);
        
        // Vérification simple de la capacité pour éviter les problèmes de mémoire
        if (anim->frame_paths.size() > anim->frame_paths.capacity()) {
            printf("ERREUR: Problème d'allocation mémoire pour %zu chemins\n", raw_names.size());
            delete anim;
            return false;
        }
    }
    
    anim->num_frames_stream = static_cast<uint32_t>(anim->frame_paths.size());
    printf("Animation créée avec %lu frames en streaming\n", anim->num_frames_stream);
    
    if (anim->num_frames_stream == 0) { 
        delete anim; 
        return false; 
    }

    animations.push_back(anim);
    return true;
}

// La logique DirectoryFrames est désormais directement dans load_animation
bool AnimationPlayer::play_animation(int animation_index) {
    if (animation_index < 0 || animation_index >= static_cast<int>(animations.size())) {
        return false;
    }
    
    current_animation_index = animation_index;
    current_frame_index = 0;
    last_frame_time = to_ms_since_boot(get_absolute_time());
    
    return true;
}

bool AnimationPlayer::play_animation(const char* animation_name) {
    for (int i = 0; i < static_cast<int>(animations.size()); i++) {
        if (animations[i]->name == animation_name) {
            return play_animation(i);
        }
    }
    
    return false;
}

void AnimationPlayer::update() {
    if (current_animation_index < 0 || current_animation_index >= static_cast<int>(animations.size())) {
        return;
    }
    
    Animation* anim = animations[current_animation_index];
    // Déterminer le nombre total de frames
    int total_frames = !anim->frames.empty() ? static_cast<int>(anim->frames.size())
                                             : static_cast<int>(anim->num_frames_stream);
    if (total_frames <= 0) return;

    // Délai de frame: utiliser celui de la frame en RAM si dispo, sinon selon mode de performance
    AnimationFrame* current_frame = (!anim->frames.empty()) ? anim->frames[current_frame_index] : nullptr;
    uint16_t default_delays[] = {33, 16, 8}; // 30fps, 60fps, 120fps
    uint16_t delay_ms = current_frame ? current_frame->delay_ms : default_delays[performance_mode];

    // Vérifier si il est temps de passer à la frame suivante
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    if (current_time - last_frame_time >= delay_ms) {
        // Affichage soit depuis RAM (si frames chargées), soit en streaming
        bool shown = false;
        if (tft_display) {
            if (!anim->frames.empty() && current_frame->data) {
                tft_display->blitRGB565FullFrame(current_frame->data);
                tft_display->sendFrame();
                shown = true;
            } else if (anim->stream_from_dir_files) {
                // Lire le fichier correspondant à l'index courant dans read_buffer
                if (current_frame_index >= 0 && current_frame_index < static_cast<int>(anim->num_frames_stream)) {
                    const char* path = anim->frame_paths[current_frame_index].c_str();
                    if (read_frame_from_file(path, anim->frame_size_bytes)) {
                        // Données déjà copiées dans le framebuffer
                        tft_display->sendFrame();
                        shown = true;
                    }
                }
            } else if (anim->stream_generated_names) {
                // Mode ultra-économe : générer le nom à la volée (optimisé)
                if (current_frame_index >= 0 && current_frame_index < static_cast<int>(anim->num_frames_stream)) {
                    static char full_path_buffer[256]; // Buffer statique pour éviter les allocations
                    snprintf(full_path_buffer, sizeof(full_path_buffer), 
                            "%s/FR_%03d.RAW", anim->base_directory.c_str(), current_frame_index);
                    
                    if (read_frame_from_file(full_path_buffer, anim->frame_size_bytes)) {
                        tft_display->sendFrame();
                        shown = true;
                    }
                }
            }
        }

        // Passer à la frame suivante (même si non shown, on évite blocage)
        current_frame_index++;
        
        // Gestion spéciale pour les animations par blocs
        if (anim->stream_by_blocks) {
            update_block_animation(anim);
            // total_frames sera mis à jour par load_next_block si nécessaire
            total_frames = static_cast<int>(anim->num_frames_stream);
        } else {
            // Gestion normale pour les autres types d'animations
            if (total_frames <= 0) total_frames = 1;
            if (current_frame_index >= total_frames) {
                if (anim->loop) current_frame_index = 0; else current_frame_index = total_frames - 1;
            }
        }

        last_frame_time = current_time;
        (void)shown;
    }
}

void AnimationPlayer::stop() {
    current_animation_index = -1;
    current_frame_index = 0;
}

void AnimationPlayer::pause() {
    // Pour pauser, on peut juste arrêter de mettre à jour last_frame_time
    // L'implémentation dépend des besoins exacts
}

void AnimationPlayer::resume() {
    last_frame_time = to_ms_since_boot(get_absolute_time());
}

void AnimationPlayer::next_animation() {
    if (animations.empty()) return;
    
    int next_index = current_animation_index + 1;
    if (next_index >= static_cast<int>(animations.size())) {
        next_index = 0; // Boucler au début
    }
    play_animation(next_index);
}

void AnimationPlayer::previous_animation() {
    if (animations.empty()) return;
    
    int prev_index = current_animation_index - 1;
    if (prev_index < 0) {
        prev_index = static_cast<int>(animations.size()) - 1; // Aller à la fin
    }
    play_animation(prev_index);
}

void AnimationPlayer::next_frame() {
    if (current_animation_index < 0 || current_animation_index >= static_cast<int>(animations.size())) {
        return;
    }
    
    Animation* anim = animations[current_animation_index];
    current_frame_index++;
    if (current_frame_index >= static_cast<int>(anim->frames.size())) {
        current_frame_index = 0;
    }
    last_frame_time = to_ms_since_boot(get_absolute_time());
}

void AnimationPlayer::previous_frame() {
    if (current_animation_index < 0 || current_animation_index >= static_cast<int>(animations.size())) {
        return;
    }
    
    Animation* anim = animations[current_animation_index];
    current_frame_index--;
    if (current_frame_index < 0) {
        current_frame_index = static_cast<int>(anim->frames.size()) - 1;
    }
    last_frame_time = to_ms_since_boot(get_absolute_time());
}

void AnimationPlayer::set_loop(bool loop_enabled) {
    if (current_animation_index >= 0 && current_animation_index < static_cast<int>(animations.size())) {
        animations[current_animation_index]->loop = loop_enabled;
    }
}

void AnimationPlayer::set_frame_delay(uint16_t delay_ms) {
    if (current_animation_index >= 0 && current_animation_index < static_cast<int>(animations.size())) {
        Animation* anim = animations[current_animation_index];
        for (auto frame : anim->frames) {
            frame->delay_ms = delay_ms;
        }
    }
}

const char* AnimationPlayer::get_current_animation_name() const {
    if (current_animation_index >= 0 && current_animation_index < static_cast<int>(animations.size())) {
        return animations[current_animation_index]->name.c_str();
    }
    return nullptr;
}

void AnimationPlayer::list_animations() const {
    printf("=== Animations chargées ===\n");
    for (int i = 0; i < static_cast<int>(animations.size()); i++) {
        const Animation* a = animations[i];
        size_t cnt = !a->frames.empty() ? a->frames.size() : a->num_frames_stream;
        const char* mode = a->stream_by_blocks ? "blocks" : 
                          (a->stream_generated_names ? "generated" :
                          (a->stream_from_dir_files ? "stream(dir)" : "mem"));
        
        if (a->stream_by_blocks) {
            printf("%d: '%s' (%zu/%lu frames) [%s] bloc:%lu-%lu\n", 
                   i, a->name.c_str(), cnt, a->total_files_available, mode,
                   a->current_block_start, a->current_block_start + cnt - 1);
        } else {
            printf("%d: '%s' (%zu frames) [%s]\n", i, a->name.c_str(), cnt, mode);
        }
    }
    printf("=========================\n");
}

void AnimationPlayer::clear_all_animations() {
    printf("Nettoyage de %zu animations...\n", animations.size());
    
    // Arrêter la lecture en cours
    stop();
    
    // Libérer toutes les animations
    for (auto anim : animations) {
        delete anim;
    }
    animations.clear();
    
    printf("Mémoire libérée\n");
}

void AnimationPlayer::set_performance_mode(int mode) {
    if (mode >= 0 && mode <= 2) {
        performance_mode = mode;
        const char* mode_names[] = {"Normal (30fps)", "Rapide (60fps)", "Ultra (120fps)"};
        printf("Mode de performance: %s\n", mode_names[mode]);
    } else {
        printf("Mode invalide. Utiliser 0=normal, 1=rapide, 2=ultra\n");
    }
}

void AnimationPlayer::optimize_block_size_for_performance() {
    for (auto anim : animations) {
        if (anim->stream_by_blocks) {
            // Ajuster la taille des blocs selon le mode de performance
            uint32_t optimal_sizes[] = {5, 15, 25}; // Plus gros blocs = moins de transitions
            uint32_t new_size = optimal_sizes[performance_mode];
            
            if (anim->block_size != new_size) {
                anim->block_size = new_size;
                printf("Taille de bloc optimisée: %lu pour mode %d\n", new_size, performance_mode);
            }
        }
    }
}

void AnimationPlayer::measure_performance(int frames_to_measure) {
    if (current_animation_index < 0) {
        printf("Aucune animation en cours pour mesurer les performances\n");
        return;
    }
    
    printf("Début mesure performance sur %d frames...\n", frames_to_measure);
    
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    int initial_frame = current_frame_index;
    int frames_counted = 0;
    
    // Mesurer pendant frames_to_measure frames ou 5 secondes max
    while (frames_counted < frames_to_measure) {
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        
        // Timeout de sécurité
        if (current_time - start_time > 5000) {
            printf("Timeout atteint lors de la mesure\n");
            break;
        }
        
        int old_frame = current_frame_index;
        update(); // Une frame
        
        // Compter seulement si la frame a changé
        if (current_frame_index != old_frame) {
            frames_counted++;
        }
        
        sleep_ms(1); // Éviter la boucle infinie
    }
    
    uint32_t end_time = to_ms_since_boot(get_absolute_time());
    uint32_t elapsed_ms = end_time - start_time;
    
    float fps = (frames_counted * 1000.0f) / elapsed_ms;
    
    printf("=== RÉSULTATS PERFORMANCE ===\n");
    printf("Frames mesurées: %d\n", frames_counted);
    printf("Temps écoulé: %lu ms\n", elapsed_ms);
    printf("FPS réels: %.2f\n", fps);
    printf("Mode performance: %d\n", performance_mode);
    printf("==============================\n");
}

void AnimationPlayer::check_memory_usage() const {
    size_t total_paths = 0;
    size_t total_frames = 0;
    
    for (const auto* anim : animations) {
        total_paths += anim->frame_paths.size();
        total_frames += anim->frames.size();
    }
    
    // Estimation approximative de l'usage mémoire
    size_t estimated_paths_memory = total_paths * 50; // ~50 bytes par chemin en moyenne
    size_t estimated_frames_memory = total_frames * (240 * 240 * 2); // Si frames en RAM
    
    printf("=== Usage Mémoire Estimé ===\n");
    printf("Animations: %zu\n", animations.size());
    printf("Chemins stockés: %zu (~%zu bytes)\n", total_paths, estimated_paths_memory);
    printf("Frames en RAM: %zu (~%zu bytes)\n", total_frames, estimated_frames_memory);
    printf("============================\n");
}

bool AnimationPlayer::load_animation_safe(const char* directory_path, const char* name, size_t max_files) {
    if (!directory_path || max_files == 0) {
        return false;
    }
    
    printf("Chargement sécurisé: max %zu fichiers depuis %s\n", max_files, directory_path);
    
    Animation* anim = new Animation();
    if (!anim) return false;
    
    anim->name = name ? name : directory_path;
    anim->stream_from_dir_files = true;
    anim->frame_size_bytes = TFTConfig::WIDTH * TFTConfig::HEIGHT * TFTConfig::BYTES_PER_PIXEL;
    
    // Réserver la mémoire à l'avance
    anim->frame_paths.reserve(max_files);
    
    // Vérifier que la réservation a fonctionné
    if (anim->frame_paths.capacity() < max_files) {
        printf("ERREUR: Impossible de réserver mémoire pour %zu chemins\n", max_files);
        delete anim;
        return false;
    }
    
    // Générer les chemins sans lister le répertoire complet
    size_t loaded_count = 0;
    for (size_t i = 0; i < max_files && loaded_count < max_files; i++) {
        char filename[32];
        snprintf(filename, sizeof(filename), "FR_%03zu.RAW", i);
        
        std::string full_path = std::string(directory_path) + "/" + filename;
        anim->frame_paths.push_back(full_path);
        loaded_count++;
        
        if (loaded_count % 5 == 0) {
            printf("Préparé %zu fichiers...\n", loaded_count);
        }
    }
    
    anim->num_frames_stream = loaded_count;
    animations.push_back(anim);
    
    printf("Animation sécurisée créée: %zu frames\n", loaded_count);
    return true;
}

bool AnimationPlayer::load_animation_generated(const char* directory_path, const char* name, size_t frame_count) {
    if (!directory_path || frame_count == 0) {
        return false;
    }
    
    printf("Mode ultra-économe: %zu frames générées à la volée depuis %s\n", frame_count, directory_path);
    
    Animation* anim = new Animation();
    if (!anim) return false;
    
    anim->name = name ? name : directory_path;
    anim->stream_from_dir_files = false;  // Pas de liste de chemins
    anim->stream_generated_names = true;   // Génération à la volée
    anim->base_directory = directory_path;
    anim->num_frames_stream = frame_count;
    anim->frame_size_bytes = TFTConfig::WIDTH * TFTConfig::HEIGHT * TFTConfig::BYTES_PER_PIXEL;
    
    // AUCUNE allocation de vecteur de chemins !
    animations.push_back(anim);
    
    printf("Animation ultra-économe créée: %zu frames (0 bytes pour les chemins)\n", frame_count);
    return true;
}

bool AnimationPlayer::load_animation_by_blocks(const char* directory_path, const char* name, size_t total_files, size_t block_size) {
    if (!directory_path || total_files == 0 || block_size == 0) {
        return false;
    }
    
    printf("Mode par blocs: %zu fichiers totaux, blocs de %zu depuis %s\n", total_files, block_size, directory_path);
    
    Animation* anim = new Animation();
    if (!anim) return false;
    
    anim->name = name ? name : directory_path;
    anim->stream_by_blocks = true;
    anim->base_directory = directory_path;
    anim->total_files_available = total_files;
    anim->block_size = block_size;
    anim->current_block_start = 0;
    anim->frame_size_bytes = TFTConfig::WIDTH * TFTConfig::HEIGHT * TFTConfig::BYTES_PER_PIXEL;
    
    // Charger le premier bloc
    if (!load_next_block(anim)) {
        delete anim;
        return false;
    }
    
    animations.push_back(anim);
    printf("Animation par blocs créée: %zu fichiers totaux, bloc actuel: %lu-%lu\n", 
           total_files, anim->current_block_start, 
           anim->current_block_start + anim->frame_paths.size() - 1);
    return true;
}

bool AnimationPlayer::read_frame_from_file(const char* full_path, uint32_t frame_size, int offset_x, int offset_y) {
    if (!storage_manager || !storage_manager->is_fat32_mounted() || !full_path) {
        return false;
    }

    // Obtenir l'accès au système FAT32 via StorageManager
    FAT32* fs = storage_manager->get_fat32_fs();
    if (!fs || !fs->is_initialized()) {
        return false;
    }

    // Split directory and base
    std::string path(full_path);
    std::string dir = "/"; std::string base = path;
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        dir = path.substr(0, slash); if (dir.empty()) dir = "/";
        base = path.substr(slash + 1);
    }
    if (!fs->change_directory(dir.c_str())) {
        return false;
    }
    
    FAT_ErrorCode r = fs->file_open(base.c_str(), READ);
    if (r != FILE_FOUND) {
        fs->change_directory("/");
        return false;
    }

    // Lire l'entête (4 octets: width + height en little-endian)
    uint8_t header[4];
    uint32_t header_read = 0;
    ReadHandler h{};
    uint32_t chunk_size = 0;
    uint8_t tmp[512];
    
    // Lire l'entête d'abord
    while (header_read < 4) {
        chunk_size = fs->file_read(tmp, &h);
        if (chunk_size == 0) {
            fs->file_close();
            fs->change_directory("/");
            return false; // Impossible de lire l'entête
        }
        
        uint32_t need = 4 - header_read;
        uint32_t to_copy = (chunk_size > need) ? need : chunk_size;
        memcpy(header + header_read, tmp, to_copy);
        header_read += to_copy;
        
        // S'il reste des données dans tmp après l'entête, les décaler
        if (to_copy < chunk_size) {
            memmove(tmp, tmp + to_copy, chunk_size - to_copy);
            chunk_size -= to_copy;
        } else {
            chunk_size = 0;
        }
    }
    
    // Extraire largeur et hauteur (little-endian)
    uint16_t width = header[0] | (header[1] << 8);
    uint16_t height = header[2] | (header[3] << 8);
    
    // Vérifier que les dimensions sont raisonnables
    if (width == 0 || height == 0 || width > 1024 || height > 1024) {
        printf("Erreur: Dimensions invalides ou trop grandes: %dx%d\n", width, height);
        fs->file_close();
        fs->change_directory("/");
        return false; // Dimensions invalides
    }
    
    // Vérifier que la taille calculée correspond à la taille du fichier
    uint32_t expected_file_size = 4 + (width * height * 2); // 4 bytes header + pixel data
    uint8_t* fb = tft_display ? tft_display->getFramebuffer() : nullptr;
    uint32_t fb_size = tft_display ? tft_display->getFramebufferSize() : 0;
    
    if (!fb || fb_size == 0) {
        fs->file_close();
        fs->change_directory("/");
        return false;
    }
    
    // Obtenir les dimensions du framebuffer TFT
    uint16_t fb_width = TFTConfig::WIDTH;
    uint16_t fb_height = TFTConfig::HEIGHT;
    
    // Si pas d'offset spécifié et que l'image est plus petite, calculer l'offset pour centrer
    if (offset_x == 0 && offset_y == 0 && (width != fb_width || height != fb_height)) {
        if (width < fb_width) {
            offset_x = (fb_width - width) / 2;
        }
        if (height < fb_height) {
            offset_y = (fb_height - height) / 2;
        }
    }
    
    // Calculer la taille des données de pixels (2 octets par pixel RGB565)
    uint32_t pixel_data_size = width * height * 2;
    
    // Cas spécial : si les dimensions semblent incorrectes mais la taille correspond à l'écran
    uint32_t expected_screen_pixels = fb_width * fb_height;
    if (pixel_data_size == expected_screen_pixels * 2 && (width != fb_width || height != fb_height)) {
        printf("Warning: Dimensions %dx%d ne correspondent pas à l'écran %dx%d mais taille correcte, utilisation directe\n", 
               width, height, fb_width, fb_height);
        width = fb_width;
        height = fb_height;
    }
    
    // Si les dimensions correspondent exactement et pas d'offset, copie directe
    if (width == fb_width && height == fb_height && offset_x == 0 && offset_y == 0) {
        uint32_t max_read = (pixel_data_size < fb_size) ? pixel_data_size : fb_size;
        uint32_t copied = 0;
        
        // Copier d'abord les données restantes de tmp si il y en a
        if (chunk_size > 0) {
            uint32_t to_copy = (chunk_size > max_read) ? max_read : chunk_size;
            memcpy(fb, tmp, to_copy);
            copied += to_copy;
        }
        
        // Continuer la lecture des données de pixels
        while (copied < max_read) {
            chunk_size = fs->file_read(tmp, &h);
            if (chunk_size == 0) {
                break; // Fin de fichier
            }
            
            uint32_t need = max_read - copied;
            uint32_t to_copy = (chunk_size > need) ? need : chunk_size;
            memcpy(fb + copied, tmp, to_copy);
            copied += to_copy;
        }
        
        // Si le fichier est plus petit que prévu, remplir le reste avec du noir
        if (copied < max_read) {
            memset(fb + copied, 0, max_read - copied);
        }
    } else {
        // Dimensions différentes ou avec offset : copie ligne par ligne avec gestion des retours à la ligne
        // Calculer la zone de destination dans le framebuffer
        int dest_start_x = offset_x;
        int dest_start_y = offset_y;
        int dest_end_x = dest_start_x + width;
        int dest_end_y = dest_start_y + height;
        
        // Clipping : s'assurer qu'on reste dans les limites du framebuffer
        if (dest_start_x >= fb_width || dest_start_y >= fb_height || 
            dest_end_x <= 0 || dest_end_y <= 0) {
            // Image complètement hors écran
            fs->file_close();
            fs->change_directory("/");
            return true; // Pas d'erreur, juste rien à afficher
        }
        
        // Calculer les zones de clipping
        int src_start_x = (dest_start_x < 0) ? -dest_start_x : 0;
        int src_start_y = (dest_start_y < 0) ? -dest_start_y : 0;
        int clip_dest_start_x = (dest_start_x < 0) ? 0 : dest_start_x;
        int clip_dest_start_y = (dest_start_y < 0) ? 0 : dest_start_y;
        int clip_dest_end_x = (dest_end_x > fb_width) ? fb_width : dest_end_x;
        int clip_dest_end_y = (dest_end_y > fb_height) ? fb_height : dest_end_y;
        
        int copy_width = clip_dest_end_x - clip_dest_start_x;
        int copy_height = clip_dest_end_y - clip_dest_start_y;
        
        if (copy_width <= 0 || copy_height <= 0) {
            fs->file_close();
            fs->change_directory("/");
            return true; // Rien à copier après clipping
        }
        
        // Buffer temporaire pour une ligne de l'image source
        // Limiter la taille du buffer pour éviter les dépassements mémoire
        uint32_t max_line_pixels = 1024; // Limite raisonnable pour un microcontrôleur
        if (width > max_line_pixels) {
            printf("Erreur: Image trop large (%d pixels > %lu max)\n", width, max_line_pixels);
            fs->file_close();
            fs->change_directory("/");
            return false;
        }
        
        uint16_t* line_buffer = new uint16_t[width]; // Buffer pour une ligne complète de l'image source
        if (!line_buffer) {
            printf("Erreur: Impossible d'allouer %d bytes pour line_buffer\n", width * 2);
            fs->file_close();
            fs->change_directory("/");
            return false;
        }
        
        uint32_t remaining_in_tmp = chunk_size;
        uint32_t tmp_offset = 0;
        
        // Nettoyer le framebuffer pour une nouvelle image (sauf si c'est une superposition intentionnelle)
        // On nettoie si l'image ne couvre pas exactement tout l'écran
        if (!(offset_x == 0 && offset_y == 0 && width == fb_width && height == fb_height)) {
            memset(fb, 0, fb_size);
        }
        
        for (uint16_t y = 0; y < height; y++) {
            uint32_t line_bytes_needed = width * 2; // Octets pour une ligne complète
            uint32_t line_bytes_read = 0;
            
            // Lire une ligne complète de l'image source
            while (line_bytes_read < line_bytes_needed) {
                // Si plus de données dans tmp, lire le prochain chunk
                if (remaining_in_tmp == 0) {
                    chunk_size = fs->file_read(tmp, &h);
                    if (chunk_size == 0) break; // Fin de fichier
                    remaining_in_tmp = chunk_size;
                    tmp_offset = 0;
                }
                
                uint32_t need = line_bytes_needed - line_bytes_read;
                uint32_t available = remaining_in_tmp;
                uint32_t to_copy = (available > need) ? need : available;
                
                memcpy((uint8_t*)line_buffer + line_bytes_read, tmp + tmp_offset, to_copy);
                line_bytes_read += to_copy;
                tmp_offset += to_copy;
                remaining_in_tmp -= to_copy;
            }
            
            // Si on n'a pas pu lire une ligne complète, arrêter
            if (line_bytes_read < line_bytes_needed) break;
            
            // Vérifier si cette ligne doit être copiée (clipping vertical)
            int dest_y = dest_start_y + y;
            if (dest_y >= clip_dest_start_y && dest_y < clip_dest_end_y) {
                // Copier la portion de ligne qui rentre dans le framebuffer
                uint16_t* fb_line = (uint16_t*)fb + (dest_y * fb_width);
                for (int x = 0; x < copy_width; x++) {
                    int src_x = src_start_x + x;
                    int dest_x = clip_dest_start_x + x;
                    if (src_x < width) {
                        fb_line[dest_x] = line_buffer[src_x];
                    }
                }
            }
        }
        
        delete[] line_buffer;
    }
    
    fs->file_close();
    fs->change_directory("/");
    
    // Considérer comme succès si on a traité l'image
    return true;
}

bool AnimationPlayer::load_next_block(Animation* anim) {
    if (!anim || !anim->stream_by_blocks) {
        return false;
    }
    
    // Nettoyer le bloc précédent
    anim->frame_paths.clear();
    
    // Calculer la plage du bloc actuel
    uint32_t block_start = anim->current_block_start;
    uint32_t block_end = block_start + anim->block_size;
    if (block_end > anim->total_files_available) {
        block_end = anim->total_files_available;
    }
    
    printf("Chargement bloc %lu-%lu...\n", block_start, block_end - 1);
    
    // Réserver la mémoire pour ce bloc
    size_t block_size_needed = block_end - block_start;
    size_t current_capacity = anim->frame_paths.capacity();
    
    // Vérifier si on a besoin de plus de mémoire
    if (current_capacity < block_size_needed) {
        anim->frame_paths.reserve(block_size_needed);
        
        // Vérifier si la réservation a réussi
        if (anim->frame_paths.capacity() < block_size_needed) {
            printf("ERREUR: Impossible de réserver mémoire pour le bloc\n");
            return false;
        }
    }
    
    // Générer les chemins pour ce bloc
    for (uint32_t i = block_start; i < block_end; i++) {
        char filename[32];
        snprintf(filename, sizeof(filename), "FR_%03u.RAW", i);
        
        std::string full_path = anim->base_directory + "/" + filename;
        anim->frame_paths.push_back(full_path);
    }
    
    anim->num_frames_stream = anim->frame_paths.size();
    anim->stream_from_dir_files = true; // Utiliser le système de lecture existant
    
    printf("Bloc chargé: %lu frames (%lu-%lu)\n", 
           anim->num_frames_stream, block_start, block_end - 1);
    return true;
}

void AnimationPlayer::update_block_animation(Animation* anim) {
    if (!anim || !anim->stream_by_blocks) {
        return;
    }
    
    // Vérifier si on est arrivé à la fin du bloc actuel
    if (current_frame_index >= static_cast<int>(anim->num_frames_stream)) {
        // Passer au bloc suivant
        anim->current_block_start += anim->block_size;
        
        // Si on dépasse le total, revenir au début (boucle complète)
        if (anim->current_block_start >= anim->total_files_available) {
            printf("Fin de l'animation complète, retour au début\n");
            anim->current_block_start = 0;
        }
        
        // Charger le nouveau bloc
        if (load_next_block(anim)) {
            current_frame_index = 0; // Repartir au début du nouveau bloc
            printf("Transition vers bloc %lu\n", anim->current_block_start);
        } else {
            printf("Erreur lors du chargement du bloc suivant\n");
            current_frame_index = static_cast<int>(anim->num_frames_stream) - 1; // Rester à la fin
        }
    }
}

bool AnimationPlayer::load_image_at_position(const char* file_path, int offset_x, int offset_y) {
    if (!file_path || !tft_display) {
        return false;
    }
    
    // Utiliser une taille de frame par défaut (sera ignorée avec la nouvelle logique basée sur l'entête)
    uint32_t dummy_frame_size = TFTConfig::WIDTH * TFTConfig::HEIGHT * TFTConfig::BYTES_PER_PIXEL;
    
    // Lire et afficher l'image à la position spécifiée
    if (read_frame_from_file(file_path, dummy_frame_size, offset_x, offset_y)) {
        // Envoyer le framebuffer à l'écran
        tft_display->sendFrame();
        return true;
    }
    
    return false;
}

size_t AnimationPlayer::detect_animation_files_count(const char* directory_path) {
    if (!directory_path || !storage_manager || !storage_manager->is_fat32_mounted()) {
        printf("Erreur: Paramètres invalides pour détection des fichiers\n");
        return 0;
    }
    
    printf("Détection du nombre de fichiers d'animation dans: %s\n", directory_path);
    
    // Utiliser StorageManager pour lister le répertoire
    std::vector<FileInfo> files = storage_manager->list_directory(directory_path);
    
    if (files.empty()) {
        printf("Répertoire vide ou inaccessible: %s\n", directory_path);
        return 0;
    }
    
    size_t animation_files_count = 0;
    size_t total_files = files.size();
    
    // Compter les fichiers qui correspondent au pattern FR_XXX.RAW
    for (const auto& file : files) {
        // Ignorer les répertoires
        if (file.is_directory) {
            continue;
        }
        
        // Vérifier si le nom correspond au pattern FR_XXX.RAW
        std::string filename(file.name);
        
        // Convertir en majuscules pour la comparaison
        std::transform(filename.begin(), filename.end(), filename.begin(), ::toupper);
        
        // Vérifier le pattern: FR_ suivi de 3 chiffres puis .RAW
        if (filename.length() >= 10 && 
            filename.substr(0, 3) == "FR_" && 
            filename.substr(filename.length() - 4) == ".RAW") {
            
            // Vérifier que les caractères 3,4,5 sont des chiffres
            bool valid_pattern = true;
            for (size_t i = 3; i < 6 && i < filename.length(); i++) {
                if (!isdigit(filename[i])) {
                    valid_pattern = false;
                    break;
                }
            }
            
            if (valid_pattern) {
                animation_files_count++;
                if (animation_files_count <= 10) { // Afficher les 10 premiers pour debug
                    printf("  Trouvé: %s\n", file.name);
                }
            }
        }
    }
    
    printf("=== Détection terminée ===\n");
    printf("Fichiers totaux: %zu\n", total_files);
    printf("Fichiers d'animation (FR_XXX.RAW): %zu\n", animation_files_count);
    
    if (animation_files_count > 10) {
        printf("  (et %zu autres...)\n", animation_files_count - 10);
    }
    
    printf("========================\n");
    
    return animation_files_count;
}

bool AnimationPlayer::load_animation_auto_detect(const char* directory_path, const char* name) {
    if (!directory_path) {
        printf("Erreur: Chemin de répertoire invalide\n");
        return false;
    }
    
    printf("Chargement automatique d'animation depuis: %s\n", directory_path);
    
    // Détecter automatiquement le nombre de fichiers d'animation
    size_t detected_count = detect_animation_files_count(directory_path);
    
    if (detected_count == 0) {
        printf("Aucun fichier d'animation détecté dans: %s\n", directory_path);
        return false;
    }
    
    printf("Fichiers détectés: %zu\n", detected_count);
    
    // Choisir la méthode de chargement en fonction du nombre de fichiers
    const size_t MEMORY_SAFE_LIMIT = 20;      // Limite pour le mode sécurisé
    const size_t BLOCK_MODE_THRESHOLD = 50;   // Seuil pour le mode par blocs
    const size_t OPTIMAL_BLOCK_SIZE = 10;     // Taille optimale des blocs
    
    bool success = false;
    
    if (detected_count <= MEMORY_SAFE_LIMIT) {
        // Mode sécurisé pour les petites animations
        printf("Mode sécurisé sélectionné (≤%zu fichiers)\n", MEMORY_SAFE_LIMIT);
        success = load_animation_safe(directory_path, name, detected_count);
    } else if (detected_count <= BLOCK_MODE_THRESHOLD) {
        // Mode ultra-économe pour les animations moyennes
        printf("Mode ultra-économe sélectionné (%zu-%zu fichiers)\n", MEMORY_SAFE_LIMIT + 1, BLOCK_MODE_THRESHOLD);
        success = load_animation_generated(directory_path, name, detected_count);
    } else {
        // Mode par blocs pour les très grosses animations
        printf("Mode par blocs sélectionné (>%zu fichiers)\n", BLOCK_MODE_THRESHOLD);
        success = load_animation_by_blocks(directory_path, name, detected_count, OPTIMAL_BLOCK_SIZE);
    }
    
    if (success) {
        printf("Animation chargée avec succès: %zu fichiers\n", detected_count);
    } else {
        printf("Erreur lors du chargement de l'animation\n");
    }
    
    return success;
}