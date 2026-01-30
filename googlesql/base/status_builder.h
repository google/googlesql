//
// Copyright 2018 Google LLC
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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_STATUS_BUILDER_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_STATUS_BUILDER_H_

#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/logging.h"
#include "googlesql/base/source_location.h"
#include "googlesql/base/status_payload.h"

namespace googlesql_base {

// Creates a status based on an original_status, but enriched with additional
// information.  The builder implicitly converts to Status and StatusOr<T>
// allowing for it to be returned directly.
//
//   StatusBuilder builder(original, GOOGLESQL_LOC);
//   builder.Attach(proto);
//   builder << "info about error";
//   return builder;
//
// It provides method chaining to simplify typical usage:
//
//   return StatusBuilder(original, GOOGLESQL_LOC)
//       .Log(base_logging::WARNING) << "oh no!";
//
// In more detail:
// - When the original status is OK, all methods become no-ops and nothing will
//   be logged.
// - Messages streamed into the status builder are collected into a single
//   additional message string.
// - The original Status's message and the additional message are joined
//   together when the result status is built.
// - By default, the messages will be joined with a convenience separator
//   between the original message and the additional one.  This behavior can be
//   changed with the `SetAppend()` and `SetPrepend()` methods of the builder.
// - By default, the result status is not logged (but see `Log` method).
// - All side effects (like logging) happen when the builder is converted to a
//   status.
class ABSL_MUST_USE_RESULT StatusBuilder {
 public:
  // Creates a `StatusBuilder` from an StatusCode.  If logging is enabled,
  // it will use `location` as the location from which the log message occurs.
  StatusBuilder(absl::StatusCode code, googlesql_base::SourceLocation location =
                                           SourceLocation::current());

  // Creates a `StatusBuilder` based on an original status.  If logging is
  // enabled, it will use `location` as the location from which the log message
  // occurs.
  StatusBuilder(
      const absl::Status& original_status,
      googlesql_base::SourceLocation location = SourceLocation::current());
  StatusBuilder(
      absl::Status&& original_status,
      googlesql_base::SourceLocation location = SourceLocation::current());

  StatusBuilder(const StatusBuilder& sb);
  StatusBuilder& operator=(const StatusBuilder& sb);
  StatusBuilder(StatusBuilder&&) = default;
  StatusBuilder& operator=(StatusBuilder&&) = default;

  // Mutates the builder so that the final additional message is prepended to
  // the original error message in the status.  A convenience separator is not
  // placed between the messages.
  //
  // NOTE: Multiple calls to `SetPrepend` and `SetAppend` just adjust the
  // behavior of the final join of the original status with the extra message.
  //
  // Returns `*this` to allow method chaining.
  StatusBuilder& SetPrepend();

  // Mutates the builder so that the final additional message is appended to the
  // original error message in the status.  A convenience separator is not
  // placed between the messages.
  //
  // NOTE: Multiple calls to `SetPrepend` and `SetAppend` just adjust the
  // behavior of the final join of the original status with the extra message.
  //
  // Returns `*this` to allow method chaining.
  StatusBuilder& SetAppend();

  // Mutates the builder to disable any logging that was set using any of the
  // logging functions below.  Returns `*this` to allow method chaining.
  StatusBuilder& SetNoLogging();

  // Mutates the builder so that the result status will be logged (without a
  // stack trace) when this builder is converted to a Status.  This overrides
  // the logging settings from earlier calls to any of the logging mutator
  // functions.  Returns `*this` to allow method chaining.
  StatusBuilder& Log(absl::LogSeverity level);
  StatusBuilder& LogError() { return Log(absl::LogSeverity::kError); }
  StatusBuilder& LogWarning() { return Log(absl::LogSeverity::kWarning); }
  StatusBuilder& LogInfo() { return Log(absl::LogSeverity::kInfo); }

  // Mutates the builder so that a stack trace will be logged if the status is
  // logged. One of the logging setters above should be called as well. If
  // logging is not yet enabled this behaves as if LogInfo().EmitStackTrace()
  // was called. Returns `*this` to allow method chaining.
  StatusBuilder& EmitStackTrace();

  // Appends to the extra message that will be added to the original status.  By
  // default, the extra message is added to the original message with a
  // separator (';') between the original message and the enriched one.
  template <typename T>
  StatusBuilder& operator<<(const T& value);

