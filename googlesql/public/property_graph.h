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

#ifndef GOOGLESQL_PUBLIC_PROPERTY_GRAPH_H_
#define GOOGLESQL_PUBLIC_PROPERTY_GRAPH_H_
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/type.h"
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {
class GraphElementTable;
class GraphNodeTableReference;
class Table;
class ResolvedExpr;
class GraphEdgeTable;
class GraphNodeTable;
class GraphPropertyDefinition;
class GraphElementLabel;
class GraphPropertyDeclaration;
class GraphDynamicLabel;
class GraphDynamicProperties;

ABSL_DEPRECATED(
    "Do not use this function. It exists only as a bridge until all callers of "
    "the API are migrated to use the new name_path.")
inline std::string QuoteIdentifier(absl::string_view name) {
  // TODO: when this is no longer used for lookup, we should return
  // it quoted, i.e.:
  // return absl::StrCat("`", name, "`");
  return std::string(name);
}

ABSL_DEPRECATED(
    "Do not use this function. It exists only as a bridge until all callers of "
    "the API are migrated to use the new name_path.")
inline std::string FullName(absl::Span<const std::string> name_path) {
  return absl::StrJoin(name_path, ".", [](std::string* out, const auto& s) {
    out->append(QuoteIdentifier(s));
  });
}

inline std::string FullName(absl::Span<const std::string> name_path_prefix,
                            absl::string_view name) {
  return absl::StrCat(FullName(name_path_prefix), ".", QuoteIdentifier(name));
}

// A property graph object visible in a GoogleSQL query. It shares the same
// namespace with Tables. The PropertyGraph owns unique instances of
// GraphElementTable(GraphNodeTable and GraphEdgeTable), GraphElementLabel and
// GraphPropertyDeclarations. Objects owned by a PropertyGraph may refer to
// other objects only within the same property graph. Eg, GraphElementLabel
// could refer to a set of GraphPropertyDeclarations, GraphEdgeTable
// could refer to GraphNodeTable as its source/destination GraphNodeReference
// etc.
//
class PropertyGraph {
 public:
  virtual ~PropertyGraph() = default;

  // Get the property graph name.
  virtual std::string Name() const { return NamePath().back(); }

  // Returns the name path, top-down and fully qualified.
  virtual absl::Span<const std::string> NamePath() const {
    ABSL_LOG(FATAL) << "Not implemented";  // Crash OK
  }

  // IMPORTANT: Intended for debugging only. DO NOT USE THIS FUNCTION for actual
  // logic. It is ambiguous as individual name parts may contain dots.
  //
  // Returns A representation of the fully-qualified name of this PropertyGraph,
  // including nested catalog names. Name scopes are delimited by periods. For
  // example, if name of the top level catalog is "bar", catalog "Foo" is nested
  // within "bar" and Graph "Baz" is defined in "Foo", the fully-qualified name
  // of the graph is then "bar.Foo.Baz".
  virtual std::string FullName() const {
    return ::googlesql::FullName(NamePath());
  }

  // Finds the GraphElementLabel in this PropertyGraph with the <name>,
  // Use name instead of fully-qualified name for the label.
  // Returns error status if there is no such GraphElementLabel.
  virtual absl::Status FindLabelByName(
      absl::string_view name, const GraphElementLabel*& label) const = 0;

  // Finds the GraphPropertyDeclaration with the <name> exposed by any
  // GraphElementLabel in this PropertyGraph.
  // This will include both scalar properties and measure properties.
  // Use name instead of fully-qualified name for the property declaration.
  // Returns error status if there is no such GraphPropertyDeclaration.
  virtual absl::Status FindPropertyDeclarationByName(
      absl::string_view name,
      const GraphPropertyDeclaration*& property_declaration) const = 0;

  // Finds the GraphElementTable in this PropertyGraph with the <name>,
  // Use name instead of fully-qualified name for the table.
  // Returns error status if there is no such GraphElementTable.
  virtual absl::Status FindElementTableByName(
      absl::string_view name,
      const GraphElementTable*& element_table) const = 0;

  // Returns all GraphNodeTables owned by this PropertyGraph.
  virtual absl::Status GetNodeTables(
      absl::flat_hash_set<const GraphNodeTable*>& output) const = 0;

  // Returns all GraphEdgeTables owned by this PropertyGraph.
  virtual absl::Status GetEdgeTables(
      absl::flat_hash_set<const GraphEdgeTable*>& output) const = 0;

  // Returns all GraphElementLabels owned by this PropertyGraph.
  virtual absl::Status GetLabels(
      absl::flat_hash_set<const GraphElementLabel*>& output) const = 0;

  // Returns all GraphPropertyDeclarations owned by this PropertyGraph.
  // This will include both scalar properties and measure properties.
  virtual absl::Status GetPropertyDeclarations(
      absl::flat_hash_set<const GraphPropertyDeclaration*>& output) const = 0;

  // Returns whether or not this PropertyGraph is a specific property graph
  // interface or implementation.
  template <class PropertyGraphSubclass>
  bool Is() const {
    return dynamic_cast<const PropertyGraphSubclass*>(this) != nullptr;
  }

  // Returns this PropertyGraph as PropertyGraphSubclass*. Must only be
  // used when it is known that the object *is* this subclass, which can be
  // checked using Is() before calling GetAs().
  template <class PropertyGraphSubclass>
  const PropertyGraphSubclass* GetAs() const {
    return static_cast<const PropertyGraphSubclass*>(this);
  }
};

// Represents an element (node or edge) table in a property graph.
//
// A GraphElementTable exposes a set of GraphElementLabels and owns a set of
// GraphPropertyDefinitions, which 1:1 define the exposed
// GraphPropertyDeclarations by its GraphElementLabel set.
class GraphElementTable {
 public:
  enum class Kind { kNode, kEdge };
  enum class DynamicLabelCardinality { kUnknown, kSingle, kMultiple };
  virtual ~GraphElementTable() = default;

  // Returns the name which is a unique identifier of a GraphElementTable in
  // the property graph.
  virtual std::string Name() const = 0;

  // Returns the name path, top-down and fully qualified.
  virtual absl::Span<const std::string> PropertyGraphNamePath() const {
    ABSL_LOG(FATAL) << "Not implemented";  // Crash OK
  }

  // IMPORTANT: Intended for debugging only. DO NOT USE THIS FUNCTION for actual
  // logic. It is ambiguous as individual name parts may contain dots.
  //
  // Returns A representation of the fully-qualified name of this
  // GraphElementTable, including property graph name.
  virtual std::string FullName() const {
    return ::googlesql::FullName(PropertyGraphNamePath(), Name());
  }

  // Returns the kind of this ElementTable.
  virtual GraphElementTable::Kind kind() const = 0;
  virtual const GraphEdgeTable* AsEdgeTable() const { return nullptr; }
  virtual const GraphNodeTable* AsNodeTable() const { return nullptr; }

  // Returns the table identified by the <element table name> in a
  // <element table definition>.
  virtual const Table* GetTable() const = 0;

  // Returns the ordinal indexes of key columns. Key columns are specified from
  // graph DDL statements or implicitly the PK columns of the underlying table.
  virtual const std::vector<int>& GetKeyColumns() const = 0;

  // Finds the GraphPropertyDefinition in this GraphElementTable, which maps to
  // a GraphPropertyDeclaration with the <property_name>. Returns error status
  // if GraphElementTable does not have any GraphPropertyDefinition mapping to a
  // GraphPropertyDeclaration by this name.
  virtual absl::Status FindPropertyDefinitionByName(
      absl::string_view property_name,
      const GraphPropertyDefinition*& property_definition) const = 0;

  // Returns all GraphPropertyDefinitions exposed by GraphElementLabels on this
  // GraphElementTable.
  virtual absl::Status GetPropertyDefinitions(
      absl::flat_hash_set<const GraphPropertyDefinition*>& output) const = 0;

  // Finds the GraphElementLabel on this GraphElementTable with the <name>.
  // Use name instead of fully-qualified name for the label.
  // Returns error status if there is no such GraphElementLabel.
  virtual absl::Status FindLabelByName(
      absl::string_view name, const GraphElementLabel*& label) const = 0;

  // Returns all GraphElementLabels exposed by this GraphElementTable.
  virtual absl::Status GetLabels(
      absl::flat_hash_set<const GraphElementLabel*>& output) const = 0;

  // Returns true if this GraphElementTable has a dynamic label.
  virtual bool HasDynamicLabel() const { return false; }
  // Returns the dynamic label cardinality.
  virtual enum GraphElementTable::DynamicLabelCardinality
  DynamicLabelCardinality() const {
    return DynamicLabelCardinality::kUnknown;
  }
  // Returns the dynamic label of this GraphElementTable.
  virtual absl::Status GetDynamicLabel(
      const GraphDynamicLabel*& dynamic_label) const {
    ABSL_LOG(FATAL) << "Not implemented";  // Crash OK
  }

  // Returns true if this GraphElementTable has dynamic properties.
  virtual bool HasDynamicProperties() const { return false; }
  // Returns the dynamic properties of this GraphElementTable.
  virtual absl::Status GetDynamicProperties(
      const GraphDynamicProperties*& dynamic_properties) const {
    ABSL_LOG(FATAL) << "Not implemented";  // Crash OK
  }
  // Returns whether or not this GraphElementTable is a specific
  // interface or implementation.
  template <class GraphElementTableSubclass>
  bool Is() const {
    return dynamic_cast<const GraphElementTableSubclass*>(this) != nullptr;
  }

  // Returns this GraphElementTable as GraphElementLabelSubclass*. Must only be
  // used when it is known that the object *is* this subclass, which can be
  // checked using Is() before calling GetAs().
  template <class GraphElementTableSubclass>
  const GraphElementTableSubclass* GetAs() const {
    return static_cast<const GraphElementTableSubclass*>(this);
  }
};

class GraphNodeTable : public GraphElementTable {
 public:
  GraphElementTable::Kind kind() const override { return Kind::kNode; }
  const GraphNodeTable* AsNodeTable() const override { return this; }
};

class GraphEdgeTable : public GraphElementTable {
 public:
  GraphElementTable::Kind kind() const override { return Kind::kEdge; }
  const GraphEdgeTable* AsEdgeTable() const override { return this; }

  // Returns the source GraphNodeTableReference.
  virtual const GraphNodeTableReference* GetSourceNodeTable() const = 0;

  // Returns the destination GraphNodeTableReference.
  virtual const GraphNodeTableReference* GetDestNodeTable() const = 0;
};

// Represents a label in a property graph.
//
// Each GraphElementLabel could expose a set of GraphPropertyDeclarations.
class GraphElementLabel {
 public:
  virtual ~GraphElementLabel() = default;

  // Returns the name of this Label.
  virtual std::string Name() const = 0;

  // Returns the name path, top-down and fully qualified.
  virtual absl::Span<const std::string> PropertyGraphNamePath() const {
    ABSL_LOG(FATAL) << "Not implemented";  // Crash OK
  }

  // IMPORTANT: Intended for debugging only. DO NOT USE THIS FUNCTION for actual
  // logic. It is ambiguous as individual name parts may contain dots.
  //
  // Returns A representation of the fully-qualified name, including property
  // graph name.
  virtual std::string FullName() const {
    return ::googlesql::FullName(PropertyGraphNamePath(), Name());
  }

  // Returns pointers to all GraphPropertyDeclarations exposed by this.
  // GraphElementLabel. The PropertyGraph owns GraphPropertyDeclarations.
  virtual absl::Status GetPropertyDeclarations(
      absl::flat_hash_set<const GraphPropertyDeclaration*>& output) const = 0;

  // Returns whether or not this GraphElementLabel is a specific
  // interface or implementation.
  template <class GraphElementLabelSubclass>
  bool Is() const {
    return dynamic_cast<const GraphElementLabelSubclass*>(this) != nullptr;
  }

  // Returns this GraphElementLabel as GraphElementLabelSubclass*. Must only be
  // used when it is known that the object *is* this subclass, which can be
  // checked using Is() before calling GetAs().
  template <class GraphElementLabelSubclass>
  const GraphElementLabelSubclass* GetAs() const {
    return static_cast<const GraphElementLabelSubclass*>(this);
  }
};

// Represents how a GraphEdgeTable references a GraphNodeTable as one of the
// endpoints.
//
class GraphNodeTableReference {
 public:
  virtual ~GraphNodeTableReference() = default;

  // Returns the referenced GraphNodeTable.
  virtual const GraphNodeTable* GetReferencedNodeTable() const = 0;

  // Returns ordinal indexes of referencing columns from the GraphEdgeTable that
  // references the GraphNodeTable columns returned by
  // GetGraphNodeTableColumns().
  virtual const std::vector<int>& GetEdgeTableColumns() const = 0;

  // Returns ordinal indexes of columns from the referenced GraphNodeTable.
  virtual const std::vector<int>& GetNodeTableColumns() const = 0;

  // Returns this GraphNodeTableReference as GraphNodeTableReferenceSubclass*.
  // Must only be used when it is known that the object *is* this subclass,
  // which can be checked using Is() before calling GetAs().
  template <class GraphNodeTableReferenceSubclass>
  const GraphNodeTableReferenceSubclass* GetAs() const {
    return static_cast<const GraphNodeTableReferenceSubclass*>(this);
  }
};

// Represents a property declaration in a property graph.
//
// A GraphPropertyDeclaration could be exposed by one or more
// GraphElementLabels. Within a property graph, there is one
// GraphPropertyDeclaration for each unique property name. It guarantees the
// consistency of property declaration.
class GraphPropertyDeclaration {
 public:
  enum class Kind { kInvalid, kScalar, kMeasure };

  virtual ~GraphPropertyDeclaration() = default;

  virtual std::string Name() const = 0;

  virtual GraphPropertyDeclaration::Kind kind() const { return Kind::kInvalid; }

  // Returns the owning graph's name path, top-down and fully qualified.
  virtual absl::Span<const std::string> PropertyGraphNamePath() const {
    ABSL_LOG(FATAL) << "Not implemented";  // Crash OK
  }

  // IMPORTANT: Intended for debugging only. DO NOT USE THIS FUNCTION for actual
  // logic. It is ambiguous as individual name parts may contain dots.
  //
  // Returns A representation of the fully-qualified name, including property
  // graph name.
  virtual std::string FullName() const {
    return ::googlesql::FullName(PropertyGraphNamePath(), Name());
  }

  virtual const Type* Type() const = 0;

  // TODO: This should be made a pure virtual function once all subclasses
  // implement it.
  virtual const AnnotationMap* TypeAnnotationMap() const { return nullptr; }

  AnnotatedType GetAnnotatedType() const {
    return AnnotatedType(Type(), TypeAnnotationMap());
  }

  // Returns whether or not this GraphPropertyDeclaration is a specific
  // interface or implementation.
  template <class GraphPropertyDeclarationSubclass>
  bool Is() const {
    return dynamic_cast<const GraphPropertyDeclarationSubclass*>(this) !=
           nullptr;
  }

  // Returns this GraphPropertyDeclaration as GraphPropertyDeclarationSubclass*.
  // Must only be used when it is known that the object *is* this subclass,
  // which can be checked using Is() before calling GetAs().
  template <class GraphPropertyDeclarationSubclass>
  const GraphPropertyDeclarationSubclass* GetAs() const {
    return static_cast<const GraphPropertyDeclarationSubclass*>(this);
  }
};

// Represents a property definition in a GraphElementTable.
//
// GraphPropertyDefinition describes how a GraphPropertyDeclaration is defined
// within a GraphElementTable with the value expression.
class GraphPropertyDefinition {
 public:
  virtual ~GraphPropertyDefinition() = default;
  virtual const GraphPropertyDeclaration& GetDeclaration() const = 0;

  virtual absl::string_view expression_sql() const = 0;

  // Value expression of how property declarations are defined in the containing
  // GraphElementTable. The columns are represented by ResolvedCatalogColumnRef
  // instead of regular ResolvedColumnRef.
  virtual absl::StatusOr<const ResolvedExpr*> GetValueExpression() const = 0;

  // Returns whether or not this GraphPropertyDefinition is a specific
  // interface or implementation.
  template <class GraphPropertyDefinitionSubclass>
  bool Is() const {
    return dynamic_cast<const GraphPropertyDefinitionSubclass*>(this) !=
           nullptr;
  }

  // Returns this GraphPropertyDefinition as GraphPropertyDefinitionSubclass*.
  // Must only be used when it is known that the object *is* this subclass,
  // which can be checked using Is() before calling GetAs().
  template <class GraphPropertyDefinitionSubclass>
  const GraphPropertyDefinitionSubclass* GetAs() const {
    return static_cast<const GraphPropertyDefinitionSubclass*>(this);
  }
};

class GraphDynamicLabel {
 public:
  virtual ~GraphDynamicLabel() = default;

  // IMPORTANT: Intended for debugging only. DO NOT USE THIS FUNCTION for actual
  // logic. It is ambiguous as it may contain dots, as part of a path
  // expression.
  //
  // Returns a SQL expression of the dynamic label column, as part of its
  // definition.
  virtual absl::string_view label_expression() const = 0;

  virtual absl::StatusOr<const ResolvedExpr*> GetValueExpression() const = 0;

  template <class GraphDynamicLabelSubclass>
  bool Is() const {
    return dynamic_cast<const GraphDynamicLabelSubclass*>(this) != nullptr;
  }
  template <class GraphDynamicLabelSubclass>
  const GraphDynamicLabelSubclass* GetAs() const {
    return static_cast<const GraphDynamicLabelSubclass*>(this);
  }
};

class GraphDynamicProperties {
 public:
  virtual ~GraphDynamicProperties() = default;

  // IMPORTANT: Intended for debugging only. DO NOT USE THIS FUNCTION for actual
  // logic. It is ambiguous as it may contain dots, as part of a path
  // expression.
  //
  // Returns a SQL expression of the dynamic properties column, as part of its
  // definition.
  virtual absl::string_view properties_expression() const = 0;

  virtual absl::StatusOr<const ResolvedExpr*> GetValueExpression() const = 0;

  template <class GraphDynamicPropertiesSubclass>
  bool Is() const {
    return dynamic_cast<const GraphDynamicPropertiesSubclass*>(this) != nullptr;
  }
  template <class GraphDynamicPropertiesSubclass>
  const GraphDynamicPropertiesSubclass* GetAs() const {
    return static_cast<const GraphDynamicPropertiesSubclass*>(this);
  }
};

}  // namespace googlesql
#endif  // GOOGLESQL_PUBLIC_PROPERTY_GRAPH_H_
