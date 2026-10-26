// A minimal MPEG audio layer III decoder (MPEG-1, MPEG-2 and 2.5; mono, stereo, joint stereo
// without intensity stereo) for the game's voice clips (voices\*.mp3, 22050 Hz mono, 64 kbit/s).
// The original plays them through soundx.dll and Windows' ACM MP3 codec.
#pragma once
#include <cstdint>
#include <memory>

#include "platform/ui_platform.h"

// Decodes a whole file (an ID3v2 tag is skipped); null when no frame decodes.
std::shared_ptr<const platform::Pcm> mp3_decode(const uint8_t* data, uint32_t size);
