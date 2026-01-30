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

// Internal data structures for reference implementation evaluation.

#ifndef GOOGLESQL_REFERENCE_IMPL_TUPLE_H_
#define GOOGLESQL_REFERENCE_IMPL_TUPLE_H_

#include <stddef.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/public/proto_util.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/tuple_comparator.h"
#include "googlesql/reference_impl/variable_id.h"
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/flat_set.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

// Stores the mapping of variables (which must all be distinct) to slots in a
// tuple.
class TupleSchema {
 public:
  explicit TupleSchema(absl::Span<const VariableId> variables);
  TupleSchema(const TupleSchema&) = delete;
  TupleSchema& operator=(const TupleSchema&) = delete;

  int num_variables() const { return variables_.size(); }

  const VariableId& variable(int i) const { return variables_[i]; }

  const std::vector<VariableId>& variables() const { return variables_; }

  std::optional<int> FindIndexForVariable(const VariableId& variable) const;

  std::string DebugString() const;

  void AddVariable(VariableId variable);

 private:
  using VariableStorage = std::vector<VariableId>;
  struct VariableIdComparator {
    using is_transparent = void;
    bool operator()(int x, const VariableId& y) const {
      return (*storage)[x] < y;
    }
    bool operator()(const VariableId& x, int y) const {
      return x < (*storage)[y];
    }
    bool operator()(int x, int y) const {
      return (*storage)[x] < (*storage)[y];
    }

    const VariableStorage* storage;
  };
  using VariableIndex = googlesql_base::flat_set<int, VariableIdComparator>;

  VariableStorage variables_;
  VariableIndex vars_to_idxs_{VariableIdComparator{&variables_}};
};

// Represents a field access on a proto-valued expression. (A subfield access
// a.b.c consists of two field accesses.)
struct ProtoFieldAccessInfo {
  // Information for reading the proto field from bytes.
  ProtoFieldInfo field_info;
  // If true, then if the proto-valued expression is NULL, return
  // 'field_info.default_value' instead of NULL. Must be false if
  // 'field_info->get_has_bit' is true.
  bool return_default_value_when_unset = false;
};

// Represents a list of proto fields to deserialize from the result of a
// particular proto-valued expression. (A subfield access a.b.c involves two
// registries: one for a.b and one for (a.b).c.) An operator node that wants the
// value a particular proto field accesses the list of result values based on
// its desired field's position in the list. The algebrizer does its best to
// consolidate field accesses into as few ProtoFieldRegistries as possible.
class ProtoFieldRegistry {
 public:
  // 'id' is a unique identifier of this instance that is used for debug
  // logging.
  ProtoFieldRegistry() = default;
  ProtoFieldRegistry(const ProtoFieldRegistry&) = delete;
  ProtoFieldRegistry& operator=(const ProtoFieldRegistry&) = delete;

  // Registers 'access_info' (which must outlive this object) for subsequent
  // calls to GetRegisteredFields(). Returns the index of 'access_info' in the
  // internal vector, which is the index of the result in the corresponding
  // ProtoFieldValueList returned by ReadProtoFields and stored in a TupleSlot's
  // SharedProtoState if EvaluationOptions::store_proto_field_value_maps is
  // true.
  int RegisterField(const ProtoFieldAccessInfo* access_info) {
    const int index = registered_access_infos_.size();
    registered_access_infos_.push_back(access_info);
    return index;
  }

  const std::vector<const ProtoFieldAccessInfo*>& GetRegisteredFields() const {
    return registered_access_infos_;
  }

 private:
  // This is the set of fields that GetProtoFieldExprs care about. Not owned.
  std::vector<const ProtoFieldAccessInfo*> registered_access_infos_;
};

// Key type for ProtoFieldValueMap (defined below). An entry in that map
// represents the deserialized proto field values for a particular proto Value
// (representing by a pointer to its internally reference-counted data) and
// ProtoFieldRegistry. (A subfield access a.b.c involves two
// ProtoFieldValueMapKeys: one for a.b and another for (a.b).c.)
struct ProtoFieldValueMapKey {
  const InternalValue::ProtoRep* proto_rep = nullptr;
  const ProtoFieldRegistry* registry = nullptr;

  bool operator==(const ProtoFieldValueMapKey& k) const {
    return proto_rep == k.proto_rep && registry == k.registry;
  }

  template <typename H>
  friend H AbslHashValue(H h, const ProtoFieldValueMapKey& k) {
    return H::combine(std::move(h), k.proto_rep, k.registry);
  }
};

