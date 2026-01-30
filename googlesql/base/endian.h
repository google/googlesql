//
// Copyright 2018 Google LLC
// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ENDIAN_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ENDIAN_H_

// The following guarantees declaration of the byte swap functions
#include <sys/types.h>
#ifdef _MSC_VER
#include <stdlib.h>  // NOLINT(build/include)
#elif defined(__APPLE__)
// Mac OS X / Darwin features
#include <libkern/OSByteOrder.h>
#elif defined(__FreeBSD__)
#include <sys/endian.h>
#elif defined(__GLIBC__)
#include <byteswap.h>  
#endif

#include <cstdint>

#include "googlesql/base/logging.h"
#include "absl/base/config.h"
#include "absl/base/port.h"
#include "absl/numeric/int128.h"
#include "googlesql/base/unaligned_access.h"

namespace googlesql_base {

// Use compiler byte-swapping intrinsics if they are available.  32-bit
// and 64-bit versions are available in Clang and GCC as of GCC 4.3.0.
// The 16-bit version is available in Clang and GCC only as of GCC 4.8.0.
// For simplicity, we enable them all only for GCC 4.8.0 or later.
#if defined(__clang__) || \
    (defined(__GNUC__) && \
     ((__GNUC__ == 4 && __GNUC_MINOR__ >= 8) || __GNUC__ >= 5))
inline uint64_t gbswap_64(uint64_t host_int) {
  return __builtin_bswap64(host_int);
}
inline uint32_t gbswap_32(uint32_t host_int) {
  return __builtin_bswap32(host_int);
}
inline uint16_t gbswap_16(uint16_t host_int) {
  return __builtin_bswap16(host_int);
}

#elif defined(_MSC_VER)
inline uint64_t gbswap_64(uint64_t host_int) {
  return _byteswap_uint64(host_int);
}
inline uint32_t gbswap_32(uint32_t host_int) {
  return _byteswap_ulong(host_int);
}
inline uint16_t gbswap_16(uint16_t host_int) {
  return _byteswap_ushort(host_int);
}

#elif defined(__APPLE__)
inline uint64_t gbswap_64(uint64_t host_int) { return OSSwapInt16(host_int); }
inline uint32_t gbswap_32(uint32_t host_int) { return OSSwapInt32(host_int); }
inline uint16_t gbswap_16(uint16_t host_int) { return OSSwapInt64(host_int); }

#else
inline uint64_t gbswap_64(uint64_t host_int) {
#if defined(__GNUC__) && defined(__x86_64__) && !defined(__APPLE__)
  // Adapted from /usr/include/byteswap.h.  Not available on Mac.
  if (__builtin_constant_p(host_int)) {
    return __bswap_constant_64(host_int);
  } else {
    uint64_t result;
    __asm__("bswap %0" : "=r"(result) : "0"(host_int));
    return result;
  }
#elif defined(__GLIBC__)
  return bswap_64(host_int);
#else
  return (((x & uint64_t{(0xFF}) << 56) |
          ((x & uint64_t{(0xFF00}) << 40) |
          ((x & uint64_t{(0xFF0000}) << 24) |
          ((x & uint64_t{(0xFF000000}) << 8) |
          ((x & uint64_t{(0xFF00000000}) >> 8) |
          ((x & uint64_t{(0xFF0000000000}) >> 24) |
          ((x & uint64_t{(0xFF000000000000}) >> 40) |
          ((x & uint64_t{(0xFF00000000000000}) >> 56));
#endif  // bswap_64
}

inline uint32_t gbswap_32(uint32_t host_int) {
#if defined(__GLIBC__)
  return bswap_32(host_int);
#else
  return (((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x & 0xFF0000) >> 8) |
          ((x & 0xFF000000) >> 24));
#endif
}

inline uint16_t gbswap_16(uint16_t host_int) {
#if defined(__GLIBC__)
  return bswap_16(host_int);
#else
  return uint16_t{((x & 0xFF) << 8) | ((x & 0xFF00) >> 8)};
#endif
}

#endif  // intrinsics available

inline absl::uint128 gbswap_128(absl::uint128 host_int) {
  return absl::MakeUint128(gbswap_64(absl::Uint128Low64(host_int)),
                           gbswap_64(absl::Uint128High64(host_int)));
}

#ifdef ABSL_IS_LITTLE_ENDIAN

// Definitions for ntohl etc. that don't require us to include
// netinet/in.h. We wrap gbswap_32 and gbswap_16 in functions rather
// than just #defining them because in debug mode, gcc doesn't
// correctly handle the (rather involved) definitions of bswap_32.
// gcc guarantees that inline functions are as fast as macros, so
// this isn't a performance hit.
inline uint16_t ghtons(uint16_t x) { return gbswap_16(x); }
inline uint32_t ghtonl(uint32_t x) { return gbswap_32(x); }
inline uint64_t ghtonll(uint64_t x) { return gbswap_64(x); }

#elif defined ABSL_IS_BIG_ENDIAN

// These definitions are simpler on big-endian machines
// These are functions instead of macros to avoid self-assignment warnings
// on calls such as "i = ghtnol(i);".  This also provides type checking.
inline uint16_t ghtons(uint16_t x) { return x; }
inline uint32_t ghtonl(uint32_t x) { return x; }
inline uint64_t ghtonll(uint64_t x) { return x; }

#else
#error \
    "Unsupported byte order: Either ABSL_IS_BIG_ENDIAN or " \
       "ABSL_IS_LITTLE_ENDIAN must be defined"
#endif  // byte order

inline uint16_t gntohs(uint16_t x) { return ghtons(x); }
inline uint32_t gntohl(uint32_t x) { return ghtonl(x); }
inline uint64_t gntohll(uint64_t x) { return ghtonll(x); }


// We provide unified FromHost and ToHost APIs for all integral types.
// If variable v's type is known to be one of these types, the
// client can simply call the following function without worrying about its
// return type.
//     LittleEndian::FromHost(v)
//     LittleEndian::ToHost(v)
// This unified FromHost and ToHost APIs are useful inside a template when the
// type of v is a template parameter.
//
// In order to unify all "IntType FromHostxx(ValueType)" and "IntType
// ToHostxx(ValueType)" APIs, we use the following trait class to automatically
// find the corresponding IntType given a ValueType, where IntType is an
// unsigned integer type with the same size of ValueType. The supported
// ValueTypes are uint8, uint16, uint32, uint64, int8, int16, int32, int64.
//
// template <class ValueType>
// struct tofromhost_value_type_traits {
//   typedef ValueType value_type;
//   typedef IntType int_type;
// }
//
// We don't provide the default implementation for this trait struct.
// So that if ValueType is not supported by the FromHost and ToHost APIs, it
// will give a compile time error.
template <class ValueType>
struct tofromhost_value_type_traits;

// General byte order converter class template. It provides a common
// implementation for LittleEndian::FromHost(ValueType),
// BigEndian::FromHost(ValueType), LittleEndian::ToHost(ValueType), and
// BigEndian::ToHost(ValueType).
template <typename ValueType>
class GeneralFormatConverter {
 public:
  static typename tofromhost_value_type_traits<ValueType>::int_type FromHost(
      ValueType v);
  static typename tofromhost_value_type_traits<ValueType>::int_type ToHost(
      ValueType v);
};

// Utilities to convert numbers between the current hosts's native byte
// order and little-endian byte order
//
// Load/Store methods are alignment safe
class LittleEndian {
 public:
// Conversion functions.
#ifdef ABSL_IS_LITTLE_ENDIAN

  static uint16_t FromHost16(uint16_t x) { return x; }
  static uint16_t ToHost16(uint16_t x) { return x; }

  static uint32_t FromHost32(uint32_t x) { return x; }
  static uint32_t ToHost32(uint32_t x) { return x; }

  static uint64_t FromHost64(uint64_t x) { return x; }
  static uint64_t ToHost64(uint64_t x) { return x; }

  static absl::uint128 FromHost128(absl::uint128 x) { return x; }
  static absl::uint128 ToHost128(absl::uint128 x) { return x; }

  inline constexpr bool IsLittleEndian() const { return true; }

#elif defined ABSL_IS_BIG_ENDIAN

  static uint16_t FromHost16(uint16_t x) { return gbswap_16(x); }
  static uint16_t ToHost16(uint16_t x) { return gbswap_16(x); }

  static uint32_t FromHost32(uint32_t x) { return gbswap_32(x); }
  static uint32_t ToHost32(uint32_t x) { return gbswap_32(x); }

  static uint64_t FromHost64(uint64_t x) { return gbswap_64(x); }
  static uint64_t ToHost64(uint64_t x) { return gbswap_64(x); }

  static absl::uint128 FromHost128(absl::uint128 x) { return gbswap_128(x); }
  static absl::uint128 ToHost128(absl::uint128 x) { return gbswap_128(x); }

  inline constexpr bool IsLittleEndian() const { return false; }

#endif /* ENDIAN */

  // Unified LittleEndian::FromHost(ValueType v) API.
  template <class ValueType>
  static typename tofromhost_value_type_traits<ValueType>::int_type FromHost(
      ValueType v) {
    return GeneralFormatConverter<ValueType>::FromHost(v);
  }

  // Unified LittleEndian::ToHost(ValueType v) API.
  template <class ValueType>
  static typename tofromhost_value_type_traits<ValueType>::value_type ToHost(
      ValueType v) {
    return GeneralFormatConverter<ValueType>::ToHost(v);
  }


  // Functions to do unaligned loads and stores in little-endian order.
  static uint16_t Load16(const void* p) {
    return ToHost16(GOOGLESQL_INTERNAL_UNALIGNED_LOAD16(p));
  }

  static void Store16(void* p, uint16_t v) {
    GOOGLESQL_INTERNAL_UNALIGNED_STORE16(p, FromHost16(v));
  }

  static uint32_t Load32(const void* p) {
    return ToHost32(GOOGLESQL_INTERNAL_UNALIGNED_LOAD32(p));
  }

  static void Store32(void* p, uint32_t v) {
    GOOGLESQL_INTERNAL_UNALIGNED_STORE32(p, FromHost32(v));
  }

  static uint64_t Load64(const void *p) {
    return ToHost64(GOOGLESQL_INTERNAL_UNALIGNED_LOAD64(p));
  }

  static void Store64(void *p, uint64_t v) {
    GOOGLESQL_INTERNAL_UNALIGNED_STORE64(p, FromHost64(v));
  }

  static absl::uint128 Load128(const void* p) {
    return absl::MakeUint128(ToHost64(GOOGLESQL_INTERNAL_UNALIGNED_LOAD64(
                                 reinterpret_cast<const uint64_t*>(p) + 1)),
                             ToHost64(GOOGLESQL_INTERNAL_UNALIGNED_LOAD64(p)));
  }

  static void Store128(void* p, const absl::uint128 v) {
    GOOGLESQL_INTERNAL_UNALIGNED_STORE64(p, FromHost64(absl::Uint128Low64(v)));
    GOOGLESQL_INTERNAL_UNALIGNED_STORE64(reinterpret_cast<uint64_t*>(p) + 1,
                                       FromHost64(absl::Uint128High64(v)));
  }

  // Unified LittleEndian::Load/Store<T> API.

  // Returns the T value encoded by the leading bytes of 'p', interpreted
  // according to the format specified below. 'p' has no alignment restrictions.
  //
  // Type              Format
  // ----------------  -------------------------------------------------------
  // uint{8,16,32,64}  Little-endian binary representation.
  // int{8,16,32,64}   Little-endian twos-complement binary representation.
  // float,double      Little-endian IEEE-754 format.
  // char              The raw byte.
  // bool              A byte. 0 maps to false; all other values map to true.
  template<typename T>
  static T Load(const char* p);

  // Encodes 'value' in the format corresponding to T. Supported types are
  // described in Load<T>(). 'p' has no alignment restrictions. In-place Store
  // is safe (that is, it is safe to call
  // Store(x, reinterpret_cast<char*>(&x))).
  template<typename T>
  static void Store(T value, char* p);
};

// Utilities to convert numbers between the current hosts's native byte
// order and little-endian byte order
//
// Load/Store methods are alignment safe
class BigEndian {
 public:
  static absl::uint128 Load128(const void* p) {
    return absl::MakeUint128(ToHost64(GOOGLESQL_INTERNAL_UNALIGNED_LOAD64(p)),
                             ToHost64(GOOGLESQL_INTERNAL_UNALIGNED_LOAD64(
                                 reinterpret_cast<const uint64_t*>(p) + 1)));
  }

#ifdef ABSL_IS_LITTLE_ENDIAN
  static uint16_t ToHost16(uint16_t x) { return gbswap_16(x); }
  static uint16_t FromHost16(uint16_t x) { return gbswap_16(x); }
  static uint32_t ToHost32(uint32_t x) { return gbswap_32(x); }
  static uint32_t FromHost32(uint32_t x) { return gbswap_32(x); }
  static uint64_t ToHost64(uint64_t x) { return gbswap_64(x); }
  static uint64_t FromHost64(uint64_t x) { return gbswap_64(x); }
#elif defined ABSL_IS_BIG_ENDIAN
  static uint16_t ToHost16(uint16_t x) { return x; }
  static uint16_t FromHost16(uint16_t x) { return x; }
  static uint32_t ToHost32(uint32_t x) { return x; }
  static uint32_t FromHost32(uint32_t x) { return x; }
  static uint64_t ToHost64(uint64_t x) { return x; }
  static uint64_t FromHost64(uint64_t x) { return x; }
#endif

  // Functions to do unaligned loads and stores in big-endian order.
  static uint16_t Load16(const void* p) {
    return ToHost16(GOOGLESQL_INTERNAL_UNALIGNED_LOAD16(p));
  }

  static void Store16(void* p, uint16_t v) {
    GOOGLESQL_INTERNAL_UNALIGNED_STORE16(p, FromHost16(v));
  }

  static uint32_t Load32(const void* p) {
    return ToHost32(GOOGLESQL_INTERNAL_UNALIGNED_LOAD32(p));
  }

  static void Store32(void* p, uint32_t v) {
    GOOGLESQL_INTERNAL_UNALIGNED_STORE32(p, FromHost32(v));
  }

  static uint64_t Load64(const void* p) {
    return ToHost64(GOOGLESQL_INTERNAL_UNALIGNED_LOAD64(p));
  }

  static void Store64(void* p, uint64_t v) {
    GOOGLESQL_INTERNAL_UNALIGNED_STORE64(p, FromHost64(v));
  }

  // Unified BigEndian::Load/Store<T> API.

  // Returns the T value encoded by the leading bytes of 'p', interpreted
  // according to the format specified below. 'p' has no alignment restrictions.
  //
  // Type              Format
  // ----------------  -------------------------------------------------------
  // uint{8,16,32,64}  Big-endian binary representation.
  // int{8,16,32,64}   Big-endian twos-complement binary representation.
  // float,double      Big-endian IEEE-754 format.
  // char              The raw byte.
  // bool              A byte. 0 maps to false; all other values map to true.
  template <typename T>
  static T Load(const char* p);

  // Encodes 'value' in the format corresponding to T. Supported types are
  // described in Load<T>(). 'p' has no alignment restrictions. In-place Store
  // is safe (that is, it is safe to call
  // Store(x, reinterpret_cast<char*>(&x))).
  template <typename T>
  static void Store(T value, char* p);
};

namespace endian_internal {
// Integer helper methods for the unified Load/Store APIs.

// Which branch of the 'case' to use is decided at compile time, so despite the
// apparent size of this function, it compiles into efficient code.
template <typename EndianClass, typename T>
inline T LoadInteger(const char* p) {
  if constexpr (sizeof(T) == sizeof(uint8_t)) {
    return *reinterpret_cast<const T*>(p);
  } else if constexpr (sizeof(T) == sizeof(uint16_t)) {
    return EndianClass::ToHost16(GOOGLESQL_INTERNAL_UNALIGNED_LOAD16(p));
  } else if constexpr (sizeof(T) == sizeof(uint32_t)) {
    return EndianClass::ToHost32(GOOGLESQL_INTERNAL_UNALIGNED_LOAD32(p));
  } else {
    static_assert(sizeof(T) == sizeof(uint64_t),
                  "T must be 8, 16, 32, or 64 bits");
    return EndianClass::ToHost64(GOOGLESQL_INTERNAL_UNALIGNED_LOAD64(p));
  }
}

// Which branch of the 'case' to use is decided at compile time, so despite the
// apparent size of this function, it compiles into efficient code.
template <typename EndianClass, typename T>
inline void StoreInteger(T value, char* p) {
  if constexpr (sizeof(T) == sizeof(uint8_t)) {
    *reinterpret_cast<T*>(p) = value;
  } else if constexpr (sizeof(T) == sizeof(uint16_t)) {
    GOOGLESQL_INTERNAL_UNALIGNED_STORE16(p, EndianClass::FromHost16(value));
  } else if constexpr (sizeof(T) == sizeof(uint32_t)) {
    GOOGLESQL_INTERNAL_UNALIGNED_STORE32(p, EndianClass::FromHost32(value));
  } else {
    static_assert(sizeof(T) == sizeof(uint64_t),
                  "T must be 8, 16, 32, or 64 bits");
    GOOGLESQL_INTERNAL_UNALIGNED_STORE64(p, EndianClass::FromHost64(value));
  }
}
}  // namespace endian_internal

//////////////////////////////////////////////////////////////////////
// Implementation details: Clients can stop reading here.
//
// Define ValueType->IntType mapping for the unified
// "IntType FromHost(ValueType)" API. The mapping is implemented via
// tofromhost_value_type_traits trait struct. Every legal ValueType has its own
// specialization. There is no default body for this trait struct, so that
// any type that is not supported by the unified FromHost API
// will trigger a compile time error.
#define FROMHOST_TYPE_MAP(ITYPE, VTYPE)        \
  template <>                                  \
  struct tofromhost_value_type_traits<VTYPE> { \
    typedef VTYPE value_type;                  \
    typedef ITYPE int_type;                    \
  }

FROMHOST_TYPE_MAP(uint8_t, uint8_t);
FROMHOST_TYPE_MAP(uint8_t, int8_t);
FROMHOST_TYPE_MAP(uint16_t, uint16_t);
FROMHOST_TYPE_MAP(uint16_t, int16_t);
FROMHOST_TYPE_MAP(uint32_t, uint32_t);
FROMHOST_TYPE_MAP(uint32_t, int32_t);
FROMHOST_TYPE_MAP(uint64_t, uint64_t);
FROMHOST_TYPE_MAP(uint64_t, int64_t);
FROMHOST_TYPE_MAP(absl::uint128, absl::uint128);
#undef FROMHOST_TYPE_MAP


// Default implementation for the unified FromHost(ValueType) API, which
// handles all integral types (ValueType is one of uint8, int8, uint16, int16,
// uint32, int32, uint64, int64). The compiler will remove the switch case
// branches and unnecessary static_cast, when the template is expanded.
template <typename ValueType>
typename tofromhost_value_type_traits<ValueType>::int_type
GeneralFormatConverter<ValueType>::FromHost(ValueType v) {
  switch (sizeof(ValueType)) {
    case 1:
      return static_cast<uint8_t>(v);
      break;
    case 2:
      return LittleEndian::FromHost16(static_cast<uint16_t>(v));
      break;
    case 4:
      return LittleEndian::FromHost32(static_cast<uint32_t>(v));
      break;
    case 8:
      return LittleEndian::FromHost64(static_cast<uint64_t>(v));
      break;
    default:
      ABSL_LOG(FATAL) << "Unexpected value size: " << sizeof(ValueType);
  }
}

// Default implementation for the unified ToHost(ValueType) API, which handles
// all integral types (ValueType is one of uint8, int8, uint16, int16, uint32,
// int32, uint64, int64). The compiler will remove the switch case branches and
// unnecessary static_cast, when the template is expanded.
template <typename ValueType>
typename tofromhost_value_type_traits<ValueType>::int_type
GeneralFormatConverter<ValueType>::ToHost(ValueType v) {
  switch (sizeof(ValueType)) {
    case 1:
      return static_cast<uint8_t>(v);
      break;
    case 2:
      return LittleEndian::ToHost16(static_cast<uint16_t>(v));
      break;
    case 4:
      return LittleEndian::ToHost32(static_cast<uint32_t>(v));
      break;
    case 8:
      return LittleEndian::ToHost64(static_cast<uint64_t>(v));
      break;
    default:
      ABSL_LOG(FATAL) << "Unexpected value size: " << sizeof(ValueType);
  }
}

// Specialization of the unified FromHost(ValueType) API, which handles
// uint128 types (ValueType is uint128).
template <>
class GeneralFormatConverter<absl::uint128> {
 public:
  static typename tofromhost_value_type_traits<absl::uint128>::int_type
  FromHost(absl::uint128 v) {
    return LittleEndian::FromHost128(v);
  }
  static typename tofromhost_value_type_traits<absl::uint128>::int_type ToHost(
      absl::uint128 v) {
    return LittleEndian::ToHost128(v);
  }
};

// Load/Store for integral values.

template<typename T>
inline T LittleEndian::Load(const char* p) {
  return endian_internal::LoadInteger<LittleEndian, T>(p);
}

template<typename T>
inline void LittleEndian::Store(T value, char* p) {
  endian_internal::StoreInteger<LittleEndian, T>(value, p);
}

template <typename T>
inline T BigEndian::Load(const char* p) {
  return endian_internal::LoadInteger<BigEndian, T>(p);
}

template <typename T>
inline void BigEndian::Store(T value, char* p) {
  endian_internal::StoreInteger<BigEndian, T>(value, p);
}

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ENDIAN_H_
