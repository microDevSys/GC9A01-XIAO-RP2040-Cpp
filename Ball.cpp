#include "Ball.h"
#include <cstdlib>
#include <cmath>

/*******************************************************
 * Nom du fichier : Ball.cpp
 * Auteur         : Guillaume Sahuc
 * Date           : 13 novembre 2025
 * Description    : test affichage balle sur l'écran
 *******************************************************/

Ball::Ball(int width, int height) {
    radius = 2 + rand() % 9;
    x = radius + rand() % (width - 2 * radius);
    y = radius + rand() % (height - 2 * radius);
    float speed = 3.0f + (rand() % 10);
    float angle = ((float)rand() / RAND_MAX) * 2.0f * M_PI;
    vx = speed * cosf(angle);
    vy = speed * sinf(angle);
    uint8_t r = rand() % 32, g = rand() % 64, b = rand() % 32;
    color = ((r << 11) | (g << 5) | b);
}

void Ball::update(int width, int height) {
    x += vx;
    y += vy;
    if (x - radius < 0) { x = radius; vx = -vx; }
    if (x + radius >= width) { x = width - radius - 1; vx = -vx; }
    if (y - radius < 0) { y = radius; vy = -vy; }
    if (y + radius >= height) { y = height - radius - 1; vy = -vy; }
}