// Maps a ProtoFieldValueMapKey to its corresponding set of deserialized
// Values.
//
// To see how this is used, consider this example query:
//    SELECT a.b.c, a.b.d FROM Table
//
// The algebrized tree looks something like this:
//
//     Root
//     + c
//     |  + b
//     |     + a
//     + d
//     |  + b
//     |     + a
//     + Table
//
// The two nodes for "a" share the same registry, for which the only field of
// interest is "b". The two nodes for "b" share the same registry, which has
// fields "c" and "d".
//
// Since "a" is a column, there is one TupleSlot (physical storage, defined
// below) for it in the row, and both nodes for "a" refer to that TupleSlot. The
// two nodes for a.b each have their own (transient) TupleSlot. The nodes for
// a.b.c and a.b.d each have their own TupleSlots in the row.
//
// Here's how the ProtoFieldValueMap is populated:
//
// 1) First we evaluate Value(a.b) and put (ProtoRep(a), Registry(a)) ->
//    {Value(a.b)}) in the map.
//
// 2) Then we evaluate Value(Value(a.b).c). Due to the shared registry, we
//    compute Value(Value(a.b).c) and Value(Value(a.b).d) and put
//    (ProtoRep(Value(a.b)), Registry(a.b)) -> {Value(Value(a.b).c),
//    Value(Value(a.b).d)} in the map. We return Value(Value(a.b).c).
//
// 3) During the second evaluation of Value(a.b), we use the entry we put in the
//    map in (1) to avoid deserialization and just return the Value from the
//    map.
//
// 4) During the evaluation of Value(Value(a.b).d), we use the entry we put in
//    the map in (2) and just return Value(Value(a.b).d) from the map instead of
//    deserializing it.
//
// The fact that the ProtoRep is in the key is critical for this to work. It's
// how we get a unique identifier for the proto whose field we are accessing,
// such that the identifier is preserved even as the Value is copied around.
//
// This example does not really show why it is important for the registry to be
// in the key. For that, the issue is that there might be some other place in
// the query where we are, for example, extracting e.f, and "e" actually has the
// same value as "a", but the algebrizer isn't able to infer that (so the node
// for "e" will have a new registry). By putting the registry in the key, we
// guarantee that this kind of thing won't confuse the evaluation.
using ProtoFieldValueMap =
    absl::flat_hash_map<ProtoFieldValueMapKey,
                        std::unique_ptr<ProtoFieldValueList>>;

// Stores a Value along with some cached information for proto values. The
// evaluation code passes around Tuples (defined below), which are basically
// just a pair (TupleSchema, TupleData), where a TupleData is basically just a
// vector of TupleSlots. Thus, a TupleSlot is the fundamental unit by which data
// is passed around by the evaluation code.
//
// Functions that populate a TupleSlot should take a VirtualTupleSlot* as an
// output argument (instead of a TupleSlot*). VirtualTupleSlot is defined below;
// it is a generalization of TupleSlot where the Value and cached information
// can come from anywhere (not necessarily a TupleSlot).
class TupleSlot {
 public:
  // The shared proto-related state stored in this object alongside a Value,
  // used to avoid deserializing the same field of a proto multiple times.
  //
  // When performing an operation whose input is a Value v, the evaluation code
  // configures the destination TupleSlot to share proto-related state with the
  // source TupleSlot if the operation is one of the following:
  //   - A copy.
  //   - Getting a field from v, which is a proto.
  //   - Getting a field from v, which is a struct.
  //       - (The algebrizer does not exploit this today, but the approach for
  //          proto fields can be easily generalized.)
  //
  // As a simple example, consider this query:
  //     SELECT proto_column.field1, proto_column.field2 FROM Table
  // The algebrizer consolidates the two accesses into a single const
  // FieldRegistry* shared by two GetProtoFieldExpr nodes. The first call to
  // GetProtoFieldExpr::Eval() reads both fields and puts the value of field1
  // into the destination TupleSlot along with SharedProtoState that has a
  // ProtoFieldValueMap entry corresponding to the reference-counted internal
  // state of proto_column and the const FieldRegistry*. That SharedProtoState
  // is shared with the TupleSlot holding the value of proto_column. The second
  // call to GetFieldExpr::Eval() looks up the ProtoFieldValueMap entry (it
  // has all the information to construct the key) and uses the
  // ProtoFieldValueMap stored in that entry to determine the value of field2.
  using SharedProtoState = std::optional<ProtoFieldValueMap>;

  // For performance reasons, we only store SharedProtoState for PROTOs and:
  // - STRUCTs
  // - GRAPH_ELEMENTs
  // - GRAPH_PATHS
  // - MEASUREs
  // which may contain protos.
  // TODO check why ARRAY isn't in this list.
  // TODO see whether can-contain-proto is something Type itself
  // should indicate.
  static bool ShouldStoreSharedProtoStateFor(TypeKind kind) {
    return kind == TYPE_PROTO || kind == TYPE_GRAPH_ELEMENT ||
           kind == TYPE_GRAPH_PATH || kind == TYPE_STRUCT ||
           kind == TYPE_MEASURE;
  }

  TupleSlot() = default;

  TupleSlot(const TupleSlot& slot) { *this = slot; }
  TupleSlot(TupleSlot&& slot) = default;

  TupleSlot& operator=(const TupleSlot& slot) {
    value_ = slot.value_;
    if (value_.is_valid() &&
        ShouldStoreSharedProtoStateFor(value_.type_kind())) {
      shared_proto_state_ = slot.shared_proto_state_;
    }
    return *this;
  }
  TupleSlot& operator=(TupleSlot&& slot) = default;

  // Returns the wrapped Value.
  const Value& value() const { return value_; }

  // Returns a pointer to the wrapped Value so that it can be reset. This is not
  // implemented as SetValue(...) for performance reasons, as even a move is
  // measurably less efficient than just writing data directly. The caller is
  // responsible for ensuring that the shared proto state is correctly updated
  // after calling this method: the SharedProtoState should be non-NULL if and
  // only if ShouldStoreSharedProtoStateFor(value().type_kind()) is true.
  //
  // Another use case for this method is to move the wrapped Value out of this
  // TupleSlot. In that case, this TupleSlot is invalidated until the next time
  // the Value is modified.
  Value* mutable_value() { return &value_; }

  // Returns a pointer to internal state stored alongside the Value in this
  // object. The raw pointer is never NULL. If the callers of mutable_value()
  // obey the contract, the shared_ptr will be NULL if and only if the Value has
  // non-PROTO type. This method has two uses.
  // 1) Modifying the SharedProtoState used by potentially multiple TupleSlots.
  // 2) Configuring two TupleSlots to share SharedProtoState with each other.
  std::shared_ptr<SharedProtoState>* mutable_shared_proto_state() const {
    return &shared_proto_state_;
  }

