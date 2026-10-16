#ifndef PS5WL_TILING_H
#define PS5WL_TILING_H
#include <stdint.h>
/* SDL PS5_Tilemap_BufferSize: final band + final complete 512x128 tile. */
#define PS5_TILE_EXTENT(w, h) (((((h)-1)/128)*(128*(w)) + (((w)-1)/512)*65536 + 65536) * 4)
static inline uint32_t tile_offset(uint32_t x, uint32_t y) {
 return ((x & 1) | ((x >> 1 & 1) << 1) | ((y & 1) << 2) |
 ((y >> 1 & 1) << 3) | ((y >> 2 & 1) << 4) | ((x >> 2 & 1) << 5) |
 (((x >> 3 ^ y >> 3) & 1) << 6) | (((x >> 4 ^ y >> 4) & 1) << 7) |
 (((x >> 6 ^ y >> 5) & 1) << 8) | (((x >> 5 ^ y >> 6) & 1) << 9) |
 ((y >> 3 & 1) << 10) | ((x >> 4 & 1) << 11) | ((y >> 6 & 1) << 12) |
 ((x >> 6 & 1) << 13) | ((x >> 7 & 1) << 14) | ((x >> 8 & 1) << 15));
}
#endif
