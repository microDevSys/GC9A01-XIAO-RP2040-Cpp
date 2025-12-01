#pragma once

/**
 * @file TFT.h
 * @brief Driver pour écran TFT avec polices et primitives graphiques
 * @author Guillaume Sahuc
 * @date 2025
 * 
 * @class TFT
 * @brief Gestionnaire d'écran TFT avec framebuffer et support multi-polices
 *
 * Cette classe fournit une interface complète pour :
 * - Initialisation et communication SPI avec l'écran
 * - Gestion du framebuffer et transferts SPI (bloquant)
 * - Primitives de dessin (lignes, rectangles, cercles)
 * - Rendu de texte avec plusieurs polices (Mini, Standard, Arial32)
 * - Gestion de la rotation d'écran et du scroll
 * - Support des animations (balles, marqueurs horaires)
 *
 */

#include <cstdint>
#include <vector>
#include "Ball.h"
#include "Color.h"
#include "main.h"
#include "arial_S32.h"

// ===== ÉNUMÉRATIONS =====

/**
 * @enum FontType
 * @brief Types de polices disponibles
 */
enum class FontType {
    FONT_MINI,      ///< Police 4x6 pixels - Compacte
    FONT_STANDARD,  ///< Police 8x12 pixels - Standard
    ARIAL_32        ///< Police Arial 32 pixels - Haute qualité
};

/**
 * @enum Rotation
 * @brief Orientations d'écran disponibles
 */
enum class Rotation {
    PORTRAIT_0 = 0,     ///< 0° - Portrait normal
    LANDSCAPE_90 = 1,   ///< 90° - Paysage (rotation horaire)
    PORTRAIT_180 = 2,   ///< 180° - Portrait inversé
    LANDSCAPE_270 = 3   ///< 270° - Paysage inversé (rotation anti-horaire)
};

// ===== CLASSE PRINCIPALE =====

class TFT {
public:
    // ===== CONSTRUCTEUR/DESTRUCTEUR =====
    TFT();
    ~TFT();

    // ===== INITIALISATION =====
    /**
    * @brief Initialise l'écran TFT (GPIO, SPI, LCD)
     */
    void init();

    // ===== GESTION DU FRAMEBUFFER =====
    /**
     * @brief Remplit tout l'écran avec une couleur
     * @param color Couleur 16 bits (RGB565)
     */
    void fill(uint16_t color);
    
    /**
     * @brief Efface l'écran (remplit en noir RGB565) et envoie immédiatement la frame à l'écran.
     */
    void clear();
    
    /**
     * @brief Définit un pixel à une position donnée
     * @param x Coordonnée X
     * @param y Coordonnée Y
     * @param color Couleur 16 bits
     */
    void setPixel(int x, int y, uint16_t color);
    
    /**
     * @brief Définit la couleur de remplissage par défaut
     * @param color Couleur 16 bits
     */
    void setFillColor(uint16_t color);
    
    /**
    * @brief Envoie le framebuffer vers l'écran via SPI (bloquant)
     */
    void sendFrame();

    /**
     * @brief Copie une frame complète RGB565 dans le framebuffer interne.
     * @param src Pointeur vers les données source (taille attendue: TFTConfig::FB_SIZE_BYTES)
     * @note Cette fonction n'envoie pas l'image à l'écran; appelez sendFrame() après blit.
     */
    void blitRGB565FullFrame(const uint8_t* src);

    // Accès direct au framebuffer pour streaming sans buffer intermédiaire
    uint8_t* getFramebuffer() { return framebuffer; }
    // Accès au framebuffer en 16-bit (plus efficace pour manipuler des pixels)
    inline uint16_t* getFramebuffer16() { return reinterpret_cast<uint16_t*>(framebuffer); }
    // Taille du framebuffer (en octets)
    size_t getFramebufferSize() const;

    // ===== PRIMITIVES DE DESSIN =====
    /**
     * @brief Dessine une ligne entre deux points
     */
    void drawLine(int x0, int y0, int x1, int y1, uint16_t color);
    
    /**
     * @brief Dessine le contour d'un rectangle
     */
    void drawRect(int x, int y, int w, int h, uint16_t color);
    
    /**
     * @brief Dessine un rectangle rempli
     */
    void fillRect(int x, int y, int w, int h, uint16_t color);
    
    /**
     * @brief Dessine le contour d'un cercle
     */
    void drawCircle(int xc, int yc, int r, uint16_t color);
    
    /**
     * @brief Dessine un cercle rempli
     */
    void drawFillCircle(int xc, int yc, int r, uint16_t color);
    