  // Copy 'value' into this object. If we should be storing SharedProtoState for
  // 'value', initializes it. This method is provided for convenience:
  // internally, it simply creates a VirtualTupleSlot backed by this slot and
  // writes to the VirtualTupleSlot.
  void SetValue(const Value& value);

  // Move 'value' into this object. If we should be storing SharedProtoState for
  // 'value', initializes it. This method is provided for convenience:
  // internally, it simply creates a VirtualTupleSlot backed by this slot and
  // writes to the VirtualTupleSlot.
  void SetValue(Value&& value);

  // Copy 'other' into this object. If we should be storing SharedProtoState for
  // 'value', initializes it. This method is provided for convenience:
  // internally, it simply creates a VirtualTupleSlot backed by this slot and
  // writes to the VirtualTupleSlot.
  void CopyFromSlot(const TupleSlot& other);

  // Returns an approximation of the amount of memory used to store this slot.
  int64_t GetPhysicalByteSize() const;

  bool operator==(const TupleSlot& s) const { return value_ == s.value_; }

  template <typename H>
  friend H AbslHashValue(H h, const TupleSlot& s) {
    return H::combine(std::move(h), s.value_);
  }

 private:
  Value value_;
  // We use a std::shared_ptr because of the sharing scheme described above.
  // The pointer is NULL if 'value_' does not have type PROTO. (This is a
  // performance optimization.)
  // Mutable because logically a TupleSlot is just a single Value.
  mutable std::shared_ptr<SharedProtoState> shared_proto_state_;
  // Allow copy/move/assign.
};

// Represents a TupleSlot for writing, but also supports backing by an arbitrary
// Value and std::shared_ptr<TupleSlot::SharedProtoState> for performance
// reasons. TupleSlot is the physical storage for a column Value in a row/Tuple,
// while VirtualTupleSlot wraps either a real TupleSlot or a fake TupleSlot
// represented by a Value and
// std::shared_ptr<TupleSlot::SharedProtoState>. VirtualTupleSlot provides the
// interface to write values from various sources.
//
// The reason for using fake TupleSlots is to support fast creation of
// array/struct Values without having to populate real TupleSlots and then move
// the values into a std::vector<Value>. For example:
//
//     std::vector<Value> values(values_size);
//     for (int i = 0; i < values.size(); ++i) {
//       const TupleSlot& source_slot = <some function of i>;
//       std::shared_ptr<TupleSlot::SharedProtoState> shared_proto_state;
//       VirtualTupleSlot dest_slot(&values[i], &shared_proto_state);
//       // Populate 'dest_slot' with something based on 'source_slot'.
//       PopulateSlot(source_slot, dest_slot);
//     }  // Throw away shared_proto_state.
//
//     ... Create struct or array from 'values' ...
//
// If PopulateSlot()'s 'dest_slot' argument were a TupleSlot*, we would have to
// do a bunch of extra moves of Values from TupleSlots into 'values'.
//
// Thus, functions that populate a TupleSlot should take a VirtualTupleSlot* as
// an output argument (instead of a TupleSlot*).
class VirtualTupleSlot {
 public:
  // Construct a VirtualTupleSlot for writing to 'slot'. The caller retains
  // ownership of 'slot'.
  explicit VirtualTupleSlot(TupleSlot* slot)
      : VirtualTupleSlot(slot->mutable_value(),
                         slot->mutable_shared_proto_state()) {}

  // Construct a VirtualTupleSlot backed by 'value' and
  // 'shared_proto_state'. The caller retains ownership of both pointers.
  VirtualTupleSlot(
      Value* value,
      std::shared_ptr<TupleSlot::SharedProtoState>* shared_proto_state)
      : value_(value), shared_proto_state_(shared_proto_state) {}

  // Sets the internal value to 'value'. If we should be storing
  // SharedProtoState for 'value', sets it to 'shared_proto_state'.
  void SetValueAndMaybeSharedProtoState(
      Value&& value,
      std::shared_ptr<TupleSlot::SharedProtoState>* shared_proto_state) {
    *value_ = value;
    MaybeUpdateSharedProtoStateAfterSettingValue(shared_proto_state);
  }

  // Copy 'slot' into this object.
  void CopyFromSlot(const TupleSlot& slot) {
    *value_ = slot.value();
    MaybeUpdateSharedProtoStateAfterSettingValue(
        slot.mutable_shared_proto_state());
  }

  // Copy 'value' into this object. If we should be storing SharedProtoState for
  // 'value', initializes it.
  void SetValue(const Value& value) {
    *value_ = value;
    MaybeResetSharedProtoState();
  }

  // Move 'value' into this object. If we should be storing SharedProtoState for
  // 'value', initializes it.
  void SetValue(Value&& value) {
    *value_ = std::move(value);
    MaybeResetSharedProtoState();
  }

  // For performance reasons, users can write directly to the wrapped Value
  // without incurring the cost of a move. Those users must call
  // MaybeResetSharedProtoState() (when the source Value is not derived from a
  // TupleSlot) or MaybeUpdateSharedProtoStateAfterSettingValue() (when the
  // source Value is derived from another TupleSlot).
  Value* mutable_value() { return value_; }

  // Initializes the SharedProtoState for the internal value if we should be
  // storing it.
  void MaybeResetSharedProtoState() {
    // Value may be invalid when a computation fails and its side effect is
    // deferred.
    if (value_->is_valid() &&
        TupleSlot::ShouldStoreSharedProtoStateFor(value_->type_kind())) {
      *shared_proto_state_ = std::make_shared<TupleSlot::SharedProtoState>();
    }
  }

