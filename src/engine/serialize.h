// Explicit serialisation of the game-state records (docs/CLEANUP_PLAN.md, section 3, phase B).
//
// Savegames, the harness's .state snapshots and the state hash hold the records in the original's
// byte layout: little-endian integers, fields in declaration order, no padding (the layout structs
// are packed). Each record type has one function that lists its fields,
//
//   template <class Io> void serialize(Io& io, VEH& r) { io(r.x); io(r.y); ... }
//
// and the same function writes (Writer: object -> bytes) and reads (Reader: bytes -> object). The
// byte layout is therefore the code's, not the compiler's: a record's in-memory type may change
// (reordered fields, wider types, real pointers) as long as its serialize() still produces the
// original's bytes. tests/unit/test_serialize.cpp checks every record against its layout.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

// Writes records into a byte buffer.
class Writer {
public:
    static constexpr bool kReading = false;
    std::vector<uint8_t> bytes;

    template <class T>
    void operator()(T& v) {
        field(v);
    }
    void raw(const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        bytes.insert(bytes.end(), b, b + n);
    }
    const uint8_t* data() const { return bytes.data(); }
    uint32_t size() const { return static_cast<uint32_t>(bytes.size()); }

private:
    template <class T>
    void field(T& v);
};

// Reads records from a byte buffer. Reading past the end leaves the rest of the record unchanged
// and clears ok().
class Reader {
public:
    static constexpr bool kReading = true;
    Reader(const void* p, size_t n) : p_(static_cast<const uint8_t*>(p)), n_(n) {}

    template <class T>
    void operator()(T& v) {
        field(v);
    }
    void raw(void* out, size_t n) {
        if (pos_ + n > n_) {
            ok_ = false;
            pos_ = n_;
            return;
        }
        std::memcpy(out, p_ + pos_, n);
        pos_ += n;
    }
    bool ok() const { return ok_; }
    size_t pos() const { return pos_; }

private:
    template <class T>
    void field(T& v);
    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
    bool ok_ = true;
};

namespace serialize_detail {
template <class T>
constexpr bool is_scalar_v = std::is_integral_v<T> || std::is_enum_v<T>;
template <class T, bool = std::is_enum_v<T>>
struct integer_of {
    using type = T;
};
template <class T>
struct integer_of<T, true> {
    using type = std::underlying_type_t<T>;
};
template <class T>
using unsigned_of_t = std::make_unsigned_t<typename integer_of<T>::type>;
template <class T>
constexpr bool is_byte_array_v =
    std::is_array_v<T> && sizeof(std::remove_all_extents_t<T>) == 1 && is_scalar_v<std::remove_all_extents_t<T>>;
}  // namespace serialize_detail

// Integers little-endian in their own width; arrays element by element (byte arrays as one
// block); any other type through its serialize() overload.
template <class T>
void Writer::field(T& v) {
    using namespace serialize_detail;
    if constexpr (is_byte_array_v<T>) {
        raw(&v, sizeof(T));
    } else if constexpr (std::is_array_v<T>) {
        for (auto& e : v) field(e);
    } else if constexpr (is_scalar_v<T>) {
        using U = unsigned_of_t<T>;
        U u = static_cast<U>(v);
        for (size_t i = 0; i < sizeof(T); i++) bytes.push_back(static_cast<uint8_t>(u >> (8 * i)));
    } else {
        serialize(*this, v);
    }
}

template <class T>
void Reader::field(T& v) {
    using namespace serialize_detail;
    if constexpr (is_byte_array_v<T>) {
        raw(&v, sizeof(T));
    } else if constexpr (std::is_array_v<T>) {
        for (auto& e : v) field(e);
    } else if constexpr (is_scalar_v<T>) {
        using U = unsigned_of_t<T>;
        if (pos_ + sizeof(T) > n_) {
            ok_ = false;
            pos_ = n_;
            return;
        }
        U u = 0;
        for (size_t i = 0; i < sizeof(T); i++) u = static_cast<U>(u | static_cast<U>(p_[pos_ + i]) << (8 * i));
        pos_ += sizeof(T);
        v = static_cast<T>(u);
    } else {
        serialize(*this, v);
    }
}

// The bytes of count records starting at first, as the original lays them out.
template <class T>
Writer write_records(const T* first, size_t count) {
    Writer w;
    for (size_t i = 0; i < count; i++) w(const_cast<T&>(first[i]));
    return w;
}

// Reads count records from bytes into first. Returns false if bytes holds fewer.
template <class T>
bool read_records(T* first, size_t count, const void* bytes, size_t size) {
    Reader r(bytes, size);
    for (size_t i = 0; i < count; i++) r(first[i]);
    return r.ok();
}
