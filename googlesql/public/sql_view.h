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

#ifndef GOOGLESQL_PUBLIC_SQL_VIEW_H_
#define GOOGLESQL_PUBLIC_SQL_VIEW_H_

#include "googlesql/public/catalog.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"

namespace googlesql {

class ResolvedScan;

// The SqlView interface is a sub-type of Table which represents views defined
// by a SQL SELECT query.
class SQLView : public Table {
 public:
  using SqlSecurity = ResolvedCreateStatementEnums::SqlSecurity;
  static constexpr SqlSecurity kSecurityUnspecified =
      ResolvedCreateStatementEnums::SQL_SECURITY_UNSPECIFIED;
  static constexpr SqlSecurity kSecurityDefiner =
      ResolvedCreateStatementEnums::SQL_SECURITY_DEFINER;
  static constexpr SqlSecurity kSecurityInvoker =
      ResolvedCreateStatementEnums::SQL_SECURITY_INVOKER;

  // The SQL SECURITY specified in when the view was created.
  virtual SqlSecurity sql_security() const = 0;

  // A ResolvedAST representation of the query definition. This ResolvedScan
  // should be equivalent to the ResolvedCreateViewStmt::query() member. It may
  // or may not have had some rewrites applied to it already.
  //
  // The lifetime of this ResolvedScan object must be at least as long as the
  // SQLView object that returns it. It may be owned by the SQLView object, the
  // catalog, or any datastructure with longer lifetime. The analyzer will make
  // deep copies of this object when the view is inlined into invoking
  // ResolvedAST fragments.
  virtual const ResolvedScan* view_query() const = 0;

  // When 'true', the GoogleSQL analyzer will inline the view body during
  // ResolvedAST rewrite.
  virtual bool enable_view_inline() const { return true; }
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_SQL_VIEW_H_
