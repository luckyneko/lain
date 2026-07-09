// Compiles the stb_image + stb_image_write implementations in their own translation unit,
// apart from lain's strict flags — stb's implementations are noisy under -Werror -Wall
// -Wextra, so they must not be built as lain code (addStb.cmake builds this TU with warnings
// off). JPEG only: png/tiff have dedicated codecs; stb backs the JPEG plugin (read + write).
// No stdio (we decode/encode from/to memory). Headers resolve from the fetched stb source
// dir (see addStb.cmake).
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
