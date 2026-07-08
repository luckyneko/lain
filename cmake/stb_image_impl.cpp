// Compiles the stb_image implementation in its own translation unit, apart from lain's
// strict flags — stb_image.h's implementation is noisy under -Werror -Wall -Wextra, so it
// must not be built as lain code (addStb.cmake builds this TU with warnings off). JPEG
// only: png/tiff have dedicated codecs; stb backs the JPEG plugin. No stdio (we decode from
// memory). The header is resolved from the fetched stb source dir (see addStb.cmake).
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
