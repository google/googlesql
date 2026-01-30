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

// Matchers are classes that are constructed with a condition to match, then
// their Matches() method is called repeatedly with a candidate to check if it
// meets the condition set in the constructor. All matchers are derived from the
// MatcherBase<CandidateT> class (where CandidateT is the candidate type).
// At any time, the MatchCount() method will retrieve how many times a candidate
// has successfully matched the condition and the MatcherSummary() will return
// a human readable summary of the matches that is specific to each matcher.
//
// Matchers are often more useful when grouped into a MatcherCollection. This
// Matcher takes ownership of a vector of Matchers in it's constructor, which
// will be called composed matchers. When Matches() is called on the
// MatcherCollection all composed matchers are checked. If any of the composed
// matchers matched, true is returned to the caller. When MatcherSummary() is
// called the report contains the overall number of matches, followed by the
// MatcherSummary() from each composed matcher.

#ifndef GOOGLESQL_COMPLIANCE_MATCHERS_H_
#define GOOGLESQL_COMPLIANCE_MATCHERS_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/fixed_array.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "re2/re2.h"
#include "googlesql/base/status.h"

namespace googlesql {

// Base class for all Matchers. Defines the matcher interface.
template <typename CandidateT>
class MatcherBase {
 public:
  MatcherBase() {}
  MatcherBase(const MatcherBase&) = delete;
  MatcherBase& operator=(const MatcherBase&) = delete;
  virtual ~MatcherBase() {}

  // Returns true if 'candidate' matches this matcher. If Matches returns true
  // the match is counted and included in the MatcherSummary() and MatchCount().
  bool Matches(const CandidateT& candidate) {
    if (MatchesImpl(candidate)) {
      ++match_count_;
      return true;
    }
    return false;
  }

  // Returns the human readable name of this matcher.
  virtual std::string MatcherName() const = 0;

  // Returns a human readable summary of all candidates this matcher has
  // matched.
  virtual std::string MatcherSummary() const = 0;

  // Returns the total number of times candidates have matched.
  virtual int MatchCount() const { return match_count_; }

  // Returns true if the matcher has matched any candidates.
  virtual bool HasMatches() const { return match_count_ > 0; }

 protected:
  // Returns true if 'candidate' matches this matcher. If MatchesImpl returns
  // true the match count is incremented.
  virtual bool MatchesImpl(const CandidateT& candidate) = 0;

  void IncrementMatchCount(int i = 1) { match_count_ += i; }

 private:
  // The total number of times candidates have matched.
  int match_count_ = 0;
};

// Matcher that contains a collections of other matchers to check. A candidate
// is considered matching if any of the matchers in the collection match.
// 'CandidateT' is the type of the candidate you want to call Matches() on and
// all matchers in the collection should match that type.
template <typename CandidateT>
class MatcherCollection : public MatcherBase<CandidateT> {
 public:
  // 'collection_name' is used in the matcher name for identification purposes.
  // This class will take ownership of the matchers in 'matchers'. Those
  // matchers are then checked for each candidate passed to Matches().
  MatcherCollection(
      absl::string_view collection_name,
      std::vector<std::unique_ptr<MatcherBase<CandidateT>>> matchers,
      bool is_conjunction = false)
      : collection_name_(collection_name),
        matchers_(std::move(matchers)),
        is_conjunction_(is_conjunction) {}

  MatcherCollection(const MatcherCollection&) = delete;
  MatcherCollection& operator=(const MatcherCollection&) = delete;

  // Returns a human readable summary of all candidates that matched. This is
  // produced by including the MatcherSummary() from each matcher in the
  // collection.
  std::string MatcherSummary() const override {
    std::string summary = absl::StrCat(
        "Matched ", MatcherBase<CandidateT>::MatchCount(), " candidate(s).\n");

    for (int matcher_idx = 0; matcher_idx < matchers_.size(); ++matcher_idx) {
      if (matchers_[matcher_idx]->HasMatches()) {
        absl::StrAppend(&summary, std::string(80, '='), "\n",
                        matchers_[matcher_idx]->MatcherName(), ":\n",
                        matchers_[matcher_idx]->MatcherSummary());
      }
    }
    return summary;
  }

  // Returns the human readable matcher name.
  std::string MatcherName() const override {
    return absl::StrCat(collection_name_,
                        is_conjunction_ ? " conjunction " : "",
                        " matcher collection");
  }

  void AddMatcher(std::unique_ptr<MatcherBase<CandidateT>> matcher) {
    matchers_.push_back(std::move(matcher));
  }

