// Shared helpers of the unit tests.
#pragma once
#include <cstdint>
#include <cstring>

#include "doctest/doctest.h"
#include "engine/globals.h"

// Saves a global on construction and restores it on destruction, so a test can set game state (the
// memory image or the game-state records) without leaking it into other tests.
template <class T>
class SaveGlobal {
public:
    explicit SaveGlobal(T& ref) : ref_(ref) { std::memcpy(&saved_, &ref, sizeof(T)); }
    ~SaveGlobal() { std::memcpy(&ref_, &saved_, sizeof(T)); }
    SaveGlobal(const SaveGlobal&) = delete;
    SaveGlobal& operator=(const SaveGlobal&) = delete;

private:
    T& ref_;
    T saved_;
};

// Map dimensions as the original sets them (map_init): MapHalfX = MapAreaX / 2.
struct MapDims {
    SaveGlobal<int32_t> x{MapAreaX}, y{MapAreaY}, half{MapHalfX}, flat{MapToggleFlat};
    MapDims(int32_t width, int32_t height, bool flat_map) {
        MapAreaX = width;
        MapAreaY = height;
        MapHalfX = width / 2;
        MapToggleFlat = flat_map ? 1 : 0;
    }
};
