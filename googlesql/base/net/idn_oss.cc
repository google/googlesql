//
// Copyright 2020 Google LLC
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

#include "googlesql/base/net/idn_oss.h"

#include <string>

#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/logging.h"
#include "unicode/errorcode.h"
#include "unicode/idna.h"

namespace googlesql::internal {
namespace {

const icu::IDNA* CreateIdnaOrDie() {
  icu::ErrorCode error;
  const icu::IDNA* idna =
      icu::IDNA::createUTS46Instance(UIDNA_CHECK_BIDI, error);
  ABSL_CHECK(error.isSuccess()) << error.errorName();
  return idna;
}

}  // namespace

bool ToASCII(absl::string_view utf8_host, std::string* ascii_host) {
  static const icu::IDNA* idna = CreateIdnaOrDie();

  std::string tmp;
  icu::StringByteSink<std::string> sink(&tmp);

  icu::IDNAInfo info;
  icu::ErrorCode error;

  // This method is documented as being thread-safe.
  idna->nameToASCII_UTF8(utf8_host, sink, info, error);

  if (error.isSuccess() && !info.hasErrors()) {
    ascii_host->swap(tmp);
    return true;
  }

  if (GOOGLESQL_VLOG_IS_ON(1)) {
    ABSL_LOG(INFO) << "ToASCII error: " << error.errorName()
              << ", error bits: " << absl::StrFormat("0x%X", info.getErrors())
              << ", input: " << utf8_host;
  }

  error.reset();

  return false;
}

}  // namespace googlesql::internal