 protected:
  // Returns true if any matcher in the collection matches 'candidate'. As long
  // as at least one matcher matches, the match count is incremented once.
  bool MatchesImpl(const CandidateT& candidate) final {
    bool any_match = false;
    bool all_match = true;
    for (int matcher_idx = 0; matcher_idx < matchers_.size(); ++matcher_idx) {
      if (matchers_[matcher_idx]->Matches(candidate)) {
        any_match = true;
      } else {
        all_match = false;
      }
    }
    return is_conjunction_ ? all_match : any_match;
  }

 private:
  const std::string collection_name_;
  std::vector<std::unique_ptr<MatcherBase<CandidateT>>> matchers_;
  const bool is_conjunction_;
};

// Matches a given CanonicalCode.
class ErrorCodeMatcher : public MatcherBase<absl::StatusCode> {
 public:
  explicit ErrorCodeMatcher(absl::StatusCode matching_error_code)
      : matching_error_code_(matching_error_code) {}

  ErrorCodeMatcher(const ErrorCodeMatcher&) = delete;
  ErrorCodeMatcher& operator=(const ErrorCodeMatcher&) = delete;

  // Returns a human readable summary of all matching candidates.
  std::string MatcherSummary() const override {
    return absl::StrCat("Matched ", MatchCount(), " time(s).\n");
  }

  // Returns a human readable matcher name.
  std::string MatcherName() const override {
    return absl::StrCat("Error code ",
                        absl::StatusCodeToString(matching_error_code_),
                        " matcher");
  }

  // Returns the matching error code.
  absl::StatusCode matching_error_code() const { return matching_error_code_; }

 protected:
  // Checks if 'candidate' matches the CanonicalCode provided in the
  // constructor. If 'candidate' matches the match count is incremented and
  // true is returned.
  bool MatchesImpl(const absl::StatusCode& candidate) final {
    return candidate == matching_error_code_;
  }

 private:
  const absl::StatusCode matching_error_code_;
};

// Negates the result of a Matches call on 'nested_matcher'.
template <typename CandidateT>
class NotMatcher : public MatcherBase<CandidateT> {
 public:
  explicit NotMatcher(std::unique_ptr<MatcherBase<CandidateT>> nested_matcher)
      : nested_matcher_(std::move(nested_matcher)) {}

  NotMatcher(const NotMatcher&) = delete;
  NotMatcher& operator=(const NotMatcher&) = delete;

  // Returns a human readable summary of the nested matcher.
  std::string MatcherSummary() const override {
    return nested_matcher_->MatcherSummary();
  }

  // Returns a human readable name for this matcher.
  std::string MatcherName() const override {
    return absl::StrCat("NOT ", nested_matcher_->MatcherName());
  }

 protected:
  // Matches 'candidate'
  bool MatchesImpl(const CandidateT& candidate) final {
    return !nested_matcher_->Matches(candidate);
  }

 private:
  std::unique_ptr<MatcherBase<CandidateT>> nested_matcher_;
};

// Matches a regex to a candidate string. Can also match the contents of capture
// groups against provided matchers.
class RegexMatcher : public MatcherBase<std::string> {
 public:
  // 're2_pattern' is the RE2 pattern to check candidates against. The first 100
  // matches to each group in the pattern will be stored for later summary.
  explicit RegexMatcher(absl::string_view re2_pattern)
      : RegexMatcher(re2_pattern, {}, 100 /* max_samples */) {}

  // 're2_pattern' is the RE2 pattern to check candidates against. The first 100
  // matches to each group in the pattern will be stored for later summary.
  // group_matcher is a map of capture group position (0 based) to a matcher
  // that should be called with the group contents. Only if all group matchers
  // also match will this matcher match.
  RegexMatcher(
      absl::string_view re2_pattern,
      std::map<int, std::unique_ptr<MatcherBase<std::string>>> group_matchers)
      : RegexMatcher(re2_pattern, std::move(group_matchers),
                     100 /* max_samples */) {}

  // 're2_pattern' is the RE2 pattern to check candidates against. The first
  // 'max_samples' matches to each group in the pattern will be stored for later
  // summary.
  // group_matcher is a map of capture group position (0 based) to a matcher
  // that should be called with the group contents. Only if all group matchers
  // also match will this matcher match.
  RegexMatcher(
      absl::string_view re2_message_pattern,
      std::map<int, std::unique_ptr<MatcherBase<std::string>>> group_matchers,
      int max_samples);

  RegexMatcher(const RegexMatcher&) = delete;
  RegexMatcher& operator=(const RegexMatcher&) = delete;

