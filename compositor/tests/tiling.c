#include "tiling.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
 const unsigned width = 1920, height = 1080;
 size_t bytes = PS5_TILE_EXTENT(width, height);
 assert(bytes == 8912896);
 unsigned char *seen = calloc(bytes / 4, 1); assert(seen);
 unsigned max = 0;
 for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
  unsigned column = (x / 512) * 65536 + tile_offset(x % 512, 0);
  unsigned index = (y / 128) * (128 * width) + (column ^ tile_offset(0, y % 128));
  assert(index < bytes / 4); assert(!seen[index]); seen[index] = 1;
  if (index > max) max = index;
 }
 printf("PASS 2073600 unique 1080p pixel addresses; maximum byte end=%u SDL extent=%zu rounded scanout=10485760\n", (max + 1) * 4, bytes);
 free(seen); return 0;
}
