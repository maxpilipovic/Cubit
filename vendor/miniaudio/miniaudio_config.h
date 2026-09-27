#pragma once

// The one place miniaudio is configured. Both miniaudio.c and the engine's
// AudioEngine.cpp include this rather than miniaudio.h directly, because these
// options change the layout of miniaudio's structs: a file that saw a different
// set would disagree with the library about how big an ma_engine is, and
// nothing would say so.
//
// Cubit hands miniaudio samples it made itself, so everything that reads,
// writes or generates audio data is compiled out. Loading real sound files
// would mean removing MA_NO_DECODING and MA_NO_RESOURCE_MANAGER here.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER

#include "miniaudio.h"