  // Returns a human readable summary of the candidate strings that have
  // matched. This summary includes samples of each group in the regex and the
  // summary of each group matcher.
  std::string MatcherSummary() const override;

  // Returns a human readable name for this matcher.
  std::string MatcherName() const override {
    return absl::StrCat("Regex pattern \"", re2_message_pattern_.pattern(),
                        "\" matcher");
  }

  // Convenience overload to support absl::string_view
  bool Matches(absl::string_view candidate) {
    if (MatchesStringViewImpl(candidate)) {
      IncrementMatchCount();
      return true;
    }
    return false;
  }

 protected:
  bool MatchesImpl(const std::string& candidate) final {
    return MatchesStringViewImpl(candidate);
  }

  // Matches 'candidate' against the re2_pattern from the constructor. If
  // 'candidate' matches the match count is incremented and the matching groups
  // may be sampled.
  bool MatchesStringViewImpl(absl::string_view candidate);

 private:
  const RE2 re2_message_pattern_;
  const std::map<int, std::unique_ptr<MatcherBase<std::string>>>
      group_matchers_;
  const int group_cnt_;
  const int max_samples_;
  absl::FixedArray<std::map<std::string, int>> groups_list_;
  absl::FixedArray<RE2::Arg> argv_;
  absl::FixedArray<std::string> string_groups_;
  std::unique_ptr<RE2::Arg* []> args_;
};

// Matches a substring to a candidate string.
class SubstringMatcher : public MatcherBase<std::string> {
 public:
  // 'matching_substring' is the substring to check candidates against. The
  // first 100 matches will be stored for later summary.
  explicit SubstringMatcher(absl::string_view matching_substring)
      : SubstringMatcher(matching_substring, 100 /* max_samples */) {}

  // 'matching_substring' is the substring to check candidates against. The
  // first 'max_samples' matches will be stored for later summary.
  SubstringMatcher(absl::string_view matching_substring, int max_samples)
      : max_samples_(max_samples),
        matching_substring_(std::string(matching_substring)),
        matching_substring_view_(matching_substring_) {}

  SubstringMatcher(const SubstringMatcher&) = delete;
  SubstringMatcher& operator=(const SubstringMatcher&) = delete;

  std::string MatcherSummary() const override;

  // Returns a human readable name for this matcher.
  std::string MatcherName() const override {
    return absl::StrCat("Substring pattern \"", matching_substring_,
                        "\" matcher");
  }

  // Convenience overload to support absl::string_view
  bool Matches(absl::string_view candidate) {
    if (MatchesStringViewImpl(candidate)) {
      IncrementMatchCount();
      return true;
    }
    return false;
  }

 protected:
  bool MatchesImpl(const std::string& candidate) final {
    return MatchesStringViewImpl(candidate);
  }

  // Matches 'candidate' against the matching_substring from the constructor. If
  // 'candidate' matches the match count is incremented and the matching string
  // may be sampled.
  bool MatchesStringViewImpl(absl::string_view candidate);

 private:
  const int max_samples_;
  const std::string matching_substring_;
  const absl::string_view matching_substring_view_;
  std::map<std::string, int> matching_strings_;
};

// Matcher that checks if a candidate status CanonicalCode matches an
// ErrorCodeMatcher as well as the candidate status error_message matches a
// RegexMatcher.
class StatusRegexMatcher : public MatcherBase<absl::Status> {
 public:

  // Each candidate status's CanonicalCode will be matched to a ErrorCodeMatcher
  // for the provided 'matching_error_code' and the error_message will be
  // matched to a RegexMatcher for the provided 're2_message_pattern'. The first
  // 100 matches will be sampled for later summary.
  StatusRegexMatcher(absl::StatusCode matching_error_code,
                     absl::string_view re2_message_pattern)
      : StatusRegexMatcher(matching_error_code, re2_message_pattern,
                           100 /* max_samples */) {}

  // Each candidate status's CanonicalCode will be matched to a ErrorCodeMatcher
  // for the provided 'matching_error_code' and the error_message will be
  // matched to a RegexMatcher for the provided 're2_message_pattern'. The first
  // 100 matches will be sampled for later summary.
  // group_matcher is a map of capture group position (0 based) to a matcher
  // that should be called with the group contents. Only if all group matchers
  // also match will this matcher match.
  StatusRegexMatcher(
      absl::StatusCode matching_error_code,
      absl::string_view re2_message_pattern,
      std::map<int, std::unique_ptr<MatcherBase<std::string>>> group_matchers)
      : StatusRegexMatcher(matching_error_code, re2_message_pattern,
                           std::move(group_matchers), 100 /* max_samples */) {}

