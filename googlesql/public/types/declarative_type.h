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

#ifndef GOOGLESQL_PUBLIC_TYPES_DECLARATIVE_TYPE_H_
#define GOOGLESQL_PUBLIC_TYPES_DECLARATIVE_TYPE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/public/language_options.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

class DeclarativeType;
class TypeParameters;
class TypeParameterValue;

// Uniquely identifies a declaratively-defined type.
// Any two instances with the same DeclarativeTypeId are identical and
// Type::Equals() must return true, and their behaviors must be identical.
//
// TypeFactory currently caches DeclarativeTypes based on DeclarativeTypeId,
// and any repeated calls to MakeDeclarativeType() with the same
// DeclarativeTypeId return the same Type* and GOOGLESQL_RET_CHECK that the descriptors
// are identical.
struct DeclarativeTypeId {
  static constexpr absl::string_view kGoogleSqlNamespace = "GoogleSQL";

  // For GoogleSQL built-in types, the namespace is kGoogleSqlNamespace.
  std::string name_space;
  // An ID that uniquely identifies the type within the namespace.
  // *DO NOT* use this alone for identity checks. The whole
  // DeclarativeTypeId needs to be considered.
  std::string local_id;
  // An optional version ID.
  // This can be used by systems managing user-defined types (UDTs) to
  // distinguish different iterations of a type (e.g., if a type is dropped
  // and re-created under the same name/handle).
  // For GoogleSQL built-in types, this should always be empty.
  std::string version_id;

  bool operator==(const DeclarativeTypeId& other) const {
    return name_space == other.name_space && local_id == other.local_id &&
           version_id == other.version_id;
  }

  bool IsGoogleSQLBuiltin() const { return name_space == kGoogleSqlNamespace; }

  template <typename H>
  friend H AbslHashValue(H h, const DeclarativeTypeId& type_id) {
    return H::combine(std::move(h), type_id.name_space, type_id.local_id,
                      type_id.version_id);
  }
};

// Holds the callbacks for resolution and validation of type parameters for
// types defined through the declarative type framework.
//
// Those opaque callbacks are not serialized into TypeProto, and therefore
// should only be used for built-in types known to the engine.
//
// Because built-in types have static, fixed behavior defined within GoogleSQL's
// codebase, their callbacks do not need to be serialized into TypeProto.
//
// Why stateless function pointers (`(*)(...)`) instead of `std::function`:
// 1. Native Equality: Function pointers can be compared directly (`==`). This
//    allows `DeclarativeTypeDescriptor::IsIdenticalTo` to verify that two
//    descriptors with the same DeclarativeTypeId use identical parameter
//    callbacks, ensuring `TypeFactory` safely deduplicates registrations.
// 2. Zero State/Lifetime Overhead: Captures are prohibited, ensuring static
//    lifetime. Stateless C++ lambdas (`+[](...) -> ...`) decay automatically to
//    function pointers at registration call sites.
class TypeParameterHandlers {
 public:
  // Callback to the implementation of
  // `Type::ValidateAndResolveTypeParameters()`.
  // `DeclarativeType::ValidateAndResolveTypeParameters()` will delegate to this
  // callback, and propagate the result (or any errors) to the caller.
  using ResolveCallback = absl::StatusOr<TypeParameters> (*)(
      const std::vector<TypeParameterValue>&, ProductMode);

  // Callback to the implementation of `Type::ValidateResolvedTypeParameters()`.
  // `DeclarativeType::ValidateResolvedTypeParameters()` will delegate to this
  // callback, and propagate any errors to the caller.
  using ValidateCallback = absl::Status (*)(const TypeParameters&, ProductMode);

  // Creates a TypeParameterHandlers instance. Fails if either callback is null.
  static absl::StatusOr<TypeParameterHandlers> Create(
      ResolveCallback resolve_callback, ValidateCallback validate_callback) {
    if (resolve_callback == nullptr || validate_callback == nullptr) {
      return absl::InvalidArgumentError(
          "TypeParameterHandlers requires both resolve and validate "
          "callbacks to be non-null");
    }
    return TypeParameterHandlers(resolve_callback, validate_callback);
  }