  // For performance reasons, we pass 'shared_proto_state' by pointer to avoid
  // triggering shared_ptr's refcounting logic except when necessary.
  void MaybeUpdateSharedProtoStateAfterSettingValue(
      std::shared_ptr<TupleSlot::SharedProtoState>* shared_proto_state) {
    // Value may be invalid when a computation fails and its side effect is
    // deferred.
    if (value_->is_valid() &&
        TupleSlot::ShouldStoreSharedProtoStateFor(value_->type_kind())) {
      *shared_proto_state_ = *shared_proto_state;
    }
  }

 private:
  // Both pointers are owned by the caller of the constructor. They can be
  // thought of as essentially a TupleSlot*, except there are
  // performance-sensitive cases where we actually want to specify a Value* and
  // std::shared_ptr<SharedProtoState>* separately instead of requiring writes
  // to go to a TupleSlot (out of which we would have to then copy/move the
  // Value).
  Value* value_;
  std::shared_ptr<TupleSlot::SharedProtoState>* shared_proto_state_;
  // Allow copy/move/assign.
};

inline void TupleSlot::SetValue(const Value& value) {
  VirtualTupleSlot slot(this);
  slot.SetValue(value);
}

inline void TupleSlot::SetValue(Value&& value) {
  VirtualTupleSlot slot(this);
  slot.SetValue(std::move(value));
}

inline void TupleSlot::CopyFromSlot(const TupleSlot& other) {
  VirtualTupleSlot slot(this);
  slot.CopyFromSlot(other);
}

// Stores the contents of a tuple, which is essentially a vector of TupleSlots.
class TupleData {
 public:
  TupleData() = default;

  explicit TupleData(int num_slots) : slots_(num_slots) {}

  explicit TupleData(absl::Span<const TupleSlot> slots)
      : slots_(slots.begin(), slots.end()) {}

  void Clear() { slots_.clear(); }

  void AddSlots(int num_slots) { slots_.resize(slots_.size() + num_slots); }

  // Removes the slot at 'slot_index' from this TupleData. The complexity is
  // O(n), prefer to use more efficient ways when possible.
  void RemoveSlotAt(int slot_index) {
    ABSL_DCHECK_LT(slot_index, num_slots());
    slots_.erase(slots_.begin() + slot_index);
  }

  void RemoveSlots(int num_slots) { slots_.resize(slots_.size() - num_slots); }

  int num_slots() const { return slots_.size(); }

  const TupleSlot& slot(int i) const { return slots_[i]; }

  const std::vector<TupleSlot>& slots() const { return slots_; }

  // Returns an approximation of the memory size of this TupleData.
  int64_t GetPhysicalByteSize() const {
    int64_t num_bytes = sizeof(std::vector<TupleSlot>);
    for (const TupleSlot& slot : slots()) {
      num_bytes += slot.GetPhysicalByteSize();
    }
    return num_bytes;
  }

  // The returned pointer is owned by this object and is valid until the next
  // non-const method call.
  TupleSlot* mutable_slot(int i) {
    if (i >= slots_.size()) {
      slots_.resize(i + 1);
    }
    return &slots_[i];
  }

  bool Equals(const TupleData& d) const { return *this == d; }

  bool operator==(const TupleData& d) const { return slots_ == d.slots_; }

  template <typename H>
  friend H AbslHashValue(H h, const TupleData& d) {
    return H::combine(std::move(h), d.slots_);
  }

  std::string DebugString() const {
    return absl::StrCat(
        "TupleData{",
        absl::StrJoin(slots_, ", ",
                      [](std::string* out, const TupleSlot& slot) {
                        absl::StrAppend(out, slot.value().DebugString());
                      }),
        "}");
  }

 private:
  std::vector<TupleSlot> slots_;
  // Allow copy/assign/move.
};

// Wraps a const TupleData* but hashes as the underlying TupleData.
struct TupleDataPtr {
  explicit TupleDataPtr(const TupleData* data_in) : data(data_in) {}

  const TupleData* data = nullptr;

  bool operator==(const TupleDataPtr& t) const { return *data == *t.data; }

  template <typename H>
  friend H AbslHashValue(H h, const TupleDataPtr& t) {
    return H::combine(std::move(h), *t.data);
  }
};

struct Tuple {
 public:
  Tuple(const TupleSchema* schema_in, const TupleData* data_in)
      : schema(schema_in), data(data_in) {}

  // 'data->num_slots()' must be at least 'schema->num_variables()', and the
  // i-th variable corresponds to the i-th value. It is possible that 'data' has
  // extra slots to accommodate the optimization where we would like to support
  // "extending" 'data' to match a schema with more variables without copying
  // its contents to a new TupleData.
  const TupleSchema* schema;
  const TupleData* data;

  std::string DebugString(bool verbose = false) const;
};

// Returns a TupleData corresponding to 'values' with no extra information in
// its slots.
inline TupleData CreateTupleDataFromValues(std::vector<Value> values) {
  TupleData data(values.size());
  for (int i = 0; i < values.size(); ++i) {
    VirtualTupleSlot virtual_slot(data.mutable_slot(i));
    virtual_slot.SetValue(std::move(values[i]));
  }
  return data;
}

