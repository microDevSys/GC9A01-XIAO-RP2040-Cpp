#pragma once

#include "main.h"           // Pour TFTConfig
#include "StorageManager.h"
#include "TFT.h"
#include <vector>
#include <string>

// Structure pour représenter une frame d'animation
struct AnimationFrame {
    uint8_t* data;      // Données de la frame (RGB565)
    uint32_t size;      // Taille des données
    uint16_t delay_ms;  // Délai avant la frame suivante
    
    AnimationFrame() : data(nullptr), size(0), delay_ms(100) {}
    ~AnimationFrame() {
        if (data) {
            delete[] data;
        }
    }
};

// Structure pour représenter une animation complète
struct Animation {
    std::vector<AnimationFrame*> frames;
    std::string name;
    bool loop;
    // Streaming sources (to avoid storing frames in RAM)
    bool stream_from_dir_files;                 // If true: frames are files in frame_paths
    std::vector<std::string> frame_paths;       // For directory-based frames
    uint32_t num_frames_stream;                 // Number of frames available for streaming
    uint32_t frame_size_bytes;                  // Cached frame size
    
    // Mode ultra-économe : génération à la volée
    bool stream_generated_names;                // If true: generate FR_XXX.RAW names on-the-fly
    std::string base_directory;                 // Base directory for generated names
    
    // Mode par blocs : gestion séquentielle de gros volumes
    bool stream_by_blocks;                      // If true: load animation by blocks
    uint32_t total_files_available;             // Total number of files in directory
    uint32_t current_block_start;               // Current block starting index
    uint32_t block_size;                        // Number of files per block
    
    Animation() : loop(true), stream_from_dir_files(false),
                  num_frames_stream(0), frame_size_bytes(0),
                  stream_generated_names(false), stream_by_blocks(false),
                  total_files_available(0), current_block_start(0), block_size(10) {}
    ~Animation() {
        for (auto frame : frames) {
            delete frame;
        }
        frames.clear();
    }
};

class AnimationPlayer {
private:
    StorageManager* storage_manager;
    TFT* tft_display;
    
    std::vector<Animation*> animations;
    int current_animation_index;
    int current_frame_index;
    uint32_t last_frame_time;
    int performance_mode; // 0=normal(33ms), 1=rapide(16ms), 2=ultra(8ms)
    
    // Cache pour optimisation
    uint8_t* next_frame_cache;          // Cache pour la frame suivante
    uint32_t next_frame_cache_size;     // Taille du cache
    int next_frame_cached_index;        // Index de la frame mise en cache

    // Fonctions privées
    bool read_frame_from_file(const char* full_path, uint32_t frame_size, int offset_x = 0, int offset_y = 0);
    void check_memory_usage() const;
    bool load_next_block(Animation* anim);      // Charge le bloc suivant
    void update_block_animation(Animation* anim); // Met à jour l'animation par blocs
    
public:
    AnimationPlayer(StorageManager* storage, TFT* tft);
    ~AnimationPlayer();
    
    // Gestion des animations
    // Charge une animation depuis un répertoire contenant des fichiers .raw triés (DirectoryFrames)
    bool load_animation(const char* directory_path, const char* name = nullptr);
    // Version sécurisée avec limite explicite de fichiers
    bool load_animation_safe(const char* directory_path, const char* name, size_t max_files);
    // Version ultra-économe : génère les noms à la volée (0 allocation de chemins)
    bool load_animation_generated(const char* directory_path, const char* name, size_t frame_count);
    // Version par blocs : charge l'animation complète par blocs séquentiels
    bool load_animation_by_blocks(const char* directory_path, const char* name, size_t total_files, size_t block_size = 10);
    
    // Contrôle de lecture
    bool play_animation(int animation_index);
    bool play_animation(const char* animation_name);
    void update(); // À appeler dans la boucle principale
    void stop();
    void pause();
    void resume();
    
    // Navigation
    void next_animation();
    void previous_animation();
    void next_frame();
    void previous_frame();
    
    // Configuration
    void set_loop(bool loop_enabled);
    void set_frame_delay(uint16_t delay_ms);
    void set_performance_mode(int mode); // 0=normal, 1=rapide, 2=ultra-rapide
    void optimize_block_size_for_performance(); // Ajuste la taille des blocs automatiquement
    void measure_performance(int frames_to_measure = 100); // Mesure les FPS réels
    
    // Informations
    int get_animation_count() const { return animations.size(); }
    int get_current_animation_index() const { return current_animation_index; }
    int get_current_frame_index() const { return current_frame_index; }
    const char* get_current_animation_name() const;
    
    // Utilitaires
    void list_animations() const;
    void clear_all_animations(); // Nettoie toutes les animations chargées
    
    // Détection automatique du nombre de blocs/fichiers
    size_t detect_animation_files_count(const char* directory_path); // Compte les fichiers FR_XXX.RAW
    bool load_animation_auto_detect(const char* directory_path, const char* name = nullptr); // Détecte automatiquement et charge
    
    // Chargement d'image avec offset
    bool load_image_at_position(const char* file_path, int offset_x, int offset_y);
};