// The game's movies (movies\*.wve): Electronic Arts' container (SCHl audio header, SCCl count,
// pIQT video frames, SCDl audio blocks, SCEl end) with TQI video (MPEG-1 intra macroblocks,
// 15 frames per second) and EA ADPCM stereo sound (22050 Hz). The original plays them through EA's
// player library (video module, 0x5CAE80..).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "platform/ui_platform.h"

class WveMovie {
public:
    // Reads the file; false when it is not a WVE movie.
    bool open(const char* path);
    int32_t width() const { return width_; }
    int32_t height() const { return height_; }
    int32_t frames() const { return (int32_t)video_.size(); }
    static constexpr int32_t kFps = 15;
    // The whole soundtrack (stereo, 22050 Hz); null when the movie has none.
    std::shared_ptr<const platform::Pcm> sound() const { return sound_; }
    // Decodes frame n into 0xAARRGGBB pixels (width x height). Frames are independent (intra only).
    bool decode(int32_t n, std::vector<uint32_t>* rgb);

private:
    std::vector<uint8_t> data_;
    std::vector<std::pair<uint32_t, uint32_t>> video_;  // offset, size of each frame's payload
    std::shared_ptr<platform::Pcm> sound_;
    int32_t width_ = 0, height_ = 0;
};
