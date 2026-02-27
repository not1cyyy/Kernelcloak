#pragma once
#include "../config.h"
#include "../core/types.h"

#if KC_ENABLE_VALUE_OBFUSCATION

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace kernelcloak {
namespace obfuscation {
namespace detail {

// compiler barrier to prevent MSVC from folding XOR pairs
KC_FORCEINLINE void compiler_barrier() {
#ifdef _MSC_VER
    _ReadWriteBarrier();
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

// obfuscated integer storage - value XOR'd with compile-time key
// Key  = low  32 bits of the XOR mask (always used)
// Key2 = high 32 bits of the XOR mask (only meaningful when sizeof(T) == 8)
// For 32-bit types Key2 is ignored; for 64-bit types the two halves are
// combined so the full value width is covered and no bits leak in plaintext.
template<typename T, uint32_t Key, uint32_t Key2 = 0>
class obfuscated_int {
    volatile T stored_;

    // Build the full-width XOR mask at compile time.
    // if constexpr is C++17 and allowed by the project's C++17 baseline.
    static KC_FORCEINLINE constexpr T make_key() noexcept {
        if constexpr (sizeof(T) == 8) {
            return static_cast<T>(
                (static_cast<uint64_t>(Key2) << 32) |
                 static_cast<uint64_t>(Key));
        } else {
            return static_cast<T>(Key);
        }
    }

    static KC_FORCEINLINE T encode(T val) {
        return val ^ make_key();
    }

    static KC_FORCEINLINE T decode(T val) {
        return val ^ make_key();
    }

public:
    KC_FORCEINLINE obfuscated_int() : stored_(encode(T(0))) {}

    KC_FORCEINLINE obfuscated_int(T val) : stored_(encode(val)) {}

    KC_FORCEINLINE operator T() const {
        T tmp = stored_;
        compiler_barrier();
        return decode(tmp);
    }

    KC_FORCEINLINE obfuscated_int& operator=(T val) {
        stored_ = encode(val);
        return *this;
    }

    KC_FORCEINLINE obfuscated_int& operator+=(T val) {
        T cur = decode(stored_);
        stored_ = encode(cur + val);
        return *this;
    }

    KC_FORCEINLINE obfuscated_int& operator-=(T val) {
        T cur = decode(stored_);
        stored_ = encode(cur - val);
        return *this;
    }

    KC_FORCEINLINE obfuscated_int& operator*=(T val) {
        T cur = decode(stored_);
        stored_ = encode(cur * val);
        return *this;
    }

    KC_FORCEINLINE obfuscated_int& operator&=(T val) {
        T cur = decode(stored_);
        stored_ = encode(cur & val);
        return *this;
    }

    KC_FORCEINLINE obfuscated_int& operator|=(T val) {
        T cur = decode(stored_);
        stored_ = encode(cur | val);
        return *this;
    }

    KC_FORCEINLINE obfuscated_int& operator^=(T val) {
        T cur = decode(stored_);
        stored_ = encode(cur ^ val);
        return *this;
    }

    KC_FORCEINLINE obfuscated_int& operator++() {
        T cur = decode(stored_);
        stored_ = encode(cur + T(1));
        return *this;
    }

    KC_FORCEINLINE T operator++(int) {
        T cur = decode(stored_);
        stored_ = encode(cur + T(1));
        return cur;
    }

    KC_FORCEINLINE obfuscated_int& operator--() {
        T cur = decode(stored_);
        stored_ = encode(cur - T(1));
        return *this;
    }

    KC_FORCEINLINE T operator--(int) {
        T cur = decode(stored_);
        stored_ = encode(cur - T(1));
        return cur;
    }
};

// obfuscated pointer storage - XOR'd with compile-time key
// On x64, pointers are 64-bit. Key2 fills the high 32 bits of the mask so
// that the full 64-bit pointer value is obfuscated (not just the low half).
template<typename T, uint32_t Key, uint32_t Key2 = 0>
class obfuscated_ptr {
    volatile uintptr_t stored_;

    static KC_FORCEINLINE constexpr uintptr_t make_key() noexcept {
        if constexpr (sizeof(uintptr_t) == 8) {
            return (static_cast<uintptr_t>(Key2) << 32) |
                    static_cast<uintptr_t>(Key);
        } else {
            return static_cast<uintptr_t>(Key);
        }
    }

    static KC_FORCEINLINE uintptr_t encode(T val) {
        return reinterpret_cast<uintptr_t>(val) ^ make_key();
    }

    static KC_FORCEINLINE T decode(uintptr_t val) {
        return reinterpret_cast<T>(val ^ make_key());
    }

public:
    KC_FORCEINLINE obfuscated_ptr() : stored_(encode(nullptr)) {}

    KC_FORCEINLINE obfuscated_ptr(T val) : stored_(encode(val)) {}

    KC_FORCEINLINE operator T() const {
        uintptr_t tmp = stored_;
        compiler_barrier();
        return decode(tmp);
    }

    KC_FORCEINLINE obfuscated_ptr& operator=(T val) {
        stored_ = encode(val);
        return *this;
    }

    KC_FORCEINLINE auto operator*() const -> decltype(*static_cast<T>(nullptr)) {
        return *static_cast<T>(decode(stored_));
    }

    KC_FORCEINLINE T operator->() const {
        return decode(stored_);
    }

    KC_FORCEINLINE bool operator==(T other) const {
        return decode(stored_) == other;
    }

    KC_FORCEINLINE bool operator!=(T other) const {
        return decode(stored_) != other;
    }
};

// type dispatcher - picks int vs ptr implementation
// Key2 is forwarded to whichever specialization is chosen so that 64-bit
// integers and 64-bit pointers both receive a full-width XOR mask.
template<typename T, uint32_t Key, uint32_t Key2 = 0>
using obfuscated_value = kernelcloak::detail::conditional_t<
    kernelcloak::detail::is_pointer<T>::value,
    obfuscated_ptr<T, Key, Key2>,
    obfuscated_int<T, Key, Key2>
>;

} // namespace detail
} // namespace obfuscation
} // namespace kernelcloak

// Two independent seeds are derived so that 64-bit values get a genuine
// 64-bit XOR mask: Key covers bits [31:0], Key2 covers bits [63:32].
// Both seeds consume __COUNTER__ to stay unique per call site; the
// second seed uses different multipliers to avoid correlation with Key.
#define KC_INT(x) \
    ::kernelcloak::obfuscation::detail::obfuscated_value< \
        decltype(x), \
        static_cast<::kernelcloak::uint32_t>( \
            (__COUNTER__ + 1) * 0x45D9F3Bu ^ __LINE__ * 0x1B873593u \
        ), \
        static_cast<::kernelcloak::uint32_t>( \
            (__COUNTER__ + 1) * 0xCC9E2D51u ^ __LINE__ * 0x85EBCA6Bu \
        )>(x)

#else // KC_ENABLE_VALUE_OBFUSCATION disabled

#define KC_INT(x) (x)

#endif // KC_ENABLE_VALUE_OBFUSCATION