// Helper method that concatenates 'span1' and 'span2' into a single vector,
// Does not take ownership of any of the pointers. Useful for concatenating
// spans of const {TupleSchema,TupleData}*.
template <typename T>
std::vector<T> ConcatSpans(absl::Span<const T> span1,
                           absl::Span<const T> span2) {
  std::vector<T> ret;
  ret.reserve(span1.size() + span2.size());
  ret.insert(ret.end(), span1.begin(), span1.end());
  ret.insert(ret.end(), span2.begin(), span2.end());
  return ret;
}

// Populates 'new_schema' with the concatenation of the TupleSchemas in 'tuples'
// (which must have disjoint VariableIds), and populates 'new_data' with the
// concatenation of the corresponding TupleData. If a TupleData has more slots
// than the associated TupleSchema, the extra values are dropped. Returns the
// corresponding Tuple.
Tuple ConcatTuples(absl::Span<const Tuple> tuples,
                   std::unique_ptr<TupleSchema>* new_schema,
                   std::unique_ptr<TupleData>* new_data);

// Returns deep copies of 'tuples'. std::shared_ptrs are used to facilitate
// passing as value captures to a lambda.
inline std::vector<std::shared_ptr<const TupleData>> DeepCopyTupleDatas(
    absl::Span<const TupleData* const> tuples) {
  std::vector<std::shared_ptr<const TupleData>> ret;
  ret.reserve(tuples.size());
  for (const TupleData* tuple : tuples) {
    ret.push_back(std::make_shared<TupleData>(*tuple));
  }
  return ret;
}

// Returns shallow copies of 'tuples', which are still owned by the input
// vector.
inline std::vector<const TupleData*> StripSharedPtrs(
    absl::Span<const std::shared_ptr<const TupleData>> tuples) {
  std::vector<const TupleData*> ret;
  ret.reserve(tuples.size());
  for (const std::shared_ptr<const TupleData>& tuple : tuples) {
    ret.push_back(tuple.get());
  }
  return ret;
}

// Tracks the amount of memory used for tuples in places that accumulate a bunch
// of them.
class MemoryAccountant {
 public:
  // Constructs a MemoryAccountant that can allocate at most 'total_num_bytes'
  // at once.
  explicit MemoryAccountant(int64_t total_num_bytes,
                            absl::string_view name = "")
      : total_num_bytes_(total_num_bytes),
        remaining_bytes_(total_num_bytes),
        name_(name) {}

  MemoryAccountant(const MemoryAccountant&) = delete;
  MemoryAccountant& operator=(const MemoryAccountant&) = delete;
  ~MemoryAccountant() { ABSL_DCHECK_EQ(remaining_bytes_, total_num_bytes_); }

  // If there are 'num_bytes' available, updates the number of remaining bytes
  // accordingly and returns true. Else returns false and populates
  // 'status'. Does not return absl::Status for performance reasons.
  bool RequestBytes(int64_t num_bytes, absl::Status* status) {
    ABSL_DCHECK_GE(num_bytes, 0);
    if (num_bytes > remaining_bytes_) {
      *status = absl::ResourceExhaustedError(absl::Substitute(
          "Out of memory for MemoryAccountant($0): requested $1 bytes but only "
          "$2 are available out of a total of $3.",
          name_, num_bytes, remaining_bytes_, total_num_bytes_));

      return false;
    }
    remaining_bytes_ -= num_bytes;
    return true;
  }

  // Casts `num_bytes` to an int64_t and calls RequestBytes. Returns false and
  // populates `status` if the cast fails (ie: if `num_bytes` is too large) or
  // if RequestBytes returns false.
  bool RequestUInt64Bytes(uint64_t num_bytes, absl::Status* status);

  // Returns 'num_bytes' so they are available to future calls to
  // RequestBytes().
  void ReturnBytes(int64_t num_bytes) {
    remaining_bytes_ += num_bytes;
    ABSL_DCHECK_LE(remaining_bytes_, total_num_bytes_);
  }

  int64_t remaining_bytes() const { return remaining_bytes_; }

 private:
  const int64_t total_num_bytes_;
  int64_t remaining_bytes_;
  std::string name_;
};

// Holds a deque of TupleDatas whose memory usage is tracked by a
// MemoryAccountant, which is not owned by this object.
class TupleDataDeque {
 public:
  explicit TupleDataDeque(MemoryAccountant* accountant)
      : accountant_(accountant) {}

  TupleDataDeque(const TupleDataDeque&) = delete;
  TupleDataDeque& operator=(const TupleDataDeque&) = delete;

  ~TupleDataDeque() { Clear(); }

  bool IsEmpty() const { return datas_.empty(); }

  int64_t GetSize() const { return datas_.size(); }

  // Adds 'data' to the deque. Returns true on success. On failure, returns
  // false and populates 'status'. Any modifications to 'data' while it is in
  // this object are unaccounted for. This method does not return absl::Status
  // for performance reasons.
  bool PushBack(std::unique_ptr<TupleData> data, absl::Status* status) {
    const int64_t byte_size = data->GetPhysicalByteSize() + sizeof(Entry);
    if (!accountant_->RequestBytes(byte_size, status)) {
      return false;
    }
    datas_.emplace_back(byte_size, std::move(data));
    return true;
  }

  // Removes the front entry of the deque, which must be non-empty.
  std::unique_ptr<TupleData> PopFront() {
    Entry entry = std::move(datas_.front());
    datas_.pop_front();
    accountant_->ReturnBytes(entry.first);
    return std::move(entry.second);
  }

  // Clears the deque.
  void Clear() {
    while (!IsEmpty()) {
      PopFront();
    }
  }

