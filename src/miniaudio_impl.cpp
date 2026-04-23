// WHAT:
//   Single translation unit that instantiates miniaudio implementation.
// WHY:
//   miniaudio is a single-header library; implementation macro must appear in
//   exactly one .cpp file to avoid duplicate symbol definitions at link time.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
