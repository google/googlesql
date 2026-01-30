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

#ifndef GOOGLESQL_PUBLIC_TABLE_FROM_PROTO_H_
#define GOOGLESQL_PUBLIC_TABLE_FROM_PROTO_H_

#include <string>

#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/status.h"

namespace googlesql {

struct TableFromProtoOptions {
};

// This is an implementation of the googlesql::Table Catalog interface that
// gets its schema from a proto Descriptor.  This can be used for tables that
// are stored as protos.
// Based on googlesql annotations in the proto, this class will decide what
// column names and types are present in the table, and whether the table is a
// value table.
//
// Init must be called before returning this object from a Catalog lookup.
// SimpleTable methods like IsValueTable() and GetColumn() can be used to
// inspect the schema that was created.
//
// This class can be subclassed, or can be mutated after Init() to add
// additional behavior, like calling AddColumn to add pseudo-columns.
//
// This conversion is the inverse of ConvertTableToProto.  To round trip
// any GoogleSQL result (table) type through a proto encoding, generate the
// proto using ConvertTableToProto and then convert that proto back into
// a Catalog Table using TableFromProto.
class TableFromProto : public SimpleTable {
 public:
  explicit TableFromProto(absl::string_view name);
  TableFromProto(const TableFromProto&) = delete;
  TableFromProto& operator=(const TableFromProto&) = delete;
  ~TableFromProto() override;

  // Initialize this Table's schema from <descriptor>.
  // Types will be created in <type_factory>.
  // Should be called exactly once as part of construction.
  absl::Status Init(const google::protobuf::Descriptor* descriptor,
                    TypeFactory* type_factory,
                    const TableFromProtoOptions& options =
                        TableFromProtoOptions());
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TABLE_FROM_PROTO_H_