  // Each candidate status's CanonicalCode will be matched to a ErrorCodeMatcher
  // for the provided 'matching_error_code' and the error_message will be
  // matched to a RegexMatcher for the provided 're2_message_pattern'. The first
  // 'max_samples' matches will be sampled for later summary.
  StatusRegexMatcher(absl::StatusCode matching_error_code,
                     absl::string_view re2_message_pattern, int max_samples)
      : error_code_matcher_(matching_error_code),
        regex_matcher_(re2_message_pattern, {}, max_samples) {}

  // Each candidate status's CanonicalCode will be matched to a ErrorCodeMatcher
  // for the provided 'matching_error_code' and the error_message will be
  // matched to a RegexMatcher for the provided 're2_message_pattern'. The first
  // 'max_samples' matches will be sampled for later summary.
  // group_matcher is a map of capture group position (0 based) to a matcher
  // that should be called with the group contents. Only if all group matchers
  // also match will this matcher match.
  StatusRegexMatcher(
      absl::StatusCode matching_error_code,
      absl::string_view re2_message_pattern,
      std::map<int, std::unique_ptr<MatcherBase<std::string>>> group_matchers,
      int max_samples)
      : error_code_matcher_(matching_error_code),
        regex_matcher_(re2_message_pattern, std::move(group_matchers),
                       max_samples) {}

  StatusRegexMatcher(const StatusRegexMatcher&) = delete;
  StatusRegexMatcher& operator=(const StatusRegexMatcher&) = delete;

  // Returns a human readable summary of a sample of the candidates that have
  // matched.
  std::string MatcherSummary() const override {
    return absl::StrCat(regex_matcher_.MatcherSummary());
  }

  // Returns a human readable name for this matcher.
  std::string MatcherName() const override {
    return absl::StrCat("Status matcher with ",
                        error_code_matcher_.MatcherName(), " and ",
                        regex_matcher_.MatcherName());
  }

 protected:
  // Each candidate status's CanonicalCode will be matched to a ErrorCodeMatcher
  // for the matching_error_code provided in the constuctor and the
  // error_message will be matched to a RegexMatcher for the re2_message_pattern
  // provided in the constructor. The matches may be sampled for later summary.
  bool MatchesImpl(const absl::Status& candidate) final;

 private:
  ErrorCodeMatcher error_code_matcher_;
  RegexMatcher regex_matcher_;
};

// Matcher that checks if a candidate status CanonicalCode matches an
// ErrorCodeMatcher as well as the candidate status error_message matches a
// SubstringMatcher.
class StatusSubstringMatcher : public MatcherBase<absl::Status> {
 public:

  // Each candidate status's CanonicalCode will be matched to a ErrorCodeMatcher
  // for the provided 'matching_error_code' and the error_message will be
  // matched to a SubstringMatcher for the provided 'matching_substring'. The
  // first 100 matches will be sampled for later summary.
  StatusSubstringMatcher(absl::StatusCode matching_error_code,
                         absl::string_view matching_substring)
      : StatusSubstringMatcher(matching_error_code, matching_substring,
                               100 /* max_samples */) {}

  // Each candidate status's CanonicalCode will be matched to a ErrorCodeMatcher
  // for the provided 'matching_error_code' and the error_message will be
  // matched to a SubstringMatcher for the provided 'matching_substring'. The
  // first 'max_samples' matches will be sampled for later summary.
  StatusSubstringMatcher(absl::StatusCode matching_error_code,
                         absl::string_view matching_substring, int max_samples)
      : error_code_matcher_(matching_error_code),
        substring_matcher_(matching_substring, max_samples) {}

  StatusSubstringMatcher(const StatusSubstringMatcher&) = delete;
  StatusSubstringMatcher& operator=(const StatusSubstringMatcher&) = delete;

  // Returns a human readable summary of a sample of the candidates that have
  // matched.
  std::string MatcherSummary() const override {
    return absl::StrCat(substring_matcher_.MatcherSummary());
  }

  // Returns a human readable name for this matcher.
  std::string MatcherName() const override {
    return absl::StrCat("Status matcher with ",
                        error_code_matcher_.MatcherName(), " and ",
                        substring_matcher_.MatcherName());
  }

 protected:
  // Each candidate status's CanonicalCode will be matched to a ErrorCodeMatcher
  // for the matching_error_code provided in the constuctor and the
  // error_message will be matched to a SubstringMatcher for the
  // matching_substring provided in the constructor. The matches may be sampled
  // for later summary.
  bool MatchesImpl(const absl::Status& candidate) final;