    /**
     * @brief Dessine un petit cercle optimisé (rayon < 10)
     */
    void drawSmallCircle(int xc, int yc, int r, uint16_t color);
    /**
    * @brief Envoie uniquement une région rectangulaire du framebuffer via SPI
     * @param x x de départ
     * @param y y de départ
     * @param w largeur
     * @param h hauteur
     */
    void sendRegion(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

    // ===== GESTION DES POLICES =====
    /**
     * @brief Définit la police courante
     * @param font Type de police à utiliser
     */
    void setFont(FontType font);
    
    /**
     * @brief Retourne la police courante
     */
    FontType getFont() const;
    
    /**
     * @brief Dessine un caractère à une position
     * @param x Position X
     * @param y Position Y
     * @param c Caractère à dessiner
     * @param color Couleur du texte
     */
    void drawChar(int x, int y, char c, uint16_t color);
    
    /**
     * @brief Dessine une chaîne de caractères
     * @param x Position X de départ
     * @param y Position Y
     * @param text Texte à afficher
     * @param color Couleur du texte
     */
    void drawText(int x, int y, const char* text, uint16_t color);

    // ===== MÉTRIQUES DE TEXTE =====
    /**
     * @brief Retourne la largeur d'un caractère spécifique
     * @param c Caractère à mesurer
     * @return Largeur en pixels
     */
    int getCharWidth(char c);
    
    /**
     * @brief Retourne la largeur totale d'un texte
     * @param text Texte à mesurer
     * @return Largeur totale en pixels
     */
    int getTextWidth(const char* text);
    
       // ===== GESTION DE LA ROTATION =====
    /**
     * @brief Définit l'orientation de l'écran
     * @param rotation Nouvelle orientation
     */
    void setRotation(Rotation rotation);
    
    /**
     * @brief Retourne l'orientation courante
     */
    Rotation getRotation() const;
    
    /**
     * @brief Retourne la largeur actuelle de l'écran
     */
    int getScreenWidth() const;
    
    /**
     * @brief Retourne la hauteur actuelle de l'écran
     */
    int getScreenHeight() const;

    // ===== GESTION DU SCROLL =====
    /**
     * @brief Définit les décalages de scroll
     * @param x_offset Décalage horizontal
     * @param y_offset Décalage vertical
     */
    void setScrollOffset(int x_offset, int y_offset);
    
    /**
     * @brief Ajoute un décalage de scroll
     * @param dx Décalage horizontal relatif
     * @param dy Décalage vertical relatif
     */
    void scroll(int dx, int dy);
    
    /**
     * @brief Scroll vers le haut
     * @param lines Nombre de lignes de texte
     */
    void scrollUp(int lines = 1);
    
    /**
     * @brief Scroll vers le bas
     * @param lines Nombre de lignes de texte
     */
    void scrollDown(int lines = 1);
    
    /**
     * @brief Scroll vers la gauche
     * @param pixels Nombre de pixels
     */
    void scrollLeft(int pixels = 1);
    
    /**
     * @brief Scroll vers la droite
     * @param pixels Nombre de pixels
     */
    void scrollRight(int pixels = 1);

    // ===== FONCTIONS SPÉCIALISÉES =====
    /**
     * @brief Dessine une collection de balles
     * @param balls Vecteur de balles à dessiner
     */
    void drawBalls(const std::vector<Ball>& balls);
    
    /**
     * @brief Dessine les marqueurs de secondes d'une horloge
     */
    void drawSecondsMarkers();

private:
    // ===== VARIABLES MEMBRES =====
    
    // Framebuffer et affichage
    uint8_t* framebuffer;           ///< Buffer d'image en mémoire
    uint16_t fill_color;            ///< Couleur de remplissage par défaut
   
    // Scroll et transformation
    int scroll_x, scroll_y;         ///< Décalages de scroll actuels
    Rotation current_rotation;      ///< Rotation courante de l'écran
    int screen_width, screen_height; ///< Dimensions actuelles de l'écran
    
    // Police courante
    FontType current_font;          ///< Type de police actuellement sélectionnée
    
    // Singleton instance not required (no DMA callbacks)
    
    // ===== MÉTHODES PRIVÉES =====
    
    // Initialisation
    void initGPIO();                ///< Configuration des GPIO
    void initSPI();                 ///< Configuration du SPI
    void initFramebuffer();         ///< Allocation du framebuffer
    // DMA no longer used. Send frame implemented using blocking SPI.
    void initSequence();            ///< Séquence d'initialisation LCD
    
    // Communication SPI
    void writeCmd(const uint8_t* cmd, size_t len);
    void writeData(const uint8_t* data, size_t len);
    void cmdWithData(const uint8_t cmd, const uint8_t* data, size_t datalen);
    void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    
    // (DMA removed) IRQ/Handler removed
    
    // Polices - Méthodes génériques
    const uint8_t* getFontData(char c);     ///< Données bitmap d'un caractère
    int getFontWidth();             ///< Largeur de référence de la police
    int getFontHeight();            ///< Hauteur de la police courante
    
    // Police Arial32 - Méthodes spécialisées
    const arial_S32_CharInfo& getArialCharInfo(char c); ///< Info d'un caractère Arial32
    void drawArialChar(int x, int y, char c, uint16_t color); ///< Rendu Arial32
    
      
    // Rotation et transformation
    void updateScreenDimensions();  ///< Met à jour les dimensions après rotation
    void transformCoordinates(int& x, int& y) const; ///< Transforme les coordonnées selon rotation
};

// ===== CONSTANTES ET MACROS =====

/**
 * @brief Conversion RGB vers RGB565
 */
#define RGB_TO_RGB565(r, g, b) \
    ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))

/**
 * @brief Vérification des limites d'écran
 */
#define IS_VALID_COORD(x, y, w, h) ((x) >= 0 && (y) >= 0 && (x) < (w) && (y) < (h))