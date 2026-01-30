//
// Copyright 2019 Google LLC
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

#ifndef GOOGLESQL_PUBLIC_CAST_H_
#define GOOGLESQL_PUBLIC_CAST_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "googlesql/base/status_builder.h"

// The full specification for GoogleSQL casting and coercion is at:
//   (broken link)

namespace googlesql {

class Function;
class LanguageOptions;

// Identifies the conditions where casting/coercion from type <A> to type <B>
// is valid.  EXPLICIT means coercion can only be performed if explicitly
// present in the SQL query, i.e. CAST(column AS INT32).
// EXPLICIT_OR_LITERAL_OR_PARAMETER allows explicit casts, implicit coercion
// of parameters, and implicit literal coercion if it can be done successfully
// (for example, coercing an INT64 to INT32 without overflow).
// EXPLICIT_OR_LITERAL allows explicit casts and implicit literal coercion.
// IMPLICIT means type <A> can be implicitly coerced to type <B> when
// required, and also implies explicit casting is allowed.
enum class CastFunctionType {
  IMPLICIT = 1,  // Implies EXPLICIT.
  EXPLICIT_OR_LITERAL_OR_PARAMETER = 2,
  EXPLICIT_OR_LITERAL = 3,
  EXPLICIT = 4,
};

// Helper functions identifying if implicit, explicit, literal, or
// parameter coercion is supported.
bool SupportsImplicitCoercion(CastFunctionType type);
bool SupportsLiteralCoercion(CastFunctionType type);
bool SupportsParameterCoercion(CastFunctionType type);
bool SupportsExplicitCast(CastFunctionType type);

// Properties associated with casting from one type to another.
// A CastFunctionProperty is associated with a TypeKindPair in the CastHashMap
// defined below.
struct CastFunctionProperty {
  CastFunctionProperty(CastFunctionType cast_type, int coercion_cost)
      : type(cast_type), cost(coercion_cost) {}

  bool is_implicit() const { return type == CastFunctionType::IMPLICIT; }