  TypeParameterHandlers(const TypeParameterHandlers&) = default;
  TypeParameterHandlers& operator=(const TypeParameterHandlers&) = default;

  bool operator==(const TypeParameterHandlers& other) const {
    return resolve_callback_ == other.resolve_callback_ &&
           validate_callback_ == other.validate_callback_;
  }
  bool operator!=(const TypeParameterHandlers& other) const {
    return !(*this == other);
  }

  ResolveCallback resolve_callback() const { return resolve_callback_; }
  ValidateCallback validate_callback() const { return validate_callback_; }

 private:
  TypeParameterHandlers(ResolveCallback resolve_callback,
                        ValidateCallback validate_callback)
      : resolve_callback_(resolve_callback),
        validate_callback_(validate_callback) {}

  ResolveCallback resolve_callback_;
  ValidateCallback validate_callback_;
};

#ifndef SWIG
// Keep this class cheap to copy.
static_assert(sizeof(TypeParameterHandlers) == sizeof(void*) * 2,
              "TypeParameterHandlers size mismatch");
#endif

// This contains all the information to fully specify a DeclarativeType. It
// describes the type's properties, traits, and full behavior. It also
// specifies its identity through the DeclarativeTypeId, which uniquely
// identifies the type.
//
// Unlike primitive types where the analyzer code sometimes hard-codes some
// behaviors, everything about a DeclarativeType's behavior is localized to its
// `DeclarativeTypeDescriptor`'s setup.
//
// This is quite similar to how ProtoType and EnumType are created generically,
// relying on the descriptor. However, descriptors for declaratively-defined
// types ("declarative types" for short) define the type's identity as well, not
// just its behavior.
//
// Notes:
// * `DeclarativeTypeId` is always required. It uniquely identifies the type.
//   If multiple instances are created with the same DeclarativeTypeId, they
//   must be identical: they are the same type, and Type::Equals() returns true.
//
// * `backing_type` is always required. It is used primarily for value
//    representation. For example:
//   - An engine implementing a type ComplexNumber could, under the hood,
//     represent values as a struct of two doubles.
//   - User-created types: a `CREATE TYPE` statement normally would specify a
//     backing type for the engine to know how to represent values.
//     The user-visible "base type" naturally lends itself to be the
//     `backing_type`.
//
//   Serialization and deserialization are delegated to the backing type. The
//   framework also supports delegating some traits (such as equality) to the
//   backing type.
//
// See (broken link) for more details.
class DeclarativeTypeDescriptor final {
 public:
  DeclarativeTypeDescriptor() = default;
  DeclarativeTypeDescriptor(const DeclarativeTypeDescriptor& other) {
    data_ = std::make_unique<Data>(*other.data_);
  }
  DeclarativeTypeDescriptor(DeclarativeTypeDescriptor&&) = default;

  enum class AllowCoercionMode {
    kNoCoercion = 0,    // No coercion is allowed.
    kExplicitOnly,      // Only explicit coercion is allowed.
    kAllowAllCoercion,  // Implicit, assignment and explicit are all allowed.
  };

  struct ReturningDisallowed {};
  struct ReturningDelegated {};
  using ReturningStrategy =
      std::variant<ReturningDisallowed, ReturningDelegated>;

  struct EqualityDisallowed {};
  struct EqualityDelegated {};
  using EqualityStrategy = std::variant<EqualityDelegated, EqualityDisallowed>;

  struct TypeParamsDisallowed {
    bool operator==(const TypeParamsDisallowed&) const = default;
  };
  using TypeParamsCustom = TypeParameterHandlers;
  using TypeParamsStrategy =
      std::variant<TypeParamsDisallowed, TypeParamsCustom>;

  struct FormattingDisallowed {
    bool operator==(const FormattingDisallowed&) const = default;
  };
  struct FormattingCustom {
    // Callback to format the ValueContent of a DeclarativeType into a string
    // according to the given FormatValueContentOptions.
    //
    // Those opaque callbacks are not serialized into TypeProto, and therefore
    // should only be used for built-in types known to the engine or GoogleSQL.
    //
    // Stateless function pointers (`(*)(...)`) are used so they can be
    // natively compared via `operator==`, ensuring safe deduplication in
    // `TypeFactory`.
    // In the future, we will consider which parts here can replace parts of
    // the internal Type::FormatValueContentOptions.
    struct FormatOptions {
      enum class Mode {
        kDebug,       // "%t", human debug logs and error messages (Readability)
        kSQLLiteral,  // "%T", SQL literal
        kSQLExpression,  // Called by code to generate SQL, e.g. SqlBuilder.
      };
      Mode mode;
    };
    using Callback = std::string (*)(const ValueContent&, const FormatOptions&);

