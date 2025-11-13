#pragma once
#include "Color.h"
#include <vector>
#include <string>

class TFT;

class ScrollableArea {
public:
    int x, y, width, height;
    int content_height; // Hauteur totale du contenu
    int scroll_position; // Position actuelle du scroll
    std::vector<std::string> lines;
    
    ScrollableArea(int x, int y, int w, int h);
    void addLine(const std::string& line);
    void scrollUp(int pixels = 8);
    void scrollDown(int pixels = 8);
    void draw(TFT& tft);
    
private:
    void drawScrollbar(TFT& tft);
};