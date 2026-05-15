// WHAT:
//   Single translation unit that instantiates miniaudio implementation.
// WHY:
//   miniaudio is a single-header library; implementation macro must appear in
//   exactly one .cpp file to avoid duplicate symbol definitions at link time.
//   Vorbis decoding requires stb_vorbis to be wired before miniaudio implementation.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// Compile stb_vorbis implementation in this same translation unit.
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