  // Returns a vector of pointers to the owned tuples.
  std::vector<const TupleData*> GetTuplePtrs() const {
    std::vector<const TupleData*> ptrs;
    ptrs.reserve(datas_.size());
    for (const Entry& entry : datas_) {
      ptrs.push_back(entry.second.get());
    }
    return ptrs;
  }

  // Sets 'slot_idx' of the owned tuples according to 'values', which must have
  // the same number of entries as this object, and whose elements are moved
  // into the appropriate slots. Also updates the memory accountant accordingly.
  absl::Status SetSlot(int slot_idx, std::vector<Value> values);

  // Sorts the deque using std::sort or std::stable_sort.
  void Sort(const TupleComparator& comparator, bool use_stable_sort);

 private:
  // Stores a TupleData and its memory size.
  using Entry = std::pair<int64_t, std::unique_ptr<TupleData>>;

  MemoryAccountant* accountant_;

  // Stores TupleDatas and their memory sizes.
  std::deque<Entry> datas_;
};

// Represents an ordered queue of TupleDatas whose memory usage is tracked by a
// MemoryAccountant, which is not owned by this object.
class TupleDataOrderedQueue {
 public:
  TupleDataOrderedQueue(const TupleComparator& comparator,
                        MemoryAccountant* accountant)
      : accountant_(accountant),
        entries_(Comparator(
            [comparator](const TupleData* data1, const TupleData* data2) {
              return comparator(*data1, *data2);
            })) {}

  TupleDataOrderedQueue(const TupleDataOrderedQueue&) = delete;
  TupleDataOrderedQueue& operator=(const TupleDataOrderedQueue&) = delete;

  ~TupleDataOrderedQueue() { Clear(); }

  bool IsEmpty() const { return entries_.empty(); }

  int64_t GetSize() const { return entries_.size(); }

  // Adds 'data' to the queue. Returns true on success. On failure, returns
  // false and populates 'status'. Any modifications to 'data' while it is in
  // this object are unaccounted for. This method does not return absl::Status
  // for performance reasons.
  bool Insert(std::unique_ptr<TupleData> data, absl::Status* status) {
    const int64_t byte_size = data->GetPhysicalByteSize() +
                              sizeof(std::pair<const TupleData*, ValueEntry>);
    if (!accountant_->RequestBytes(byte_size, status)) {
      return false;
    }
    TupleData* ptr = data.get();
    entries_.emplace(ptr, std::make_pair(byte_size, std::move(data)));
    return true;
  }

  // Returns the first element of the queue, which must be non-empty.
  std::unique_ptr<TupleData> PopFront() {
    auto iter = entries_.begin();
    ValueEntry value_entry = std::move(iter->second);
    entries_.erase(iter);
    accountant_->ReturnBytes(value_entry.first);
    return std::move(value_entry.second);
  }

  // Returns the last element of the queue, which must be non-empty.
  std::unique_ptr<TupleData> PopBack() {
    auto iter = entries_.end();
    --iter;
    ValueEntry value_entry = std::move(iter->second);
    entries_.erase(iter);
    accountant_->ReturnBytes(value_entry.first);
    return std::move(value_entry.second);
  }

  // Clears the queue.
  void Clear() {
    while (!IsEmpty()) {
      PopFront();
    }
  }

 private:
  MemoryAccountant* accountant_;

  using Comparator = std::function<bool(const TupleData*, const TupleData*)>;
  // The key and value TupleData are the same. The int64 is the memory
  // reservation of the TupleData for 'accountant_'.
  using ValueEntry = std::pair<int64_t, std::unique_ptr<TupleData>>;
  // We use multimap because it is a sorted container that allows duplicates
  // and allows us to associate a payload for each item.
  std::multimap<const TupleData*, ValueEntry, Comparator> entries_;
};

// Represents a memory reservation on an accountant bytes already allocated by
// the caller.
// Frees the bytes in the destructor.
class MemoryReservation {
 public:
  // Constructs an empty MemoryReservation
  explicit MemoryReservation(MemoryAccountant* accountant)
      : accountant_(accountant), num_bytes_(0) {}

  // A memory reservation is moveable, but not copyable. Moving it transfers
  // ownership; copying id disallowed altogether to avoid double-free.
  MemoryReservation(const MemoryReservation&) = delete;
  MemoryReservation operator=(const MemoryReservation&) = delete;
  MemoryReservation(MemoryReservation&& reservation)
      : accountant_(reservation.accountant_),
        num_bytes_(reservation.num_bytes_) {
    // Prevent double free when original memory reservation is destroyed.
    // Also, avoid potential crash if the accountant is destroyed before the
    // original reservation.
    reservation.num_bytes_ = 0;
    reservation.accountant_ = nullptr;
  }

  // The destructor frees allocated bytes back to the memory accountant.
  ~MemoryReservation() {
    if (accountant_ != nullptr) {
      accountant_->ReturnBytes(num_bytes_);
    }
  }

  // Allocates <num_bytes> and updates the reservation accordingly.
  ABSL_MUST_USE_RESULT bool Increase(int64_t num_bytes, absl::Status* status) {
    bool success = accountant_->RequestBytes(num_bytes, status);
    if (success) {
      num_bytes_ += num_bytes;
    }
    return success;
  }

  // Allocates <num_bytes> and updates the reservation accordingly.
  ABSL_MUST_USE_RESULT bool IncreaseUInt64(uint64_t num_bytes,
                                           absl::Status* status) {
    bool success = accountant_->RequestUInt64Bytes(num_bytes, status);
    if (success) {
      // SAFETY: RequestUInt64Bytes returns false when `num_bytes` is too large
      // to be represented as an int64_t and when `num_bytes` is greater than
      // a certain int64_t threshold that decreases with each bytes requests.
      // Hence, this addition is safe and will not overflow.
      num_bytes_ += num_bytes;
    }
    return success;
  }