  CastFunctionType type;
  int cost;  // Cost to cast from one type to another.
};

using CastHashMap = absl::flat_hash_map<TypeKindPair, CastFunctionProperty>;


// The CastFormatMap maps a TypeKindPair to a validation function of the format
// string. If a TypeKindPair exists in the map, it means that a format string is
// allowed for the type cast represented by the TypeKindPair. The corresponding
// validation function will be called to validate the format string at analysis
// time.
typedef absl::Status (*FormatValidationFunc)(absl::string_view format);
using CastFormatMap = absl::flat_hash_map<TypeKindPair, FormatValidationFunc>;

// Returns <from_value> casted to <to_type>. Returns an error if the types are
// incompatible or the value cannot be cast successfully to <to_type>.
//
// If cast involves extended types, the Catalog that defines a conversion for
// these extended types should be provided in <catalog>. CastValue will return
// an error if it encounters an extended type while <catalog> argument is NULL.
//
// Successful casting depends on the input value, which fails if it violates
// the range bounds of <to_type> (e.g. casting a large INT64 value to
// INT32), the result value is invalid for the target domain (e.g. casting
// a non-UTF8 BYTES value to STRING), etc.
//
// A Value always successfully casts to the same exact type (even for
// non-simple types).
//
// Casting float/double to integer rounds the result.
//
// Casting finer-granularity timestamps truncate to coarser granularity types
// (i.e. milliseconds to seconds).
//
// When casting between PROTO and BYTES Types, no validation is performed when
// producing the coerced Value.  In the GoogleSQL Value system, a Proto is an
// uninterpreted descriptor/bytes pair.  This cast behavior is not dictated
// by GoogleSQL, and conforming implementations are allowed to perform some
// validation.
//
// Casting behavior between string/date and timestamp uses a time zone for
// the conversion, defined as follows:
//   1) Timestamp to string/date
//      The timestamp is interpreted as of the <default_timezone>
//      when producing the string/date.
//   2) String to Timestamp
//      If the timezone is present in the string then it is used, otherwise
//      the <default_timezone> is used.
//   3) Date to Timestamp
//      The date is interpreted as of midnight in the <default_timezone>.
// If canonicalize_zero is true, the sign on a signed zero is removed when
// converting numeric type to string.
// TODO : remove canonicalize_zero parameter when all
// engines have rolled out this new behavior.
absl::StatusOr<Value> CastValue(const Value& from_value,
                                absl::TimeZone default_timezone,
                                const LanguageOptions& language_options,
                                const Type* to_type, Catalog* catalog = nullptr,
                                bool canonicalize_zero = false);

// Same as the previous method, but includes <format>, which is the format
// string used in the cast.
absl::StatusOr<Value> CastValue(const Value& from_value,
                                absl::TimeZone default_timezone,
                                const LanguageOptions& language_options,
                                const Type* to_type,
                                const std::optional<std::string>& format,
                                Catalog* catalog = nullptr,
                                bool canonicalize_zero = false);

// DEPRECATED name for CastValue()
inline absl::StatusOr<Value> CastStatusOrValue(
    const Value& from_value, absl::TimeZone default_timezone,
    const LanguageOptions& language_options, const Type* to_type,
    bool canonicalize_zero = false) {
  return CastValue(from_value, default_timezone, language_options, to_type,
                   nullptr, canonicalize_zero);
}

class ExtendedCompositeCastEvaluator;

namespace internal {  //   For internal use only

// Same as CastValue except assumes that the caller has already checked that
// casting between these types is valid.
//
// REQUIRES: The requested cast is valid according to Coercer.
// REQUIRES: If cast involves extended types, their conversion function should
// be provided in extended_conversion.
absl::StatusOr<Value> CastValueWithoutTypeValidation(
    const Value& from_value, absl::TimeZone default_timezone,
    std::optional<absl::Time> current_timestamp,
    const LanguageOptions& language_options, const Type* to_type,
    const std::optional<std::string>& format,
    const std::optional<std::string>& time_zone,
    const ExtendedCompositeCastEvaluator* extended_conversion_evaluator,
    bool canonicalize_zero);

// Returns a hash map with TypeKindPair as key, and CastFunctionProperty as
// value.  This identifies whether the (from, to) cast pairs in the key are
// allowed explicitly, implicitly, etc., and what the cost is for the cast.
// If a (from, to) pair is not in the map, then that cast is not allowed.
const CastHashMap& GetGoogleSQLCasts();

// Returns a hash map with TypeKindPair as key, and FormatValidationFunc as
// value. If a (from, to) pair is in the map, then a format string is allowed
// for that cast. The corresponding validation function will be called to
// validate the format string at analysis time.
const CastFormatMap& GetCastFormatMap();

}  // namespace internal

// ConversionEvaluator stores information necessary to execute a conversion.
// This includes a conversion function that implements a conversion.
// Function must have a FunctionSignature of the following form:
// {Type=from_type, num_occurrences=1} => to_type.
class ConversionEvaluator {
 public:
  // Creates a ConversionEvaluator. Returns error if any of provided arguments
  // is NULL or <from_type> Equals() to <to_type>.
  static absl::StatusOr<ConversionEvaluator> Create(const Type* from_type,
                                                    const Type* to_type,
                                                    const Function* function);

  // Explicitly copyable and assignable.
  ConversionEvaluator(const ConversionEvaluator& other) = default;
  ConversionEvaluator& operator=(const ConversionEvaluator& other) = default;

  // Returns the source type of the conversion.
  const Type* from_type() const { return from_type_; }

  // Returns the result type of the conversion.
  const Type* to_type() const { return to_type_; }

  // Returns the function that implements this conversion.
  const Function* function() const { return function_; }

  // Returns the signature for this conversion. Calling the Function above's
  // FunctionEvaluatorFactory with this signature should provide an appropriate
  // evaluator.
  FunctionSignature function_signature() const {
    return GetFunctionSignature(from_type_, to_type_);
  }