 private:
  ErrorCodeMatcher error_code_matcher_;
  SubstringMatcher substring_matcher_;
};

// Matcher that checks if a candidate status CanonicalCode matches an
// ErrorCodeMatcher.
class StatusErrorCodeMatcher : public MatcherBase<absl::Status> {
 public:

  // Each candidate status's CanonicalCode will be matched to a ErrorCodeMatcher
  // for the provided 'matching_error_code'. The first 100 matches will be
  // sampled for later summary.
  explicit StatusErrorCodeMatcher(absl::StatusCode matching_error_code)
      : error_code_matcher_(matching_error_code, "") {}

  // Each candidate status's CanonicalCode will be matched to a ErrorCodeMatcher
  // for the provided 'matching_error_code'. The first 'max_samples' matches
  // will be sampled for later summary.
  StatusErrorCodeMatcher(absl::StatusCode matching_error_code, int max_samples)
      : error_code_matcher_(matching_error_code, "", max_samples) {}

  StatusErrorCodeMatcher(const StatusErrorCodeMatcher&) = delete;
  StatusErrorCodeMatcher& operator=(const StatusErrorCodeMatcher&) = delete;

  // Returns a human readable summary of a sample of the candidates that have
  // matched.
  std::string MatcherSummary() const override {
    return error_code_matcher_.MatcherSummary();
  }

  // Returns a human readable name for this matcher.
  std::string MatcherName() const override {
    return absl::StrCat(error_code_matcher_.MatcherName(), " status matcher");
  }

 protected:
  // Each candidate status's CanonicalCode will be matched to a ErrorCodeMatcher
  // for the matching_error_code provided in the constuctor. The matches may be
  // sampled for later summary.
  bool MatchesImpl(const absl::Status& candidate) final;

 private:
  StatusSubstringMatcher error_code_matcher_;
};

// Matcher that checks if a bool proto field has the default value and the
// candidate matches the submatcher.
// TProto is a Proto type with a bool field with a name equal to
// 'proto_bool_field_name' from the constructor.
// TCandidate is the type of matcher of the submatcher to check if the proto
// fields value doesn't match the default value.
template <typename TProto, typename TCandidate>
class ProtoFieldIsDefaultMatcher
    : public MatcherBase<std::pair<TProto, TCandidate>> {
 public:
  // Construtor that takes the name of a bool field of TProto and takes
  // ownership of a submatch of TCandidates.
  ProtoFieldIsDefaultMatcher(
      absl::string_view proto_bool_field_name,
      std::unique_ptr<MatcherBase<TCandidate>> submatcher)
      : proto_bool_field_name_(proto_bool_field_name),
        submatcher_(std::move(submatcher)) {}

  ProtoFieldIsDefaultMatcher(const ProtoFieldIsDefaultMatcher&) = delete;
  ProtoFieldIsDefaultMatcher& operator=(const ProtoFieldIsDefaultMatcher&) =
      delete;

  // Returns a human readable summary of the candidates that have matched.
  std::string MatcherSummary() const override {
    return absl::StrCat("Default True, Value False: ", true_false_count_,
                        " Default False, Value True: ", false_true_count_, "\n",
                        submatcher_->MatcherSummary());
  }

  // Returns a human readable name for this matcher.
  std::string MatcherName() const override {
    return absl::StrCat("BoolProtoField ", proto_bool_field_name_,
                        " is default matcher with ",
                        submatcher_->MatcherName());
  }

 protected:
  // Each candidate proto's proto_bool_field_name from the constructor must
  // match the default value and the candidate must match the submatcher.
  bool MatchesImpl(const std::pair<TProto, TCandidate>& candidate) final {
    const google::protobuf::FieldDescriptor* bool_field =
        candidate.first.GetDescriptor()->FindFieldByName(
            proto_bool_field_name_);
    if (bool_field == nullptr) {
      return false;
    }
    if (bool_field->type() == google::protobuf::FieldDescriptor::TYPE_BOOL) {
      bool default_value = bool_field->default_value_bool();
      if (default_value !=
          candidate.first.GetReflection()->GetBool(candidate.first,
                                                   bool_field)) {
        if (submatcher_->Matches(candidate.second)) {
          if (default_value == false) {
            ++false_true_count_;
          } else {
            ++true_false_count_;
          }
          return true;
        }
      }
    }
    return false;
  }

 private:
  const std::string proto_bool_field_name_;
  int64_t true_false_count_ = 0;
  int64_t false_true_count_ = 0;
  std::unique_ptr<MatcherBase<TCandidate>> submatcher_;
};

}  // namespace googlesql
#endif  // GOOGLESQL_COMPLIANCE_MATCHERS_H_
