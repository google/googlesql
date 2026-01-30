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

#include "googlesql/public/functions/hash.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "googlesql/base/logging.h"
#include "absl/base/casts.h"
#include "googlesql/base/check.h"
#include "absl/strings/string_view.h"
#include "openssl/md5.h"
#include "openssl/sha.h"
#include "googlesql/base/endian.h"
#include "farmhash.h"

namespace googlesql {
namespace functions {
namespace {

template <typename CtxT, int init_f(CtxT*),
          int update_f(CtxT*, const void*, size_t),
          int finalize_f(unsigned char*, CtxT*), int kDigestSize>
class OpenSSLHasher final : public Hasher {
 public:
  OpenSSLHasher() {
    static_assert(sizeof(digest_) == kDigestSize);
  }

  ~OpenSSLHasher() final {
    memset(&ctx_, 0, sizeof(ctx_));
    memset(&digest_, 0, sizeof(digest_));
  }

  std::string Hash(absl::string_view input) final {
    init_f(&ctx_);
    memset(digest_, 0, sizeof(digest_));
    ABSL_CHECK_EQ(update_f(&ctx_, input.data(), input.length()), 1);

    ABSL_CHECK_EQ(finalize_f(digest_, &ctx_), 1);
    return std::string(reinterpret_cast<const char*>(digest_), sizeof(digest_));
  }

 private:
  // Note: Neither of these values are really state of the class, rather, they
  // are used as buffers to avoid having to allocate on every call to `Hash()`.
  CtxT ctx_;
  uint8_t digest_[kDigestSize];
};

int Sha1FinalWithIllegalHashCheck(uint8_t out[SHA_DIGEST_LENGTH],
                                  SHA_CTX* ctx) {
  int ret_value = SHA1_Final(out, ctx);
  if (ret_value == 0) {
    return 0;
  }
  for (int i = 0; i < 3; ++i) {
    if (GOOGLESQL_INTERNAL_UNALIGNED_LOAD32(out + i * sizeof(uint32_t)) != 0) {
      return ret_value;
    }
  }
  // The first 12 bytes are 0 which makes this an "illegal" hash (based on) a
  // legacy definition.
  // A random string will meet this fate with a probability of 2^(-96) which
  // should be extremely rare. To make it legal, we just put a non-zero value in
  // each of the first 12 bytes.
  static_assert(SHA_DIGEST_LENGTH > 12);
  memcpy(out, "abcdefghijkl", 12);
  return ret_value;
}

using Md5Hasher =
    OpenSSLHasher<MD5_CTX, MD5_Init, MD5_Update, MD5_Final, MD5_DIGEST_LENGTH>;
using Sha1Hasher =
    OpenSSLHasher<SHA_CTX, SHA1_Init, SHA1_Update,
                  Sha1FinalWithIllegalHashCheck, SHA_DIGEST_LENGTH>;
using Sha256Hasher = OpenSSLHasher<SHA256_CTX, SHA256_Init, SHA256_Update,
                                   SHA256_Final, SHA256_DIGEST_LENGTH>;
using Sha512Hasher = OpenSSLHasher<SHA512_CTX, SHA512_Init, SHA512_Update,
                                   SHA512_Final, SHA512_DIGEST_LENGTH>;

}  // namespace

// static
std::unique_ptr<Hasher> Hasher::Create(Algorithm algorithm) {
  switch (algorithm) {
    case kMd5:
      return std::unique_ptr<Hasher>(new Md5Hasher);
    case kSha1:
      return std::unique_ptr<Hasher>(new Sha1Hasher);
    case kSha256:
      return std::unique_ptr<Hasher>(new Sha256Hasher);
    case kSha512:
      return std::unique_ptr<Hasher>(new Sha512Hasher);
  }
}

int64_t FarmFingerprint(absl::string_view input) {
  return absl::bit_cast<int64_t>(farmhash::Fingerprint64(input));
}

}  // namespace functions
}  // namespace googlesql
