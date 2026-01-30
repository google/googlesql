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

#include "googlesql/testdata/sample_catalog.h"

#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/testdata/sample_catalog_impl.h"
#include "googlesql/base/check.h"

namespace googlesql {

SampleCatalog::SampleCatalog() : SampleCatalogImpl() {
  GOOGLESQL_CHECK_OK(SampleCatalogImpl::LoadCatalogImpl(
      GoogleSQLBuiltinFunctionOptions(LanguageOptions())));
}

// Constructor given 'language_options' and an optional 'type_factory'.
// If 'type_factory' is specified then it must outlive this SampleCatalog
// and this SampleCatalog does not take ownership of it.  If 'type_factory'
// is not specified then a locally owned TypeFactory is created and
// used instead.
SampleCatalog::SampleCatalog(const LanguageOptions& language_options,
                             TypeFactory* type_factory)
    : SampleCatalog(BuiltinFunctionOptions(language_options), type_factory) {}

// Constructor given 'builtin_function_options' and optional 'type_factory'.
// If 'type_factory' is specified then it must outlive this SampleCatalog
// and this SampleCatalog does not take ownership of it.  If 'type_factory'
// is not specified then a locally owned TypeFactory is created and
// used instead.
SampleCatalog::SampleCatalog(
    const BuiltinFunctionOptions& builtin_function_options,
    TypeFactory* type_factory)
    : SampleCatalogImpl(type_factory) {
  GOOGLESQL_CHECK_OK(SampleCatalogImpl::LoadCatalogImpl(builtin_function_options));
}

}  // namespace googlesql