    explicit FormattingCustom(Callback callback) : callback(callback) {}

    Callback callback = nullptr;

    bool operator==(const FormattingCustom& other) const {
      return callback == other.callback;
    }
  };

  using FormattingStrategy =
      std::variant<FormattingDisallowed, FormattingCustom>;

  const DeclarativeTypeId& type_id() const { return data_->type_id; }
  DeclarativeTypeDescriptor& set_type_id(const DeclarativeTypeId& type_id) {
    data_->type_id = type_id;
    return *this;
  }

  const std::string& display_name() const { return data_->display_name; }
  DeclarativeTypeDescriptor& set_display_name(absl::string_view display_name) {
    data_->display_name = display_name;
    return *this;
  }

  const Type* backing_type() const { return data_->backing_type; }
  DeclarativeTypeDescriptor& set_backing_type(const Type* backing_type) {
    data_->backing_type = backing_type;
    return *this;
  }

  AllowCoercionMode coercion_from_backing_type() const {
    return data_->coercion_from_backing_type;
  }
  DeclarativeTypeDescriptor& set_coercion_from_backing_type(
      AllowCoercionMode coercion_from_backing_type) {
    data_->coercion_from_backing_type = coercion_from_backing_type;
    return *this;
  }

  AllowCoercionMode coercion_to_backing_type() const {
    return data_->coercion_to_backing_type;
  }
  DeclarativeTypeDescriptor& set_coercion_to_backing_type(
      AllowCoercionMode coercion_to_backing_type) {
    data_->coercion_to_backing_type = coercion_to_backing_type;
    return *this;
  }

  ReturningStrategy returning_strategy() const {
    return data_->returning_strategy;
  }
  DeclarativeTypeDescriptor& set_returning_strategy(
      const ReturningStrategy& returning_strategy) {
    data_->returning_strategy = returning_strategy;
    return *this;
  }

  const EqualityStrategy& equality_strategy() const {
    return data_->equality_strategy;
  }
  DeclarativeTypeDescriptor& set_equality_strategy(
      const EqualityStrategy& equality_strategy) {
    data_->equality_strategy = equality_strategy;
    return *this;
  }

  const FormattingStrategy& formatting_strategy() const {
    return data_->formatting_strategy;
  }
  DeclarativeTypeDescriptor& set_formatting_strategy(
      FormattingStrategy formatting_strategy) {
    data_->formatting_strategy = std::move(formatting_strategy);
    return *this;
  }

  const LanguageOptions::LanguageFeatureSet&
  additional_required_language_features() const {
    return data_->additional_required_language_features;
  }
  DeclarativeTypeDescriptor& set_additional_required_language_features(
      const LanguageOptions::LanguageFeatureSet&
          additional_required_language_features) {
    data_->additional_required_language_features =
        additional_required_language_features;
    return *this;
  }

  size_t GetEstimatedOwnedMemoryBytesSize() const;

  // A descriptor is identical to other if all fields are identical.
  bool IsIdenticalTo(const DeclarativeTypeDescriptor& other) const;

  // Indicates whether this type supports type parameters, and if so, the
  // callbacks to resolve and validate those type parameters.
  // Those are the implementation of `Type::ValidateAndResolveTypeParameters()`
  // and `Type::ValidateResolvedTypeParameters()`.
  // When TypeParameterHandlers is present, `DeclarativeType` delegates to these
  // callbacks from its overrides of those signatures on Type.
  //
  // If TypeParamsDisallowed, the type does not support type parameters.
  // `DeclarativeType`'s implementations of
  // `Type::ValidateAndResolveTypeParameters()` and
  // `Type::ValidateResolvedTypeParameters()` return an error reporting as such.
  const TypeParamsStrategy& type_params_strategy() const {
    return data_->type_params_strategy;
  }
  DeclarativeTypeDescriptor& set_type_params_strategy(
      const TypeParamsStrategy& type_params_strategy) {
    data_->type_params_strategy = type_params_strategy;
    return *this;
  }