  // Converts the given Value using conversion function. Requires Value's type
  // to be <from_type_>. If conversion succeeds, the type of returned value will
  // be <to_type_>.
  absl::StatusOr<Value> Eval(const Value& from_value) const;

  // Returns a concrete signature that can be used with conversion function to
  // execute a conversion from <from_type> to <to_type>. The form of the
  // signature is: {Type=<from_type>, num_occurrences=1} => <to_type>.
  static FunctionSignature GetFunctionSignature(const Type* from_type,
                                                const Type* to_type);

 private:
  friend class Conversion;

  // Constructs an ConversionEvaluator. Should be called only from Create.
  ConversionEvaluator(const Type* from_type, const Type* to_type,
                      const Function* function)
      : from_type_(from_type), to_type_(to_type), function_(function) {}

  // Constructs an 'invalid' conversion evaluator to use in invalid Conversion
  // objects. Should be used only from Conversion class.
  ConversionEvaluator()
      : from_type_(nullptr), to_type_(nullptr), function_(nullptr) {}

  // Returns true, if the object represents a valid conversion function and
  // false if it was created using constructor above.
  bool is_valid() const {
    return from_type_ != nullptr && to_type_ != nullptr && function_ != nullptr;
  }

  const Type* from_type_;  // Source type of conversion.
  const Type* to_type_;    // Destination (result) type of conversion.

  const Function* function_;  // Function that implements a conversion.
};

// Conversion describes a cast or coercion provided to the GoogleSQL resolver by
// a Catalog. Conversion can be either stored in a Catalog or a Catalog can
// dynamically construct it based on a mapping between two specific types.
// TODO: support type parameters in Conversion evaluator.
class Conversion {
 public:
  // Creates a Conversion. Returns an error if provided arguments are invalid:
  // e.g. any of arguments is NULL.
  static absl::StatusOr<Conversion> Create(
      const Type* from_type, const Type* to_type, const Function* function,
      const CastFunctionProperty& property);
  static absl::StatusOr<Conversion> Create(
      const ConversionEvaluator& evaluator,
      const CastFunctionProperty& property);

  // Explicitly copyable and assignable.
  Conversion(const Conversion& other) = default;
  Conversion& operator=(const Conversion& other) = default;

  // Returns the evaluator capable of executing this conversion. Requires:
  // is_valid().
  const ConversionEvaluator& evaluator() const {
    return validated().evaluator_;
  }

  // Returns the property of this conversion. Requires: is_valid().
  const CastFunctionProperty& property() const {
    return validated().cast_function_property_;
  }

  // Returns the source type of the conversion. Requires: is_valid().
  const Type* from_type() const { return evaluator().from_type(); }

  // Returns the destination type of the conversion. Requires: is_valid().
  const Type* to_type() const { return evaluator().to_type(); }

  // Returns the function that implements this conversion. Requires: is_valid().
  const Function* function() const { return evaluator().function(); }

  // Checks whether conversion matches options used by Catalog API to search
  // for conversions.
  bool IsMatch(const Catalog::FindConversionOptions& options) const;

  // Creates a conversion that is marked as invalid. Conversion is typically
  // passed by value and "Invalid" can be used to designate an uninitialized
  // conversion.
  static Conversion Invalid() { return Conversion(); }

  // Returns true, if the object represents a valid conversion and false if it
  // was created using Conversion::Invalid().
  bool is_valid() const { return evaluator_.is_valid(); }

 private:
  Conversion(const ConversionEvaluator& evaluator,
             const CastFunctionProperty& property)
      : evaluator_(evaluator), cast_function_property_(property) {}

  // Constructs an 'invalid' conversion.
  Conversion()
      : evaluator_(), cast_function_property_(CastFunctionType::EXPLICIT, 0) {}

  // Returns itself if this Conversion is valid. Crashes otherwise.
  const Conversion& validated() const {
    ABSL_CHECK(is_valid()) << "Attempt to access properties of invalid Conversion";
    return *this;
  }

