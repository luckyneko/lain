// Compiles the tinyobjloader implementation in its own translation unit, apart from lain's strict flags
// (addTinyObjLoader.cmake builds this TU with warnings off). Single precision, as lain::mesh holds it.
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