 private:
  struct Data {
    // Internal ID which uniquely identifies this type.
    // Type identity (Equals()) is determined through this ID.
    DeclarativeTypeId type_id;

    // Not used for type identity, but is still user-visible, e.g. in the result
    // of TYPEOF(), displaying function signatures, or in error messages.
    // In the future, this may become multi-part.
    std::string display_name;

    // The backing type for this declarative type.
    const Type* backing_type = nullptr;

    // Allowed coercion modes to and from the `backing_type`.
    AllowCoercionMode coercion_from_backing_type =
        AllowCoercionMode::kNoCoercion;

    // Allowed coercion modes to the `backing_type`.
    AllowCoercionMode coercion_to_backing_type = AllowCoercionMode::kNoCoercion;

    // The returning strategy for this declarative type.
    ReturningStrategy returning_strategy = ReturningDisallowed{};

    // The equality strategy for this declarative type.
    EqualityStrategy equality_strategy = EqualityDisallowed{};

    // The formatting strategy for this declarative type.
    FormattingStrategy formatting_strategy = FormattingDisallowed{};

    // *Additional* required features for this type. Does not include other
    // features which are required for the backing type.
    // IsSupportedType() checks both.
    LanguageOptions::LanguageFeatureSet additional_required_language_features;

    // The type parameter handling strategy for this declarative type.
    // Those opaque callbacks are not serialized into TypeProto, and therefore
    // should only be used for built-in types known to the engine.
    TypeParamsStrategy type_params_strategy = TypeParamsDisallowed{};
  };
  // Allocate on the heap.
  std::unique_ptr<Data> data_ = std::make_unique<Data>();
};

// A declaratively-specified type ("declarative type", for short).
// Such types are created from a `DeclarativeTypeDescriptor`, which fully
// specifies the type's properties, traits, behavior and even identity.
//
// Note that DeclarativeTypes are first-class citizens in the Type system.
// The fact that they are implemented through the declarative type framework is
// an implementation detail which should stay hidden from the various
// components.
//
// This class encapsulates the `DeclarativeTypeDescriptor` and presents the type
// to the GoogleSQL analyzer (Resolver, Coercer, etc.) as an opaque type.
// There is no implied semantic relationship or coercibility (in SQL) to/from
// the backing type.
// Value representation is delegated to the backing type.
// Various other traits are defined in terms of the backing type, e.g. through
// disabling or delegating.
//
// See (broken link) for more on declarative types.
class DeclarativeType final : public Type {
 public:
#ifndef SWIG
  DeclarativeType(const DeclarativeType&) = delete;
  DeclarativeType& operator=(const DeclarativeType&) = delete;
#endif  // SWIG

  std::string ShortTypeName(ProductMode mode) const override;
  std::string TypeName(ProductMode mode) const override;

  const DeclarativeTypeDescriptor& descriptor() const { return data_; }
  const DeclarativeTypeId& id() const { return data_.type_id(); }

  // Returns true if this is a GoogleSQL built-in type.
  bool IsGoogleSQLBuiltin() const { return id().IsGoogleSQLBuiltin(); }

  // Returns true if this is a GoogleSQL built-in type with the given local ID.
  bool IsGoogleSQLBuiltin(absl::string_view local_id) const {
    return IsGoogleSQLBuiltin() && id().local_id == local_id;
  }

  const DeclarativeTypeDescriptor::FormattingStrategy& formatting_strategy()
      const {
    return data_.formatting_strategy();
  }

