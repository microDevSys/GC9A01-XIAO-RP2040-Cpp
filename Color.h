/*
 * Color.h
 *
 *  Created on: 13 janv. 2021
 *      Author: Guillaume Sahuc
 */
#pragma once
#include <stdint.h>

//--------------------------------------------------
// RGB 16 Bpp
// RGB 565
// R4R3R2R1 R0G5G4G3 G2G1G0B4 B3B2B1B0
//--------------------------------------------------
#define COLOR_16BITS_BLACK    		0x0000 
#define COLOR_16BITS_BLUE     		0x001F 
#define COLOR_16BITS_RED      		0xF800 
#define COLOR_16BITS_GREEN    		0x07E0 
#define COLOR_16BITS_CYAN     		0x07FF
#define COLOR_16BITS_MAGENTA  		0xF81F
#define COLOR_16BITS_YELLOW   		0xFFE0
#define COLOR_16BITS_WHITE    		0xFFFF
#define COLOR_16BITS_NAVY           0x0010
#define COLOR_16BITS_DARKBLUE       0x0011
#define COLOR_16BITS_DARKGREEN      0x0320
#define COLOR_16BITS_DARKCYAN       0x0451
#define COLOR_16BITS_TURQUOISE      0x471A
#define COLOR_16BITS_INDIGO         0x4810
#define COLOR_16BITS_DARKRED        0x8000
#define COLOR_16BITS_OLIVE          0x8400
#define COLOR_16BITS_GRAY           0x8410
#define COLOR_16BITS_SKYBLUE        0x867D
#define COLOR_16BITS_BLUEVIOLET     0x895C
#define COLOR_16BITS_LIGHTGREEN     0x9772
#define COLOR_16BITS_DARKVIOLET     0x901A
#define COLOR_16BITS_YELLOWGREEN    0x9E66
#define COLOR_16BITS_BROWN          0xA145
#define COLOR_16BITS_DARKGRAY       0xAD55
#define COLOR_16BITS_SIENNA         0xA285
#define COLOR_16BITS_LIGHTBLUE      0xAEDC
#define COLOR_16BITS_GREENYELLOW    0xAFE5
#define COLOR_16BITS_SILVER         0xC618
#define COLOR_16BITS_LIGHTGREY      0xD69A
#define COLOR_16BITS_LIGHTCYAN      0xE7FF
#define COLOR_16BITS_VIOLET         0xEC1D
#define COLOR_16BITS_AZUR           0xF7FF
#define COLOR_16BITS_BEIGE          0xF7BB
#define COLOR_16BITS_TOMATO         0xFB08
#define COLOR_16BITS_GOLD           0xFEA0
#define COLOR_16BITS_ORANGE         0xFD20
#define COLOR_16BITS_SNOW           0xFFDF

//// R4R3R2R1 R0G5G4G3 G2G1G0B4 B3B2B1B0
typedef union {
	 uint16_t Word; //16bits
	 struct {
    union {
      uint8_t Byte;
      	  struct {
                uint8_t GREEN3       :1;      /* GREEN Bit 3 */
    	  	  	  uint8_t GREEN4     :1;      /* GREEN Bit 4 */
    	  	  	  uint8_t GREEN5     :1;      /* GREEN Bit 5 */
    	  	  	  uint8_t RED0       :1;      /* RED Bit 0 */
    	  	  	  uint8_t RED1       :1;      /* RED Bit 1 */
    	  	  	  uint8_t RED2       :1;      /* RED Bit 2 */
    	  	  	  uint8_t RED3       :1;      /* RED Bit 3 */
    	  	  	  uint8_t RED4       :1;      /* RED Bit 4 */
      	  	  	  }Bits;
      	  } MSB_COLOR;
#define COLOR2 _COLOR16BITS.Overlap.MSB_COLOR.Byte
#define RED4 _COLOR16BITS.Overlap.MSB_COLOR.Bits.RED4
#define RED3 _COLOR16BITS.Overlap.MSB_COLOR.Bits.RED3
#define RED2 _COLOR16BITS.Overlap.MSB_COLOR.Bits.RED2
#define RED1 _COLOR16BITS.Overlap.MSB_COLOR.Bits.RED1
#define RED0 _COLOR16BITS.Overlap.MSB_COLOR.Bits.RED0
#define GREEN5 _COLOR16BITS.Overlap.MSB_COLOR.Bits.GREEN5
#define GREEN4 _COLOR16BITS.Overlap.MSB_COLOR.Bits.GREEN4
#define GREEN3 _COLOR16BITS.Overlap.MSB_COLOR.Bits.GREEN3
   	   union {
            uint8_t Byte;
   	     struct {
   	    		uint8_t BLUE0      :1;         /* BLUE Bit 0 */
   	    		uint8_t BLUE1      :1;         /* BLUE Bit 1 */
   	    		uint8_t BLUE2      :1;         /* BLUE Bit 2 */
   	    		uint8_t BLUE3      :1;         /* BLUE Bit 3 */
   	    		uint8_t BLUE4      :1;         /* BLUE Bit 4 */
   	    		uint8_t GREEN0     :1;         /* GREEN Bit 0 */
   	    		uint8_t GREEN1     :1;         /* GREEN Bit 1 */
   	    		uint8_t GREEN2     :1;         /* GREEN Bit 2 */
   	      } Bits;
   	    } LSB_COLOR;
#define COLOR1 _COLOR16BITS.Overlap.LSB_COLOR.Byte
#define GREEN2 _COLOR16BITS.Overlap.LSB_COLOR.Bits.GREEN2
#define GREEN1 _COLOR16BITS.Overlap.LSB_COLOR.Bits.GREEN1
#define GREEN0 _COLOR16BITS.Overlap.LSB_COLOR.Bits.GREEN0
#define BLUE4 _COLOR16BITS.Overlap.LSB_COLOR.Bits.BLUE4
#define BLUE3 _COLOR16BITS.Overlap.LSB_COLOR.Bits.BLUE3
#define BLUE2 _COLOR16BITS.Overlap.LSB_COLOR.Bits.BLUE2
#define BLUE1 _COLOR16BITS.Overlap.LSB_COLOR.Bits.BLUE1
#define BLUE0 _COLOR16BITS.Overlap.LSB_COLOR.Bits.BLUE0
	  }Overlap;
	 struct {
		  uint8_t BLUE  :5;
		  uint8_t GREEN :6;
		  uint8_t RED   :5;
  	  	  	 } MergedBits;
#define RED _COLOR16BITS.MergedBits.RED
#define GREEN _COLOR16BITS.MergedBits.GREEN
#define BLUE _COLOR16BITS.MergedBits.BLUE
}RGB16;
extern volatile RGB16 _COLOR16BITS;


// Structure couleur RGB
struct Color_RGB {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

// Pixel component selector
enum Pixelcolor {
    Red,
    Green,
    Blue
};