  // Attaches a proto containing additional details about the error.
  // Returns `*this` to allow method chaining.
  template <typename T>
  StatusBuilder& Attach(const T& data) {
    return AttachPayload<T>(data);
  }

  // Attaches a proto containing additional details about the error.
  // Returns `*this` to allow method chaining.
  template <typename T>
  StatusBuilder& AttachPayload(const T& data);

  // Sets the error code for the status that will be returned by this
  // StatusBuilder.  Returns `*this` to allow method chaining.
  StatusBuilder& SetErrorCode(absl::StatusCode code);

  // Sets the status code for the status that will be returned by this
  // StatusBuilder. Returns `*this` to allow method chaining.
  StatusBuilder& SetCode(absl::StatusCode code) &;
  ABSL_MUST_USE_RESULT StatusBuilder&& SetCode(absl::StatusCode code) &&;

  ///////////////////////////////// Adaptors /////////////////////////////////
  //
  // A StatusBuilder `adaptor` is a functor which can be included in a builder
  // method chain. There are two common variants:
  //
  // 1. `Pure policy` adaptors modify the StatusBuilder and return the modified
  //    object, which can then be chained with further adaptors or mutations.
  //
  // 2. `Terminal` adaptors consume the builder's Status and return some
  //    other type of object. Alternatively, the consumed Status may be used
  //    for side effects, e.g. by passing it to a side channel. A terminal
  //    adaptor cannot be chained.
  //
  // Careful: The conversion of StatusBuilder to Status has side effects!
  // Adaptors must ensure that this conversion happens at most once in the
  // builder chain. The easiest way to do this is to determine the adaptor type
  // and follow the corresponding guidelines:
  //
  // Pure policy adaptors should:
  // (a) Take a StatusBuilder as input parameter.
  // (b) NEVER convert the StatusBuilder to Status:
  //     - Never assign the builder to a Status variable.
  //     - Never pass the builder to a function whose parameter type is Status,
  //       including by reference (e.g. const Status&).
  //     - Never pass the builder to any function which might convert the
  //       builder to Status (i.e. this restriction is viral).
  // (c) Return a StatusBuilder (usually the input parameter).
  //
  // Terminal adaptors should:
  // (a) Take a Status as input parameter (not a StatusBuilder!).
  // (b) Return a type matching the enclosing function. (This can be `void`.)
  //
  // Adaptors do not necessarily fit into one of these categories. However, any
  // which satisfies the conversion rule can always be decomposed into a pure
  // adaptor chained into a terminal adaptor. (This is recommended.)
  //
  // Examples
  //
  // Pure adaptors allow teams to configure team-specific error handling
  // policies.  For example:
  //
  //   StatusBuilder TeamPolicy(StatusBuilder builder) {
  //     AttachPayload(&builder, ...);
  //     return std::move(builder).Log(base_logging::WARNING);
  //   }
  //
  //   GOOGLESQL_RETURN_IF_ERROR(foo()).With(TeamPolicy);
  //
  // Because pure policy adaptors return the modified StatusBuilder, they
  // can be chained with further adaptors, e.g.:
  //
  //   GOOGLESQL_RETURN_IF_ERROR(foo()).With(TeamPolicy).With(OtherTeamPolicy);
  //
  // Terminal adaptors are often used for type conversion. This allows
  // GOOGLESQL_RETURN_IF_ERROR to be used in functions which do not return Status. For
  // example, a function might want to return some default value on error:
  //
  //   int GetSysCounter() {
  //     int value;
  //     GOOGLESQL_RETURN_IF_ERROR(ReadCounterFile(filename, &value))
  //         .LogInfo()
  //         .With([](const absl::Status& unused) { return 0; });
  //     return value;
  //   }

  // Calls `adaptor` on this status builder to apply policies, type conversions,
  // and/or side effects on the StatusBuilder. Returns the value returned by
  // `adaptor`, which may be any type including `void`. See comments above.
  //
  template <typename Adaptor>
  auto With(Adaptor&& adaptor) & -> decltype(
      std::forward<Adaptor>(adaptor)(*this)) {
    return std::forward<Adaptor>(adaptor)(*this);
  }
  template <typename Adaptor>
  auto With(Adaptor&& adaptor) && -> decltype(
      std::forward<Adaptor>(adaptor)(std::move(*this))) {
    return std::forward<Adaptor>(adaptor)(std::move(*this));
  }

  // Returns true if the Status created by this builder will be ok().
  bool ok() const;