  // Returns true if this type supports formatting (e.g. via the FORMAT
  // function).
  bool SupportsFormatting() const;

  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode) const override;

  absl::StatusOr<TypeParameters> ValidateAndResolveTypeParameters(
      const std::vector<TypeParameterValue>& type_parameter_values,
      ProductMode mode) const override;

  absl::Status ValidateResolvedTypeParameters(
      const TypeParameters& type_parameters, ProductMode mode) const override;

  TypeListView ComponentTypes() const final;

  const DeclarativeType* AsDeclarativeType() const override { return this; }

  // Indicates whether this type can be coerced to the given `to_type`.
  // `is_explicit` indicates whether this is for an explicit or implicit
  // coercion.
  bool CanCoerceTo(const Type* to_type, bool is_explicit) const;

  // Indicates whether `from_type` can coerce to this type.
  // `is_explicit` indicates whether this is for an explicit or implicit
  // coercion.
  bool CanCoerceFrom(const Type* from_type, bool is_explicit) const;

  std::string CapitalizedName() const final;

  bool IsSupportedType(const LanguageOptions& language_options) const final;

  int64_t GetEstimatedOwnedMemoryBytesSize() const final;

  uint64_t GetValueContentExternallyAllocatedByteSize(
      const ValueContent& value) const final;

  bool SupportsEquality() const final;

  bool SupportsOrdering(const LanguageOptions& language_options,
                        std::string* type_description) const final;

  void ClearValueContent(const ValueContent& value) const final;

  void CopyValueContent(const ValueContent& from, ValueContent* to) const final;

  // Returns the candidate supertypes for this declarative type.
  TypeListView GetCandidateSuperTypes() const { return candidate_super_types_; }

 protected:
  absl::Status SerializeToProtoAndDistinctFileDescriptorsImpl(
      const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const final;

  bool SupportsGroupingImpl(const LanguageOptions& language_options,
                            const Type** no_grouping_type) const final;

  bool SupportsPartitioningImpl(const LanguageOptions& language_options,
                                const Type** no_partitioning_type) const final;

  bool SupportsReturningImpl(const LanguageOptions& language_options,
                             const Type** no_returning_type) const final;

 private:
  DeclarativeType(const TypeFactoryBase& factory,
                  DeclarativeTypeDescriptor data);

  const Type* backing_type() const { return data_.backing_type(); }

  bool EqualsForSameKind(const Type* that, bool equivalent) const final;

  void DebugStringImpl(bool details, TypeOrStringVector* stack,
                       std::string* debug_string) const override;

  absl::HashState HashTypeParameter(absl::HashState state) const final;

  bool ValueContentEquals(const ValueContent& x, const ValueContent& y,
                          const ValueEqualityCheckOptions& options) const final;

  bool ValueContentLess(const ValueContent& x, const ValueContent& y,
                        const Type* other_type) const final;

  absl::HashState HashValueContent(const ValueContent& value,
                                   absl::HashState state) const final;
  absl::HashState HashValueContentIgnoringFloat(
      const ValueContent& value, absl::HashState state) const final;

  std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const final;

  absl::Status SerializeValueContent(const ValueContent& value,
                                     ValueProto* value_proto) const final;

  absl::Status DeserializeValueContent(const ValueProto& value_proto,
                                       ValueContent* value) const final;

  bool IsIdenticalTo(const DeclarativeType* other) const;

  // Retrieves the ValueContent corresponding to the backing type.
  // Values are 16 bytes, split as 8 bytes of content, plus 8 bytes for
  // metadata, which could be a Type* or splits as a 4-byte TypeKind enum, with
  // the 4 remaining bytes used for additional content for some kinds.
  // So there are 4 cases:
  // a) 8-byte content, plus a TypeKind
  // b) 8-byte content, plus a Type*
  // c) 8-byte pointer to variable width content, plus enum or pointer
  // d) 12-byte content, plus a TypeKind (for DATETIME and TIME)
  //
  // For declarative types, we always need a Type* pointer, which points to the
  // DeclarativeType, and whose descriptor fully describes the type, including
  // what its backing type is.
  //
  // For a backing type stored as (a), (b), or (c), the value content can be
  // stored inline the same way, with a Type* pointer pointing to the
  // DeclarativeType.
  //
  // For a backing type stored as (d), this doesn't fit, so the
  // backing type content is converted to be stored as a pointer to an
  // out-of-line 12-byte value.
  const ValueContent& GetBackingContent(
      const ValueContent& value_content) const;

  friend class TypeFactory;
  friend class Value;

  DeclarativeTypeDescriptor data_;

  std::vector<const Type*> candidate_super_types_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_DECLARATIVE_TYPE_H_
