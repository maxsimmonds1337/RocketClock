// a helper program for converting images to led grids
//
//
//

#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

typedef struct {

  unsigned char red;
  unsigned char green;
  unsigned char blue;

} pixel;

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: %s <image.jpg|image.png>\n", argv[0]);
    return 1;
  }

  int width, height, channels;
  unsigned char *img = stbi_load(argv[1], &width, &height, &channels, 3);
  printf("width: %d, height: %d, channels: %d", width, height, channels);
  if (img == NULL) {
    printf("Error loading image: %s\n", stbi_failure_reason());
    return 1;
  }

  printf("Loaded %s: %dx%d, %d channels\n", argv[1], width, height, channels);

  // image is addressible by
  // img[(y * width + x) * 3 + 0
  // get first pixel, for testing, then grey scale it
  // latest, make a type def pixel
  // indexing each pixel is [256,256,256] we can access any pixel (x,y) by
  // going y * width along the image (row selector). This works by going
  // multiples of the width through the image space, to jump down columns.
  // Then, when you reach the correct row, you go along by "x" to select the
  // pixel. We multply this by 3, since there are 3 RGB values per pixel.
  // Then, + 0 = R, + 1 = G, and + 2 = B.
  //
  //          x
  // +----------------+     ^
  // |        *       | y   |
  // |                |     | height
  // |                |     |
  // +----------------+     v
  // <------ width --->
  //

  // greyscale and use that one pixel to make a whole 64x64 image
  //
  unsigned char *new_image = malloc(width * height * channels);

  for (int x = 0; x < width; x++) {
    for (int y = 0; y < height; y++) {
      unsigned char r = img[(y * width + x) * 3 + 0];
      unsigned char g = img[(y * width + x) * 3 + 1];
      unsigned char b = img[(y * width + x) * 3 + 2];

      unsigned char greyValue = (r + g + b) / 3;
      new_image[(width * y + x) * 3 + 0] = greyValue;
      new_image[(width * y + x) * 3 + 1] = greyValue;
      new_image[(width * y + x) * 3 + 2] = greyValue;
    }
  }
  stbi_write_png("out.png", width, height, 3, new_image, width * 3);
  stbi_image_free(img);
  free(new_image);
  return 0;
}
