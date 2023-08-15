/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_UTIL_JSON_UTIL_H_
#define SERVICES_COMMON_UTIL_JSON_UTIL_H_

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/pointer.h"
#include "rapidjson/writer.h"

namespace privacy_sandbox::bidding_auction_servers {

// This is a custom class that implements the necessary methods for
// serialization of a Rapid JSON document in a shared string.
class SharedStringHolder {
 public:
  using Ch = char;  // Character type for the stream

  SharedStringHolder() : shared_string_(std::make_shared<std::string>()) {}
  explicit SharedStringHolder(int size)
      : shared_string_(std::make_shared<std::string>()) {
    shared_string_->reserve(size);
  }

  ~SharedStringHolder() { Flush(); }

  void Put(Ch c) { shared_string_->push_back(c); }

  void Clear() { shared_string_->clear(); }
  void Flush() { return; }
  size_t Size() const { return shared_string_->size(); }

  std::string GetString() const { return *shared_string_; }

  std::shared_ptr<std::string> GetSharedPointer() const {
    return shared_string_;
  }

 private:
  std::shared_ptr<std::string> shared_string_;
};

// Parse string into a rapidjson::Document. Returns error status or document.
inline absl::StatusOr<rapidjson::Document> ParseJsonString(
    absl::string_view str) {
  rapidjson::Document doc;
  // Parse into struct
  rapidjson::ParseResult parse_result =
      doc.Parse<rapidjson::kParseFullPrecisionFlag>(str.data());
  if (parse_result.IsError()) {
    return absl::InvalidArgumentError(
        absl::StrCat("JSON Parse Error: ",
                     rapidjson::GetParseError_En(parse_result.Code())));
  }
  return doc;
}

// Converts rapidjson::Document to a shared string. This provides a
// shared string to prevent copying large string parameters required
// by the ROMA engine interface. The reserve_string_len argument helps
// reserve a large string size up front to prevent reallocation and copying.
inline absl::StatusOr<std::shared_ptr<std::string>> SerializeJsonDoc(
    const rapidjson::Document& document, int reserve_string_len) {
  SharedStringHolder shared_string_holder(reserve_string_len);
  rapidjson::Writer<SharedStringHolder> writer(shared_string_holder);
  if (document.Accept(writer)) {
    return shared_string_holder.GetSharedPointer();
  }
  return absl::InternalError("Unknown JSON to string serialization error");
}

// Converts rapidjson::Value& to a string
inline absl::StatusOr<std::string> SerializeJsonDoc(
    const rapidjson::Value& document) {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);
  if (document.Accept(writer)) {
    return std::string(string_buffer.GetString());
  }
  return absl::InternalError("Error converting inner Json to String.");
}

// Converts rapidjson::Document to a string.
inline absl::StatusOr<std::string> SerializeJsonDoc(
    const rapidjson::Document& document) {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);
  if (document.Accept(writer)) {
    return std::string(string_buffer.GetString());
  }
  return absl::InternalError("Unknown JSON to string serialization error");
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_JSON_UTIL_H_