 private:
  MemoryAccountant* accountant_;
  int64_t num_bytes_;
};

// Helper class to keep track of a distinct set of TupleData's.
//
// Keeps track of all memory usage, using a MemoryAccountant, and will fail
// insert operations if the accountant does not have enough memory available.
// Used memory is freed back to the accountant in the destructor.
class DistinctRowSet {
 public:
  explicit DistinctRowSet(MemoryAccountant* accountant)
      : memory_reservation_(accountant) {}
  DistinctRowSet(const DistinctRowSet&) = delete;
  DistinctRowSet& operator=(const DistinctRowSet&) = delete;

  // Inserts a row into the row set, taking ownership of the given row.
  // - If successful, returns true.
  // - If the row duplicates any existing row in the row set, returns false
  // - If an error occurs (for example, if inserting the row would exceed
  //     memory limits), returns false and sets *status to a non-OK status
  //     describing the error.
  bool InsertRowIfNotPresent(std::unique_ptr<TupleData> row,
                             absl::Status* status) {
    if (!googlesql_base::InsertIfNotPresent(&rows_set_, TupleDataPtr(row.get()))) {
      // Duplicate; not inserted
      return false;
    }
    if (!memory_reservation_.Increase(row->GetPhysicalByteSize(), status)) {
      return false;
    }
    rows_.push_back(std::move(row));
    return true;
  }

 private:
  std::vector<std::unique_ptr<TupleData>> rows_;
  absl::flat_hash_set<TupleDataPtr> rows_set_;
  MemoryReservation memory_reservation_;
};

// Class for building an array value, tracking memory usage against a memory
// accountant.
class ArrayBuilder {
 public:
  // Represents a value, along with a memory reservation for it.
  struct TrackedValue {
    // A value
    Value value;

    // Memory reservation which owns the memory used by <value>.
    MemoryReservation reservation;
  };

  explicit ArrayBuilder(MemoryAccountant* accountant)
      : reservation_(accountant) {}

  bool IsEmpty() const { return values_.empty(); }
  size_t GetSize() const { return values_.size(); }

  // Moves the given value to the end of the array being built. Returns
  // false and sets *status to ResourceExhausted if the memory accountant does
  // not have space to hold the value.
  //
  // Note: The caller is responsible for ensuring that all values pushed are of
  // the same type. This is not verified here for performance reasons.
  ABSL_MUST_USE_RESULT bool PushBackUnsafe(Value&& value,
                                           absl::Status* status) {
    if (!reservation_.Increase(value.physical_byte_size(), status)) {
      return false;
    }
    values_.push_back(std::move(value));
    return true;
  }

  // Creates an array value representing the values previously passed to
  // PushBack(), returning a MemoryReservation to track freeing the value back
  // to the memory accountant.
  //
  // The size of returned memory reservation is the sum of the element sizes,
  // which is slightly less than the size of the returned array value. This
  // saves a couple of unnecessary allocate/free operations and the difference
  // is expected to be negligible.
  //
  // All values passed to PushBack() are expected to match type->element_type(),
  // however, for performance reasons, this is not verified at runtime.
  //
  // After return, the TrackedArrayBuilder is left in an invalid state, and
  // no more methods should be called on the object.
  ABSL_MUST_USE_RESULT TrackedValue Build(
      const ArrayType* type, InternalValue::OrderPreservationKind order_kind) {
    return TrackedValue{
        InternalValue::ArrayNotChecked(type, order_kind, std::move(values_)),
        std::move(reservation_)};
  }

 private:
  MemoryReservation reservation_;
  std::vector<Value> values_;
};

// Represents a hash set of values with memory tracked by a MemoryAccountant.
class ValueHashSet {
 public:
  explicit ValueHashSet(MemoryAccountant* accountant)
      : accountant_(accountant) {}

  ValueHashSet(const ValueHashSet&) = delete;
  ValueHashSet& operator=(const ValueHashSet&) = delete;

  ~ValueHashSet() { Clear(); }

  // If 'value' is in the underlying set, sets 'inserted' to false and returns
  // true. Otherwise requests bytes. If that succeeds, inserts 'value' into the
  // underlying set, sets 'inserted' to true, and returns false. Otherwise,
  // populates 'status' and returns false.
  bool Insert(const Value& value, bool* inserted, absl::Status* status) {
    *inserted = false;
    if (values_.contains(value)) {
      return true;
    }
    if (!accountant_->RequestBytes(value.physical_byte_size(), status)) {
      return false;
    }
    values_.insert(value);
    *inserted = true;
    return true;
  }

  // Clear the hash set.
  void Clear() {
    for (const Value& value : values_) {
      accountant_->ReturnBytes(value.physical_byte_size());
    }
    values_.clear();
  }

 private:
  MemoryAccountant* accountant_;
  absl::flat_hash_set<Value> values_;
};

// An iterator over TupleDatas. Particularly useful as a representation of a
// relation. Implementations must be thread compatible.
//
// Example:
// std::unique_ptr<TupleIterator> iter = ...
// while (true) {
//   const TupleData* data = iter->Next();
//   if (data == nullptr) {
//     GOOGLESQL_RET_CHECK_OK(iter->Status());
//     break;
//   }
//   ... Do something with 'data' ...
// }
class TupleIterator {
 public:
  virtual ~TupleIterator() = default;

  virtual const TupleSchema& Schema() const = 0;