  // Returns the code for the Status created by this builder.
  absl::StatusCode code() const;

  // Returns true iff the status created by this builder will have the given
  // `code`.
  //
  // `StatusBuilder(Status(code, "")).Is(code)` is always true. In particular,
  // if the `code` is zero, returns true if `status_builder.ok()`.
  // Sample usage:
  //
  //   StatusBuilder TeamPolicy(StatusBuilder builder) {
  //     if (builder.Is(StatusCode::kCancelled)) {
  //       builder.Log(base_logging::WARNING);
  //     }
  //     return std::move(builder);
  //   }
  //
  ABSL_DEPRECATED("Use code() == code instead")
  ABSL_MUST_USE_RESULT bool Is(absl::StatusCode code) const;

  // Implicit conversion to Status.
  //
  // Careful: this operator has side effects, so it should be called at
  // most once.
  //
  // This override allows us to implement GOOGLESQL_RETURN_IF_ERROR with 2 move
  // operations in the common case.
  operator absl::Status() const&;  // NOLINT
  operator absl::Status() &&;

  template <typename T>
  operator absl::StatusOr<T>() const&;  // NOLINT

  template <typename T>
  operator absl::StatusOr<T>() &&;  // NOLINT

  // Returns the source location used to create this builder.
  googlesql_base::SourceLocation source_location() const;

 private:
  // Specifies how to join the error message in the original status and any
  // additional message that has been streamed into the builder.
  enum class MessageJoinStyle {
    kAnnotate,
    kAppend,
    kPrepend,
  };

  // Creates a new status based on an old one by joining the message from the
  // original to an additional message.
  static absl::Status JoinMessageToStatus(absl::Status s, absl::string_view msg,
                                          MessageJoinStyle style);

  // Creates a Status from this builder and logs it if the builder has been
  // configured to log itself.
  absl::Status CreateStatusAndConditionallyLog() &&;

  // Conditionally logs if the builder has been configured to log.  This method
  // is split from the above to isolate the portability issues around logging
  // into a single place.
  void ConditionallyLog(const absl::Status& result) const;

  // Infrequently set builder options, instantiated lazily. This reduces
  // average construction/destruction time (e.g. the `stream` is fairly
  // expensive). Stacks can also be blown if StatusBuilder grows too large.
  // This is primarily an issue for debug builds, which do not necessarily
  // re-use stack space within a function across the sub-scopes used by
  // status macros.
  struct Rep {
    explicit Rep() = default;
    Rep(const Rep& r);

    enum class LoggingMode {
      kDisabled,
      kLog,
    };
    LoggingMode logging_mode = LoggingMode::kDisabled;

    // Corresponds to the levels in `base_logging::LogSeverity`. Only used when
    // `logging_mode == LoggingMode::kLog`.
    absl::LogSeverity log_severity;

    // The level at which the Status should be VLOGged.
    // Only used when `logging_mode == LoggingMode::kVLog`.
    int verbose_level;

    // Gathers additional messages added with `<<` for use in the final status.
    std::ostringstream stream;

    // Whether to log stack trace.  Only used when `logging_mode !=
    // LoggingMode::kDisabled`.
    bool should_log_stack_trace = false;

    // Specifies how to join the message in `status_` and `stream`.
    MessageJoinStyle message_join_style = MessageJoinStyle::kAnnotate;
  };

  // The status that the result will be based on.  Can be modified by Attach().
  absl::Status status_;

  googlesql_base::SourceLocation location_;

  // nullptr if the result status will be OK.  Extra fields moved to the heap to
  // minimize stack space.
  std::unique_ptr<Rep> rep_;
};

// Each of the functions below creates StatusBuilder with a canonical error.
// The error code of the StatusBuilder matches the name of the function.
StatusBuilder AbortedErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder AlreadyExistsErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder CancelledErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder DataLossErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder DeadlineExceededErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder FailedPreconditionErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder InternalErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder InvalidArgumentErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder NotFoundErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder OutOfRangeErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder PermissionDeniedErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder UnauthenticatedErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder ResourceExhaustedErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder UnavailableErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder UnimplementedErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());
StatusBuilder UnknownErrorBuilder(
    googlesql_base::SourceLocation location = SourceLocation::current());

inline StatusBuilder::StatusBuilder(absl::StatusCode code,
                                    googlesql_base::SourceLocation location)
    : status_(code, ""), location_(location) {}

