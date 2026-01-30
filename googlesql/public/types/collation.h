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

#ifndef GOOGLESQL_PUBLIC_TYPES_COLLATION_H_
#define GOOGLESQL_PUBLIC_TYPES_COLLATION_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/collation.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// TODO: Replace existing references to ResolvedCollation to this
// new class for collation.
// This class is used with googlesql::Type to indicate the collation attached to
// the type. For nested types, see comments on <child_list_> for how collation
// on subfield(s) are stored.
//
// This is always stored in a normalized form, meaning on all the nested levels,
// it has either an empty <child_list_> to indicate that it has no collation in
// any child, or it has at least one non-empty child.
class Collation {
 public:
  // Iterates the <annotation_map> and makes a normalized Collation
  // instance.
  static absl::StatusOr<Collation> MakeCollation(
      const AnnotationMap& annotation_map);

  // Makes a Collation instance with input <child_list>. If all collations
  // inside <child_list> are empty, an empty Collation will be returned for
  // normalization purpose.
  static Collation MakeCollationWithChildList(
      std::vector<Collation> child_list);

  // Makes a Collation instance for scalar type.
  static Collation MakeScalar(absl::string_view collation_name);

  // Constructs empty Collation. Default constructor must be public to
  // be used in the ResolvedAST.
  Collation() = default;

  // Collation is movable.
  Collation(Collation&&) = default;
  Collation& operator=(Collation&& that) = default;

  // When copied, the underlying string in <collation_name_> is shared. See
  // googlesql::SimpleValue for more detail.
  Collation(const Collation&) = default;
  Collation& operator=(const Collation& that) = default;

  // Returns true if current type has no collation and has no children with
  // collation.
  bool Empty() const {
    return !collation_name_.IsValid() && child_list_.empty();
  }

  bool Equals(const Collation& that) const;
  bool operator==(const Collation& that) const { return Equals(that); }

  // Returns true if this Collation has compatible nested structure with
  // <type>. The structures are compatible when they meet the conditions below:
  // * The Collation instance is either empty or is compatible by
  //   recursively following these rules. When it is empty, it indicates that
  //   the collation is empty on all the nested levels, and therefore such
  //   instance is compatible with any Type (including structs and arrays).
  // * This instance has collation and the <type> is a STRING type.
  // * This instance has non-empty child_list and the <type> is a STRUCT,
  //   the number of children matches the number of struct fields, and the
  //   children have a compatible structure with the corresponding struct field
  //   types.
  // * This instance has exactly one child and <type> is an ARRAY, and the child
  //   has a compatible structure with the array's element type.
  bool HasCompatibleStructure(const Type* type) const;

  // Returns true if this Collation object semantically equals the collation
  // annotations inside <annotation_map>. These are equal when these two
  // conditions are met:
  // * <annotation_map> is a nullptr and the Collation object is empty.
  // * The Collation object equals the collation created by calling
  //   MakeCollation with <annotation_map>.
  absl::StatusOr<bool> EqualsCollationAnnotation(
      const AnnotationMap* annotation_map) const;

  // Collation on current type (STRING), not on subfields.
  bool HasCollation() const { return collation_name_.has_string_value(); }

  absl::string_view CollationName() const {
    static char kEmptyCollation[] = "";
    return collation_name_.has_string_value()
               ? collation_name_.string_value()
               : absl::string_view(kEmptyCollation);
  }

  // Children only exist if any of the children have a collation. See comments
  // on <child_list_> for more detail.
  const Collation& child(int i) const { return child_list_[i]; }
  uint64_t num_children() const { return child_list_.size(); }

  // Returns an annotation map that is compatible with the input <type> and has
  // collation annotations equal to the Collation object. Note that the returned
  // AnnotationMap is always normalized.
  absl::StatusOr<std::unique_ptr<AnnotationMap>> ToAnnotationMap(
      const Type* type) const;

  // Populates the given annotation map with Collation annotations.
  absl::Status PopulateAnnotationMap(AnnotationMap& annotation_map) const;

  absl::Status Serialize(CollationProto* proto) const;
  static absl::StatusOr<Collation> Deserialize(const CollationProto& proto);

  std::string DebugString() const;

 private:
  Collation(absl::string_view collation_name, std::vector<Collation> child_list)
      : child_list_(std::move(child_list)) {
    if (!collation_name.empty()) {
      collation_name_ = SimpleValue::String(std::string(collation_name));
    }
  }

  // Stores Collation for subfields for ARRAY/STRUCT types.
  // <child_list_> could be empty to indicate that the ARRAY/STRUCT doesn't have
  // collation in subfield(s).
  // When <child_list_> is not empty, for ARRAY, the size of <child_list_>
  // must be 1; for STRUCT, the size of <child_list_> must be the same as the
  // number of the fields the STRUCT has.
  std::vector<Collation> child_list_;

  // This SimpleValue instance is either TYPE_INVALID to indicate there is no
  // collation or TYPE_STRING to store the collation name.
  SimpleValue collation_name_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_COLLATION_H_