  // Returns NULL if there is no next tuple or if there is an error (e.g.,
  // cancellation). In that case, the caller must call Status() to distinguish
  // between success and failure. The behavior of Next() is undefined after it
  // has returned NULL.
  //
  // The caller does not take ownership of the returned TupleData, and that
  // TupleData is guaranteed to remain valid until the next call to Next().
  //
  // The caller may not modify the number of slots in the return value or any of
  // the first 'Schema()->num_variables()' slots. The caller may modify
  // subsequent slots to allow stacked iterators to avoid copying the returned
  // TupleData into a wider TupleData with more slots.
  virtual TupleData* Next() = 0;

  // Returns the current status.
  virtual absl::Status Status() const = 0;

  // Returns false if this iterator is intentionally scrambling the order of
  // whatever it is iterating over. This should only be overridden by:
  // - ReorderingTupleIterator
  // - SortOpIterator (used by SortOp), to handle a case like
  //   SELECT x, y FROM T ORDER BY x, where the sort operation has to scramble
  //   the order of tuples (x1, y1), (x2, y2) where x1 = x2, but not tuples
  //   where x1 != x2.
  // - LetOpTupleIterator (used by LetOp), which just wraps another iterator.
  // - TestTupleIterator, which is a test-only class.
  virtual bool PreservesOrder() const { return true; }

  // Turns off reordering for this iterator (or does nothing if PreservesOrder()
  // returns true). Returns an error if called on an iterator that does not
  // preserve order after the first call to Next(). Useful, for example, for an
  // operator that must preserve some partially sorted property of its
  // input. Should only be overridden by iterators that override
  // PreservesOrder().
  virtual absl::Status DisableReordering() { return absl::OkStatus(); }

  // Returns a debug string that consists mostly of the kinds of the iterators
  // that are stacked (e.g.,
  // "FilterTupleIterator(ComputeTupleIterator(EvaluatorTableIterator))". In
  // most cases, more detailed information is available from the RelationalOp
  // corresponding to the iterator.
  virtual std::string DebugString() const = 0;
};

// Wraps another iterator and scrambles its order. The scrambling is
// deterministic to make issues easier to debug. There is some nontrivial
// processing involved so this iterator should only be used in tests.
class ReorderingTupleIterator : public TupleIterator {
 public:
  explicit ReorderingTupleIterator(std::unique_ptr<TupleIterator> iter)
      : iter_(std::move(iter)) {}

  ReorderingTupleIterator(const ReorderingTupleIterator&) = delete;
  ReorderingTupleIterator& operator=(const ReorderingTupleIterator&) = delete;

  const TupleSchema& Schema() const override { return iter_->Schema(); }

  TupleData* Next() override;

  absl::Status Status() const override { return status_; }

  bool PreservesOrder() const override { return !reorder_; }

  absl::Status DisableReordering() override {
    GOOGLESQL_RET_CHECK(!called_next_)
        << "DisableReordering() cannot be called after Next()";
    reorder_ = false;
    return absl::OkStatus();
  }

  std::string DebugString() const override {
    return absl::StrCat("ReorderingTupleIterator(", iter_->DebugString(), ")");
  }

 private:
  std::unique_ptr<TupleIterator> iter_;
  // If 'iter_' is done, contains its final status.
  std::optional<absl::Status> done_status_;
  std::vector<TupleData> current_batch_;
  int num_read_from_current_batch_ = 0;
  bool called_next_ = false;  // True if Next() was called at least once.
  bool reorder_ = true;
  absl::Status status_;
};

// An iterator that calls a user-supplied callback that creates an internal
// iterator on the first call to Next(), and returns all the tuples from that
// iterator. Useful for situations where constructing an iterator requires
// non-trivial computation that we'd like to be able to cancel (using the
// EvaluationContext, which is presumably known to the factory).
class PassThroughTupleIterator : public TupleIterator {
 public:
  // An IteratorFactory returns a TupleIterator or an error.
  using IteratorFactory =
      std::function<absl::StatusOr<std::unique_ptr<TupleIterator>>()>;
  // We avoid constructing the debug string where possible because that can be
  // expensive.
  using DebugStringFactory = std::function<std::string()>;

  PassThroughTupleIterator(const IteratorFactory& iterator_factory,
                           const TupleSchema& schema,
                           const DebugStringFactory& debug_string_factory)
      : iterator_factory_(iterator_factory),
        schema_(schema.variables()),
        debug_string_factory_(debug_string_factory) {}

  PassThroughTupleIterator(const PassThroughTupleIterator&) = delete;
  PassThroughTupleIterator& operator=(const PassThroughTupleIterator&) = delete;

  const TupleSchema& Schema() const override { return schema_; }

  TupleData* Next() override {
    if (iter_ == nullptr) {
      absl::StatusOr<std::unique_ptr<TupleIterator>> status_or_iter =
          iterator_factory_();
      if (!status_or_iter.ok()) {
        iterator_factory_status_ = status_or_iter.status();
        return nullptr;
      }
      iter_ = std::move(status_or_iter).value();
    }
    return iter_->Next();
  }

  absl::Status Status() const override {
    if (iter_ == nullptr) return iterator_factory_status_;
    return iter_->Status();
  }

  std::string DebugString() const override {
    return absl::StrCat("PassThroughTupleIterator(Factory for ",
                        debug_string_factory_(), ")");
  }

 private:
  const IteratorFactory iterator_factory_;
  const TupleSchema schema_;
  const DebugStringFactory debug_string_factory_;
  std::unique_ptr<TupleIterator> iter_;
  absl::Status iterator_factory_status_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_TUPLE_H_
