#pragma once

#include <cstdint>

// Packed BMP structures adapted for GCC/Clang
#pragma pack(push, 1)

typedef struct s_bmp_fh {
    uint16_t sType;       // Two chars 'BM' (0x4D42 little-endian)
    uint32_t iSize;       // Total file size in bytes
    uint16_t sReserved1;  // 0
    uint16_t sReserved2;  // 0
    uint32_t iOffBits;    // Offset to pixel data
} t_bmp_fh;

typedef struct s_bmp_sh {
    uint32_t iSize;         // Header size (BITMAPINFOHEADER = 40)
    int32_t  iWidth;        // Bitmap width in pixels
    int32_t  iHeight;       // Bitmap height in pixels (positive=bottom-up, negative=top-down)
    uint16_t sPlanes;       // Must be 1
    uint16_t sBitCount;     // Bits per pixel (e.g., 24)
    uint32_t iCompression;  // Compression (0 = BI_RGB)
    uint32_t iSizeImage;    // Image size (may be 0 for BI_RGB)
    int32_t  iXpelsPerMeter;// Horizontal resolution (pixels per meter)
    int32_t  iYpelsPerMeter;// Vertical resolution (pixels per meter)
    uint32_t iClrUsed;      // Number of colors used
    uint32_t iClrImportant; // Number of important colors
} t_bmp_sh;

typedef struct s_bmp_header {
    t_bmp_fh first_header;
    t_bmp_sh second_header;
} t_bmp_header;

typedef struct s_bmp {
    t_bmp_header header;
    int32_t      width;          // Convenience copy of width
    int32_t      width_useless;  // Useless/legacy field (kept for compatibility)
    int32_t      height;         // Convenience copy of height
    int32_t      size;           // width * height
    // Potential pixel storage pointer(s) could be added here if needed
} t_bmp;

#pragma pack(pop)


