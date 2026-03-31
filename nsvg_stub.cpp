// nsvg_stub.cpp — Provide nanosvg implementation for headless libslic3r linking
// The real implementation lives in slic3r/GUI/BitmapCache.cpp which we don't link.

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"
