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

#ifndef GOOGLESQL_TESTDATA_SAMPLE_CATALOG_H_
#define GOOGLESQL_TESTDATA_SAMPLE_CATALOG_H_

#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/type.h"
#include "googlesql/testdata/sample_catalog_impl.h"
#include "absl/status/status.h"

namespace googlesql {

// SampleCatalog provides a SimpleCatalog loaded with a shared sample schema,
// used by several tests.  Look at sample_catalog_impl.cc to see what's in the
// Catalog.
// All proto types compiled into this binary will be available in the catalog.
class SampleCatalog : public SampleCatalogImpl {
 public:
  // Default constructor using default LanguageOptions and a locally owned
  // TypeFactory.
  SampleCatalog();

  // Constructor given 'language_options' and an optional 'type_factory'.
  // If 'type_factory' is specified then it must outlive this SampleCatalog
  // and this SampleCatalog does not take ownership of it.  If 'type_factory'
  // is not specified then a locally owned TypeFactory is created and
  // used instead.
  explicit SampleCatalog(const LanguageOptions& language_options,
                         TypeFactory* type_factory = nullptr);

  // Constructor given 'builtin_function_options' and optional 'type_factory'.
  // If 'type_factory' is specified then it must outlive this SampleCatalog
  // and this SampleCatalog does not take ownership of it.  If 'type_factory'
  // is not specified then a locally owned TypeFactory is created and
  // used instead.
  explicit SampleCatalog(const BuiltinFunctionOptions& builtin_function_options,
                         TypeFactory* type_factory = nullptr);

  SampleCatalog(const SampleCatalog&) = delete;
  SampleCatalog& operator=(const SampleCatalog&) = delete;
  ~SampleCatalog() override = default;

 private:
  absl::Status LoadCatalogImpl(
      const GoogleSQLBuiltinFunctionOptions& builtin_function_options) = delete;
};

}  // namespace googlesql

#endif  // GOOGLESQL_TESTDATA_SAMPLE_CATALOG_H_