inline StatusBuilder::StatusBuilder(const absl::Status& original_status,
                                    googlesql_base::SourceLocation location)
    : status_(original_status), location_(location) {}

inline StatusBuilder::StatusBuilder(absl::Status&& original_status,
                                    googlesql_base::SourceLocation location)
    : status_(std::move(original_status)), location_(location) {}

inline StatusBuilder::StatusBuilder(const StatusBuilder& sb)
    : status_(sb.status_), location_(sb.location_) {
  if (sb.rep_ != nullptr) {
    rep_.reset(new Rep(*sb.rep_));
  }
}

inline StatusBuilder& StatusBuilder::operator=(const StatusBuilder& sb) {
  status_ = sb.status_;
  location_ = sb.location_;
  if (sb.rep_ != nullptr) {
    rep_.reset(new Rep(*sb.rep_));
  } else {
    rep_ = nullptr;
  }
  return *this;
}

inline StatusBuilder& StatusBuilder::SetPrepend() {
  if (status_.ok()) return *this;
  if (rep_ == nullptr) rep_.reset(new Rep());

  rep_->message_join_style = MessageJoinStyle::kPrepend;
  return *this;
}

inline StatusBuilder& StatusBuilder::SetAppend() {
  if (status_.ok()) return *this;
  if (rep_ == nullptr) rep_.reset(new Rep());
  rep_->message_join_style = MessageJoinStyle::kAppend;
  return *this;
}

inline StatusBuilder& StatusBuilder::SetNoLogging() {
  if (rep_ != nullptr) {
    rep_->logging_mode = Rep::LoggingMode::kDisabled;
  }
  return *this;
}

inline StatusBuilder& StatusBuilder::Log(absl::LogSeverity level) {
  if (status_.ok()) return *this;
  if (rep_ == nullptr) rep_.reset(new Rep());
  rep_->logging_mode = Rep::LoggingMode::kLog;
  rep_->log_severity = level;
  rep_->should_log_stack_trace = false;
  return *this;
}

inline StatusBuilder& StatusBuilder::EmitStackTrace() {
  if (status_.ok()) return *this;
  if (rep_ == nullptr) {
    rep_.reset(new Rep());
    rep_->logging_mode = Rep::LoggingMode::kLog;
    rep_->log_severity = absl::LogSeverity::kInfo;
  }
  rep_->should_log_stack_trace = true;
  return *this;
}

// Implicitly converts `builder` to `Status` and write it to `os`.
inline std::ostream& operator<<(std::ostream& os,
                                const StatusBuilder& builder) {
  return os << static_cast<absl::Status>(builder);
}

template <typename T>
StatusBuilder& StatusBuilder::operator<<(const T& value) {
  if (status_.ok()) return *this;
  if (rep_ == nullptr) rep_.reset(new Rep());
  rep_->stream << value;
  return *this;
}

inline bool StatusBuilder::ok() const { return status_.ok(); }

inline absl::StatusCode StatusBuilder::code() const { return status_.code(); }

inline bool StatusBuilder::Is(absl::StatusCode status_code) const {
  return status_.code() == status_code;
}

inline StatusBuilder::operator absl::Status() const& {
  if (rep_ == nullptr) return status_;
  return StatusBuilder(*this).CreateStatusAndConditionallyLog();
}

inline StatusBuilder::operator absl::Status() && {
  if (rep_ == nullptr) return std::move(status_);
  return std::move(*this).CreateStatusAndConditionallyLog();
};

template <typename T>
inline StatusBuilder::operator absl::StatusOr<T>() const& {
  if (rep_ == nullptr) return absl::StatusOr<T>(status_);
  return absl::StatusOr<T>(
      StatusBuilder(*this).CreateStatusAndConditionallyLog());
}

template <typename T>
inline StatusBuilder::operator absl::StatusOr<T>() && {
  if (rep_ == nullptr) return std::move(status_);
  return std::move(*this).CreateStatusAndConditionallyLog();
}

inline googlesql_base::SourceLocation StatusBuilder::source_location() const {
  return location_;
}

// Attaches a proto containing additional details about the error.
// Returns `*this` to allow method chaining.
template <typename T>
StatusBuilder& StatusBuilder::AttachPayload(const T& data) {
  googlesql_base::AttachPayload<T>(&status_, data);
  return *this;
}

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_STATUS_BUILDER_H_
