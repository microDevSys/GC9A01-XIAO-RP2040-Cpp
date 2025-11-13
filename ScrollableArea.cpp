#include "ScrollableArea.h"
#include "TFT.h"
#include "main.h"

/*******************************************************
 * Nom du fichier : ScrollableArea.cpp
 * Auteur         : Guillaume Sahuc
 * Date           : 13 novembre 2025
 * Description    : implementation zone scrollable
 *******************************************************/

ScrollableArea::ScrollableArea(int x, int y, int w, int h) 
    : x(x), y(y), width(w), height(h), content_height(0), scroll_position(0) {}

void ScrollableArea::addLine(const std::string& line) {
    lines.push_back(line);
    content_height = lines.size() * 8; // 8 pixels par ligne
}

void ScrollableArea::scrollUp(int pixels) {
    scroll_position += pixels;
    int max_scroll = content_height - height;
    if (scroll_position > max_scroll) scroll_position = max_scroll;
    if (scroll_position < 0) scroll_position = 0;
}

void ScrollableArea::scrollDown(int pixels) {
    scroll_position -= pixels;
    if (scroll_position < 0) scroll_position = 0;
}

void ScrollableArea::draw(TFT& tft) {
    // Fond de la zone
    tft.fillRect(x, y, width, height, COLOR_16BITS_BLACK);
    tft.drawRect(x, y, width, height, COLOR_16BITS_WHITE);
    
    // Calculer quelles lignes afficher
    int line_height = 8;
    int first_line = scroll_position / line_height;
    int visible_lines = height / line_height;
    
    for (int i = 0; i < visible_lines && (first_line + i) < (int)lines.size(); i++) {
        int line_index = first_line + i;
        int text_y = y + 2 + (i * line_height) - (scroll_position % line_height);
        
        if (text_y >= y && text_y < y + height - line_height) {
            tft.drawText(x + 2, text_y, lines[line_index].c_str(), 
                        COLOR_16BITS_WHITE);
        }
    }
    
    // Barre de scroll (optionnel)
    drawScrollbar(tft);
}

void ScrollableArea::drawScrollbar(TFT& tft) {
    if (content_height <= height) return; // Pas besoin de scrollbar
    
    int scrollbar_x = x + width - 3;
    int scrollbar_height = height - 4;
    int thumb_height = (height * scrollbar_height) / content_height;
    int thumb_y = y + 2 + (scroll_position * scrollbar_height) / content_height;
    
    // Fond de la scrollbar
    tft.drawLine(scrollbar_x, y + 2, scrollbar_x, y + height - 2, COLOR_16BITS_GRAY);
    
    // Curseur de la scrollbar
    tft.fillRect(scrollbar_x - 1, thumb_y, 3, thumb_height, COLOR_16BITS_WHITE);
}