  ConversionEvaluator evaluator_;
  CastFunctionProperty cast_function_property_;
};

// ExtendedCompositeCastEvaluator is a list of conversion evaluators. To execute
// a conversion, ExtendedCompositeCastEvaluator finds a matching conversion in
// the list by checking the source and destination types for equality.
class ExtendedCompositeCastEvaluator {
 public:
  // Explicitly copyable and moveable.
  ExtendedCompositeCastEvaluator(const ExtendedCompositeCastEvaluator&) =
      default;
  ExtendedCompositeCastEvaluator& operator=(
      const ExtendedCompositeCastEvaluator&) = default;
  ExtendedCompositeCastEvaluator(ExtendedCompositeCastEvaluator&&) = default;
  ExtendedCompositeCastEvaluator& operator=(ExtendedCompositeCastEvaluator&&) =
      default;

  explicit ExtendedCompositeCastEvaluator(
      std::vector<ConversionEvaluator> evaluators)
      : evaluators_(std::move(evaluators)) {}

  static ExtendedCompositeCastEvaluator Invalid() {
    return ExtendedCompositeCastEvaluator();
  }

  bool is_valid() const { return !evaluators_.empty(); }

  absl::StatusOr<Value> Eval(const Value& from_value,
                             const Type* to_type) const;

  const std::vector<ConversionEvaluator>& evaluators() const {
    return evaluators_;
  }

 private:
  ExtendedCompositeCastEvaluator() {}

  std::vector<ConversionEvaluator> evaluators_;
};

// Represents pair of Type objects used in conversion. Supports hashing and
// equality. The latter is done based on Type::Equals.
class ConversionTypePair {
 public:
  ConversionTypePair(const Type* from_type, const Type* to_type)
      : from_type_(from_type), to_type_(to_type) {
    ABSL_DCHECK(to_type);
    ABSL_DCHECK(from_type);
  }

  // Explicitly copyable and assignable.
  ConversionTypePair(const ConversionTypePair& other) = default;
  ConversionTypePair& operator=(const ConversionTypePair& other) = default;

  const Type* from_type() const { return from_type_; }
  const Type* to_type() const { return to_type_; }

  // TODO: Add tests for type pair hashing.
  template <typename H>
  friend H AbslHashValue(H h, const ConversionTypePair& pair) {
    return H::combine(std::move(h), *pair.from_type(), *pair.to_type());
  }

  bool operator==(const ConversionTypePair& other) const {
    return from_type()->Equals(other.from_type()) &&
           to_type()->Equals(other.to_type());
  }

  bool operator!=(const ConversionTypePair& other) const {
    return !(*this == other);
  }

 private:
  const Type* from_type_;
  const Type* to_type_;
};

// Helper class that serves to enable usage of Conversion and
// ConversionEvaluator classes as an elements of absl::flat_hash_set or
// absl::flat_hash_map. The elements are treated as equal if corresponding
// conversion type pairs match: a.from_type()->Equals(b.from_type()) &&
// a.to_type()->Equals(b.to_type()).
//
// Usage example:
//  absl::flat_hash_set<
//                ConversionEvaluator,
//                ConversionTypePairOperations<ConversionEvaluator>::Hash,
//                ConversionTypePairOperations<ConversionEvaluator>::Eq> set;
template <typename ConversionT>
struct ConversionTypePairOperations {
 public:
  struct Hash {
    size_t operator()(const ConversionT& conversion) const {
      return absl::Hash<ConversionTypePair>()(
          GetConversionTypePair(conversion));
    }
  };

  struct Eq {
    bool operator()(const ConversionT& lhs, const ConversionT& rhs) const {
      return GetConversionTypePair(lhs) == GetConversionTypePair(rhs);
    }
  };

 private:
  static ConversionTypePair GetConversionTypePair(
      const ConversionT& conversion) {
    return ConversionTypePair(conversion.from_type(), conversion.to_type());
  }
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_CAST_H_
