// Copyright 2023 Google LLC
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

#include "services/seller_frontend_service/util/web_utils.h"

#include <optional>
#include <set>
#include <string>
#include <utility>

#include "absl/numeric/bits.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "api/bidding_auction_servers.pb.h"
#include "glog/logging.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "services/common/compression/gzip.h"
#include "services/common/util/status_macros.h"

#include "cbor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using BiddingGroupMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using InteractionUrlMap = ::google::protobuf::Map<std::string, std::string>;
using ErrorHandler = const std::function<void(absl::string_view)>&;
using RepeatedStringProto = ::google::protobuf::RepeatedPtrField<std::string>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = absl::flat_hash_map<absl::string_view, BuyerInput>;

// Array for mapping from the CBOR data type enum (a number) to a concrete data
// type. Used for returning helpful error messages when clients incorrectly
// construct the CBOR payload. For the ordering of the data types, see:
// https://github.com/PJK/libcbor/blob/master/src/cbor/data.h
inline constexpr int kNumCborDataTypes = 8;
inline constexpr std::array<absl::string_view, kNumCborDataTypes>
    kCborDataTypesLookup = {
        kCborTypePositiveInt,   // CBOR_TYPE_UINT (positive integers)
        kCborTypeNegativeInt,   // CBOR_TYPE_NEGINT (negative integers)
        kCborTypeByteString,    // CBOR_TYPE_BYTESTRING
        kCborTypeString,        // CBOR_TYPE_STRING
        kCborTypeArray,         // CBOR_TYPE_ARRAY
        kCborTypeMap,           // CBOR_TYPE_MAP
        kCborTypeTag,           // CBOR_TYPE_TAG
        kCborTypeFloatControl,  // CBOR_TYPE_FLOAT_CTRL ("decimals and special
                                // values")
};

struct cbor_pair BuildCborKVPair(absl::string_view key,
                                 absl::string_view value) {
  return {.key = cbor_move(cbor_build_stringn(key.data(), key.size())),
          .value = cbor_move(cbor_build_stringn(value.data(), value.size()))};
}

absl::Status AddKVToMap(absl::string_view key, absl::string_view value,
                        ErrorHandler error_handler, cbor_item_t& map) {
  if (!cbor_map_add(&map, BuildCborKVPair(key, value))) {
    error_handler(absl::StrCat("Failed to serialize ", key, " to CBOR"));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

// Decodes a Span of cbor* string objects and adds them to the provided list.
RepeatedStringProto DecodeStringArray(absl::Span<cbor_item_t*> span,
                                      absl::string_view field_name,
                                      ErrorAccumulator& error_accumulator,
                                      bool fail_fast) {
  RepeatedStringProto repeated_field;
  for (const cbor_item_t* ad : span) {
    bool is_valid = IsTypeValid(&cbor_isa_string, ad, field_name, kString,
                                error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, repeated_field);
    if (is_valid) {
      repeated_field.Add(DecodeCborString(ad));
    }
  }

  return repeated_field;
}

// Collects the prevWins arrays into a JSON array and stringifies the result.
absl::StatusOr<std::string> GetStringifiedPrevWins(
    absl::Span<cbor_item_t*> prev_wins_entries, absl::string_view owner,
    ErrorAccumulator& error_accumulator, bool fail_fast) {
  rapidjson::Document document;
  document.SetArray();
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

  // Previous win entries should be in the form [relative_time, ad_render_id]
  // where relative_time is an int and ad_render_id is a string.
  for (const cbor_item_t* prev_win : prev_wins_entries) {
    bool is_valid = IsTypeValid(&cbor_isa_array, prev_win, kPrevWinsEntry,
                                kArray, error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, "");
    if (!is_valid) {
      continue;
    }

    if (cbor_array_size(prev_win) != 2) {
      const std::string error =
          absl::StrFormat(kPrevWinsNotCorrectLengthError, owner);
      error_accumulator.ReportError(ErrorVisibility::CLIENT_VISIBLE, error,
                                    ErrorCode::CLIENT_SIDE);
      RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, "");
    }

    // cbor_array_get calls cbor_incref() on the returned object, so call
    // cbor_move().
    ScopedCbor relative_time(cbor_array_get(prev_win, kRelativeTimeIndex));
    IsTypeValid(&cbor_is_int, *relative_time, kPrevWinsTimeEntry, kInt,
                error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, "");

    ScopedCbor maybe_ad_render_id(cbor_array_get(prev_win, kAdRenderIdIndex));
    IsTypeValid(&cbor_isa_string, *maybe_ad_render_id, kPrevWinsAdRenderIdEntry,
                kString, error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, "");

    if (error_accumulator.HasErrors()) {
      // No point in processing invalid data but we may continue to validate the
      // input.
      continue;
    }

    const int time = cbor_get_int(*relative_time);
    const std::string ad_render_id = DecodeCborString(*maybe_ad_render_id);

    // Convert to JSON array and add to the running JSON document.
    rapidjson::Value array(rapidjson::kArrayType);
    array.PushBack(time, allocator);
    rapidjson::Value ad_render_id_value(rapidjson::kStringType);
    ad_render_id_value.SetString(ad_render_id.c_str(), ad_render_id.length(),
                                 allocator);
    array.PushBack(ad_render_id_value, allocator);
    document.PushBack(array, allocator);
  }

  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);
  document.Accept(writer);
  return string_buffer.GetString();
}

// Decodes browser signals object and sets it in the 'buyer_interest_group'.
BrowserSignals DecodeBrowserSignals(const cbor_item_t* root,
                                    absl::string_view owner,
                                    ErrorAccumulator& error_accumulator,
                                    bool fail_fast) {
  BrowserSignals signals;
  bool is_signals_valid_type = IsTypeValid(&cbor_isa_map, root, kBrowserSignals,
                                           kMap, error_accumulator);
  RETURN_IF_PREV_ERRORS(error_accumulator, /*fail_fast=*/!is_signals_valid_type,
                        signals);

  absl::Span<cbor_pair> browser_signal_entries(cbor_map_handle(root),
                                               cbor_map_size(root));
  for (const cbor_pair& signal : browser_signal_entries) {
    bool is_valid_key_type =
        IsTypeValid(&cbor_isa_string, signal.key, kBrowserSignalsKey, kString,
                    error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, signals);
    if (!is_valid_key_type) {
      continue;
    }

    const int index =
        FindItemIndex(kBrowserSignalKeys, DecodeCborString(signal.key));
    switch (index) {
      case 0: {  // Bid count.
        bool is_count_valid_type =
            IsTypeValid(&cbor_is_int, signal.value, kBrowserSignalsBidCount,
                        kInt, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, signals);
        if (is_count_valid_type) {
          signals.set_bid_count(cbor_get_int(signal.value));
        }
        break;
      }
      case 1: {  // Join count.
        bool is_count_valid_type =
            IsTypeValid(&cbor_is_int, signal.value, kBrowserSignalsJoinCount,
                        kInt, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, signals);
        if (is_count_valid_type) {
          signals.set_join_count(cbor_get_int(signal.value));
        }
        break;
      }
      case 2: {  // Recency.
        bool is_recency_valid_type =
            IsTypeValid(&cbor_is_int, signal.value, kBrowserSignalsRecency,
                        kInt, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, signals);
        if (is_recency_valid_type) {
          signals.set_recency(cbor_get_int(signal.value));
        }
        break;
      }
      case 3: {  // Previous wins.
        bool is_win_valid_type =
            IsTypeValid(&cbor_isa_array, signal.value, kBrowserSignalsPrevWins,
                        kArray, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, signals);
        if (is_win_valid_type) {
          absl::Span<cbor_item_t*> prev_wins_entries(
              cbor_array_handle(signal.value), cbor_array_size(signal.value));
          absl::StatusOr<std::string> prev_wins = GetStringifiedPrevWins(
              prev_wins_entries, owner, error_accumulator, fail_fast);
          *signals.mutable_prev_wins() = std::move(*prev_wins);
        }
        break;
      }
    }
  }

  return signals;
}

google::protobuf::Map<std::string, BuyerInput> DecodeInterestGroupEntries(
    absl::Span<cbor_pair> interest_group_data_entries,
    ErrorAccumulator& error_accumulator, bool fail_fast) {
  google::protobuf::Map<std::string, BuyerInput> result;
  for (const cbor_pair& interest_group : interest_group_data_entries) {
    bool is_valid_key_type = IsTypeValid(&cbor_isa_string, interest_group.key,
                                         kIgKey, kString, error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, result);

    if (!is_valid_key_type) {
      continue;
    }

    const std::string owner = DecodeCborString(interest_group.key);
    // The value is a gzip compressed bytestring.
    bool is_owner_valid_type =
        IsTypeValid(&cbor_isa_bytestring, interest_group.value, kIgValue,
                    kByteString, error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, result);

    if (!is_owner_valid_type) {
      continue;
    }

    const std::string compressed_igs(
        reinterpret_cast<char*>(cbor_bytestring_handle(interest_group.value)),
        cbor_bytestring_length(interest_group.value));
    const absl::StatusOr<std::string> decompressed_buyer_input =
        GzipDecompress(compressed_igs);
    if (!decompressed_buyer_input.ok()) {
      error_accumulator.ReportError(ErrorVisibility::CLIENT_VISIBLE,
                                    kMalformedCompressedBytestring,
                                    ErrorCode::CLIENT_SIDE);
    }
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, result);

    BuyerInput buyer_input = DecodeBuyerInput(owner, *decompressed_buyer_input,
                                              error_accumulator, fail_fast);

    result.insert({std::move(owner), std::move(buyer_input)});
  }

  return result;
}

absl::Status CborSerializeString(absl::string_view key, absl::string_view value,
                                 ErrorHandler error_handler,
                                 cbor_item_t& root) {
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(cbor_build_stringn(value.data(), value.size()))};
  if (!cbor_map_add(&root, std::move(kv))) {
    error_handler(absl::StrCat("Failed to serialize ", key, " to CBOR"));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeFloat(absl::string_view key, double value,
                                ErrorHandler error_handler, cbor_item_t& root) {
  PS_ASSIGN_OR_RETURN(cbor_item_t * float_val, cbor_build_float(value));
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(float_val)};
  if (!cbor_map_add(&root, std::move(kv))) {
    error_handler(absl::StrCat("Failed to serialize ", key, " to CBOR"));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeBool(absl::string_view key, bool value,
                               ErrorHandler error_handler, cbor_item_t& root) {
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(cbor_build_bool(value))};
  if (!cbor_map_add(&root, std::move(kv))) {
    error_handler(absl::StrCat("Failed to serialize ", key, " to CBOR"));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeAdComponentUrls(
    absl::string_view key, const RepeatedStringProto& component_renders,
    ErrorHandler error_handler, cbor_item_t& root) {
  ScopedCbor serialized_component_renders(
      cbor_new_definite_array(component_renders.size()));
  for (const auto& component_render : component_renders) {
    if (!cbor_array_push(
            *serialized_component_renders,
            cbor_move(cbor_build_stringn(component_render.data(),
                                         component_render.size())))) {
      error_handler(absl::StrCat("Failed to serialize ", key, " to CBOR"));
      return absl::InternalError("");
    }
  }

  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = *serialized_component_renders};
  if (!cbor_map_add(&root, std::move(kv))) {
    error_handler(absl::StrCat("Failed to serialize ", key, " to CBOR"));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeScoreAdResponse(
    const ScoreAdsResponse::AdScore& ad_score,
    const BiddingGroupMap& bidding_group_map, ErrorHandler error_handler,
    cbor_item_t& root) {
  PS_RETURN_IF_ERROR(
      CborSerializeFloat(kBid, ad_score.buyer_bid(), error_handler, root));
  PS_RETURN_IF_ERROR(
      CborSerializeFloat(kScore, ad_score.desirability(), error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeBool(kChaff, false, error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeAdComponentUrls(
      kAdComponents, ad_score.component_renders(), error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeString(kAdRenderUrl, ad_score.render(),
                                         error_handler, root));
  PS_RETURN_IF_ERROR(
      CborSerializeBiddingGroups(bidding_group_map, error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeWinReportingUrls(
      ad_score.win_reporting_urls(), error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeString(
      kInterestGroupName, ad_score.interest_group_name(), error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeString(kInterestGroupOwner,
                                         ad_score.interest_group_owner(),
                                         error_handler, root));
  return absl::OkStatus();
}

absl::StatusOr<std::string> GetCborSerializedAuctionResult(
    ErrorHandler error_handler, cbor_item_t& cbor_data_root) {
  // Serialize the payload to CBOR.
  const size_t cbor_serialized_data_size =
      cbor_serialized_size(&cbor_data_root);
  if (!cbor_serialized_data_size) {
    error_handler(
        "Failed to serialize the AuctionResult to CBOR (data is too large!)");
    return absl::InternalError("");
  }

  std::string byte_string;
  byte_string.resize(cbor_serialized_data_size);
  if (cbor_serialize(&cbor_data_root,
                     reinterpret_cast<unsigned char*>(byte_string.data()),
                     cbor_serialized_data_size) == 0) {
    error_handler("Failed to serialize the AuctionResult to CBOR");
    return absl::InternalError("");
  }
  return byte_string;
}

absl::Status CborSerializeError(const AuctionResult::Error& error,
                                ErrorHandler error_handler, cbor_item_t& root) {
  ScopedCbor serialized_error_map(cbor_new_definite_map(kNumErrorKeys));
  const std::string& message = error.message();
  struct cbor_pair message_kv = {
      .key = cbor_move(cbor_build_stringn(kMessage, sizeof(kMessage) - 1)),
      .value = cbor_move(cbor_build_stringn(message.data(), message.size()))};
  if (!cbor_map_add(*serialized_error_map, std::move(message_kv))) {
    error_handler(
        absl::StrCat("Failed to serialize error ", kMessage, " to CBOR"));
    return absl::InternalError("");
  }

  struct cbor_pair code_kv = {
      .key = cbor_move(cbor_build_stringn(kCode, sizeof(kCode) - 1)),
      .value = cbor_move(cbor_build_uint(error.code()))};
  if (!cbor_map_add(*serialized_error_map, std::move(code_kv))) {
    error_handler(
        absl::StrCat("Failed to serialize error ", kCode, " to CBOR"));
    return absl::InternalError("");
  }

  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(kError, sizeof(kError) - 1)),
      .value = *serialized_error_map};
  if (!cbor_map_add(&root, std::move(kv))) {
    error_handler(absl::StrCat("Failed to serialize ", kError, " to CBOR"));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

}  // namespace

ConsentedDebugConfiguration DecodeConsentedDebugConfig(
    const cbor_item_t* root, ErrorAccumulator& error_accumulator,
    bool fail_fast) {
  ConsentedDebugConfiguration consented_debug_config;
  bool is_config_valid_type = IsTypeValid(
      &cbor_isa_map, root, kConsentedDebugConfig, kMap, error_accumulator);
  RETURN_IF_PREV_ERRORS(error_accumulator, /*fail_fast=*/!is_config_valid_type,
                        consented_debug_config);

  absl::Span<cbor_pair> entries(cbor_map_handle(root), cbor_map_size(root));
  for (const cbor_pair& entry : entries) {
    bool is_valid_key_type =
        IsTypeValid(&cbor_isa_string, entry.key, kConsentedDebugConfigKey,
                    kString, error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, consented_debug_config);
    if (!is_valid_key_type) {
      continue;
    }

    const int index =
        FindItemIndex(kConsentedDebugConfigKeys, DecodeCborString(entry.key));
    switch (index) {
      case 0: {  // IsConsented.
        bool is_valid_type = IsTypeValid(&cbor_is_bool, entry.value,
                                         kConsentedDebugConfigIsConsented,
                                         kString, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast,
                              consented_debug_config);
        if (is_valid_type) {
          consented_debug_config.set_is_consented(cbor_get_bool(entry.value));
        }
        break;
      }
      case 1: {  // Token.
        bool is_valid_type =
            IsTypeValid(&cbor_isa_string, entry.value,
                        kConsentedDebugConfigToken, kString, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast,
                              consented_debug_config);
        if (is_valid_type) {
          consented_debug_config.set_token(DecodeCborString(entry.value));
        }
        break;
      }
    }
  }
  return consented_debug_config;
}

EncodedBuyerInputs DecodeBuyerInputKeys(
    cbor_item_t& compressed_encoded_buyer_inputs,
    ErrorAccumulator& error_accumulator, bool fail_fast) {
  EncodedBuyerInputs encoded_buyer_inputs;
  bool is_buyer_inputs_valid_type =
      IsTypeValid(&cbor_isa_map, &compressed_encoded_buyer_inputs,
                  kInterestGroups, kMap, error_accumulator);
  RETURN_IF_PREV_ERRORS(error_accumulator,
                        /*fail_fast=*/!is_buyer_inputs_valid_type,
                        encoded_buyer_inputs);

  absl::Span<cbor_pair> interest_group_data_entries(
      cbor_map_handle(&compressed_encoded_buyer_inputs),
      cbor_map_size(&compressed_encoded_buyer_inputs));
  for (const cbor_pair& interest_group : interest_group_data_entries) {
    bool is_ig_key_valid_type =
        IsTypeValid(&cbor_isa_string, interest_group.key, kIgKey, kString,
                    error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, encoded_buyer_inputs);

    if (!is_ig_key_valid_type) {
      continue;
    }

    const std::string owner = DecodeCborString(interest_group.key);
    // The value is a gzip compressed bytestring.
    bool is_ig_val_valid_type =
        IsTypeValid(&cbor_isa_bytestring, interest_group.value, kIgValue,
                    kByteString, error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, encoded_buyer_inputs);

    if (!is_ig_val_valid_type) {
      continue;
    }
    const std::string compressed_igs(
        reinterpret_cast<char*>(cbor_bytestring_handle(interest_group.value)),
        cbor_bytestring_length(interest_group.value));
    encoded_buyer_inputs.insert({std::move(owner), std::move(compressed_igs)});
  }

  return encoded_buyer_inputs;
}

bool IsTypeValid(absl::AnyInvocable<bool(const cbor_item_t*)> is_valid_type,
                 const cbor_item_t* item, absl::string_view field_name,
                 absl::string_view expected_type,
                 ErrorAccumulator& error_accumulator, SourceLocation location) {
  if (!is_valid_type(item)) {
    absl::string_view actual_type = kUnknownDataType;
    if (item->type < kCborDataTypesLookup.size()) {
      actual_type = kCborDataTypesLookup[item->type];
    }

    std::string error = absl::StrFormat(kInvalidTypeError, field_name,
                                        expected_type, actual_type);
    VLOG(3) << "CBOR type validation failure at: " << location.file_name()
            << ":" << location.line();
    error_accumulator.ReportError(ErrorVisibility::CLIENT_VISIBLE, error,
                                  ErrorCode::CLIENT_SIDE);
    return false;
  }

  return true;
}

std::string DecodeCborString(const cbor_item_t* item) {
  return std::string(reinterpret_cast<char*>(cbor_string_handle(item)),
                     cbor_string_length(item));
}

cbor_item_t* cbor_build_uint(uint32_t input) {
  if (input <= 255) {
    return cbor_build_uint8(input);
  } else if (input <= 65535) {
    return cbor_build_uint16(input);
  }
  return cbor_build_uint32(input);
}

absl::StatusOr<std::string> SerializeToCbor(cbor_item_t& cbor_data_root) {
  // Serialize the payload to CBOR.
  const size_t cbor_serialized_data_size =
      cbor_serialized_size(&cbor_data_root);
  if (!cbor_serialized_data_size) {
    return absl::InternalError("CBOR data size too large");
  }

  std::string byte_string;
  byte_string.resize(cbor_serialized_data_size);
  if (cbor_serialize(&cbor_data_root,
                     reinterpret_cast<unsigned char*>(byte_string.data()),
                     cbor_serialized_data_size) == 0) {
    return absl::InternalError("Failed to serialize data to CBOR");
  }
  return byte_string;
}

template <typename T>
absl::StatusOr<double> EncodeDecodeFloatCbor(T input, cbor_float_width width) {
  std::string encoded;
  switch (width) {
    case CBOR_FLOAT_16: {
      ScopedCbor candidate(cbor_build_float2(input));
      PS_ASSIGN_OR_RETURN(encoded, SerializeToCbor(**candidate));
    } break;
    case CBOR_FLOAT_32: {
      ScopedCbor candidate(cbor_build_float4(input));
      PS_ASSIGN_OR_RETURN(encoded, SerializeToCbor(**candidate));
    } break;
    default: {
      ScopedCbor candidate(cbor_build_float8(input));
      PS_ASSIGN_OR_RETURN(encoded, SerializeToCbor(**candidate));
    } break;
  }

  cbor_load_result cbor_result;
  ScopedCbor loaded_data(
      cbor_load(reinterpret_cast<unsigned char*>(encoded.data()),
                encoded.size(), &cbor_result));
  if (*loaded_data == nullptr || cbor_result.error.code != CBOR_ERR_NONE) {
    return absl::InternalError("Failed to load CBOR encoded float");
  }

  return cbor_float_get_float(*loaded_data);
}

absl::StatusOr<cbor_item_t*> cbor_build_float(double input) {
  PS_ASSIGN_OR_RETURN(double decoded_double,
                      EncodeDecodeFloatCbor(input, CBOR_FLOAT_64));
  PS_ASSIGN_OR_RETURN(float decoded_single,
                      EncodeDecodeFloatCbor(input, CBOR_FLOAT_32));
  if (AreFloatsEqual(decoded_double, decoded_single)) {
    PS_ASSIGN_OR_RETURN(float decoded_half,
                        EncodeDecodeFloatCbor(input, CBOR_FLOAT_16));
    if (AreFloatsEqual(decoded_single, decoded_half)) {
      VLOG(5) << "Original input: " << input
              << ", encoded as half precision float: " << decoded_half;
      return cbor_build_float2(input);
    }
    VLOG(5) << "Original input: " << input
            << ", encoded as single precision float: " << decoded_single;
    return cbor_build_float4(input);
  }
  VLOG(5) << "Original input: " << input
          << ", encoded as double precision float: " << decoded_double;
  return cbor_build_float8(input);
}

absl::Status CborSerializeBiddingGroups(const BiddingGroupMap& bidding_groups,
                                        ErrorHandler error_handler,
                                        cbor_item_t& root) {
  ScopedCbor serialized_group_map(cbor_new_definite_map(bidding_groups.size()));
  // Order keys by length first and then lexicographically.
  std::set<absl::string_view, decltype(kComparator)> ordered_origins(
      kComparator);
  for (const auto& [origin, unused] : bidding_groups) {
    ordered_origins.insert(origin);
  }
  for (const auto& origin : ordered_origins) {
    const auto& group_indices = bidding_groups.at(origin);
    ScopedCbor serialized_group_indices(
        cbor_new_definite_array(group_indices.index_size()));
    for (int32_t index : group_indices.index()) {
      if (!cbor_array_push(*serialized_group_indices,
                           cbor_move(cbor_build_uint(index)))) {
        error_handler("Failed to serialize a bidding group index to CBOR");
        return absl::InternalError("");
      }
    }
    struct cbor_pair kv = {
        .key = cbor_move(cbor_build_stringn(origin.data(), origin.size())),
        .value = *serialized_group_indices};
    if (!cbor_map_add(*serialized_group_map, std::move(kv))) {
      error_handler(
          "Failed to serialize an <origin, bidding group array> pair to CBOR");
      return absl::InternalError("");
    }
  }
  struct cbor_pair kv = {.key = cbor_move(cbor_build_stringn(
                             kBiddingGroups, sizeof(kBiddingGroups) - 1)),
                         .value = *serialized_group_map};
  if (!cbor_map_add(&root, std::move(kv))) {
    error_handler(
        absl::StrCat("Failed to serialize ", kBiddingGroups, " to CBOR"));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

absl::Status CborSerializeInteractionReportingUrls(
    const InteractionUrlMap& interaction_url_map, ErrorHandler error_handler,
    cbor_item_t& root) {
  ScopedCbor serialized_interaction_url_map(
      cbor_new_definite_map(interaction_url_map.size()));
  std::set<absl::string_view, decltype(kComparator)> ordered_events(
      kComparator);
  for (const auto& [event, unused] : interaction_url_map) {
    ordered_events.insert(event);
  }
  for (const auto& event : ordered_events) {
    PS_RETURN_IF_ERROR(AddKVToMap(event, interaction_url_map.at(event),
                                  error_handler,
                                  **serialized_interaction_url_map));
  }
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(
          kInteractionReportingUrls, sizeof(kInteractionReportingUrls) - 1)),
      .value = *serialized_interaction_url_map};
  if (!cbor_map_add(&root, std::move(kv))) {
    error_handler(absl::StrCat("Failed to serialize ",
                               kInteractionReportingUrls, " to CBOR"));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> Encode(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const BiddingGroupMap& bidding_group_map,
    std::optional<AuctionResult::Error> error, ErrorHandler error_handler) {
  // CBOR data's root handle. When serializing the auction result to CBOR, we
  // use this handle to keep the temporary data.
  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumAuctionResultKeys));
  auto* cbor_internal = cbor_data_root.get();

  if (error.has_value()) {
    PS_RETURN_IF_ERROR(
        CborSerializeError(*error, error_handler, *cbor_internal));
  } else if (high_score.has_value()) {
    PS_RETURN_IF_ERROR(CborSerializeScoreAdResponse(
        *high_score, bidding_group_map, error_handler, *cbor_internal));
  } else {
    PS_RETURN_IF_ERROR(
        CborSerializeBool(kChaff, true, error_handler, *cbor_internal));
  }

  return GetCborSerializedAuctionResult(error_handler, *cbor_internal);
}

DecodedBuyerInputs DecodeBuyerInputs(
    const EncodedBuyerInputs& encoded_buyer_inputs,
    ErrorAccumulator& error_accumulator, bool fail_fast) {
  DecodedBuyerInputs decoded_buyer_inputs;
  for (const auto& [owner, compressed_buyer_input] : encoded_buyer_inputs) {
    BuyerInput buyer_input = DecodeBuyerInput(owner, compressed_buyer_input,
                                              error_accumulator, fail_fast);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, decoded_buyer_inputs);

    decoded_buyer_inputs.insert({owner, std::move(buyer_input)});
  }

  return decoded_buyer_inputs;
}

BuyerInput DecodeBuyerInput(absl::string_view owner,
                            absl::string_view compressed_buyer_input,
                            ErrorAccumulator& error_accumulator,
                            bool fail_fast) {
  BuyerInput buyer_input;
  const absl::StatusOr<std::string> decompressed_buyer_input =
      GzipDecompress(compressed_buyer_input);
  if (!decompressed_buyer_input.ok()) {
    error_accumulator.ReportError(
        ErrorVisibility::CLIENT_VISIBLE,
        absl::StrFormat(kMalformedCompressedIgError, owner),
        ErrorCode::CLIENT_SIDE);
    return buyer_input;
  }

  cbor_load_result result;
  ScopedCbor root(cbor_load(
      reinterpret_cast<const unsigned char*>(decompressed_buyer_input->data()),
      decompressed_buyer_input->size(), &result));

  if (result.error.code != CBOR_ERR_NONE) {
    error_accumulator.ReportError(
        ErrorVisibility::CLIENT_VISIBLE,
        absl::StrFormat(kInvalidBuyerInputCborError, owner),
        ErrorCode::CLIENT_SIDE);
    return buyer_input;
  }

  bool is_buyer_input_valid_type = IsTypeValid(
      &cbor_isa_array, *root, kBuyerInput, kArray, error_accumulator);
  RETURN_IF_PREV_ERRORS(error_accumulator,
                        /*fail_fast=*/!is_buyer_input_valid_type, buyer_input);

  absl::Span<cbor_item_t*> interest_groups(cbor_array_handle(*root),
                                           cbor_array_size(*root));
  for (const cbor_item_t* interest_group : interest_groups) {
    auto* buyer_interest_group = buyer_input.add_interest_groups();

    bool is_igs_valid_type =
        IsTypeValid(&cbor_isa_map, interest_group, kBuyerInputEntry, kMap,
                    error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);

    if (!is_igs_valid_type) {
      continue;
    }

    absl::Span<cbor_pair> ig_entries(cbor_map_handle(interest_group),
                                     cbor_map_size(interest_group));
    for (const cbor_pair& ig_entry : ig_entries) {
      bool is_key_valid_type =
          IsTypeValid(&cbor_isa_string, ig_entry.key, kBuyerInputKey, kString,
                      error_accumulator);
      RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);

      if (!is_key_valid_type) {
        continue;
      }

      const int index =
          FindItemIndex(kInterestGroupKeys, DecodeCborString(ig_entry.key));
      switch (index) {
        case 0: {  // Name.
          bool is_name_valid_type =
              IsTypeValid(&cbor_isa_string, ig_entry.value, kIgName, kString,
                          error_accumulator);
          RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);
          if (is_name_valid_type) {
            buyer_interest_group->set_name(DecodeCborString(ig_entry.value));
          }
          break;
        }
        case 1: {  // Bidding signal keys.
          bool is_bs_valid_type =
              IsTypeValid(&cbor_isa_array, ig_entry.value, kIgBiddingSignalKeys,
                          kArray, error_accumulator);
          RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);

          if (is_bs_valid_type) {
            absl::Span<cbor_item_t*> bidding_signals_list(
                cbor_array_handle(ig_entry.value),
                cbor_array_size(ig_entry.value));
            *buyer_interest_group->mutable_bidding_signals_keys() =
                DecodeStringArray(bidding_signals_list,
                                  kIgBiddingSignalKeysEntry, error_accumulator,
                                  fail_fast);
          }
          break;
        }
        case 2: {  // User bidding signals.
          bool is_bs_valid_type =
              IsTypeValid(&cbor_isa_string, ig_entry.value, kUserBiddingSignals,
                          kString, error_accumulator);
          RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);

          if (is_bs_valid_type) {
            *buyer_interest_group->mutable_user_bidding_signals() =
                DecodeCborString(ig_entry.value);
          }
          break;
        }
        case 3: {  // Ad render IDs.
          bool is_ad_render_valid_type = IsTypeValid(
              &cbor_isa_array, ig_entry.value, kAds, kArray, error_accumulator);
          RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);

          if (is_ad_render_valid_type) {
            absl::Span<cbor_item_t*> ads(cbor_array_handle(ig_entry.value),
                                         cbor_array_size(ig_entry.value));
            *buyer_interest_group->mutable_ad_render_ids() = DecodeStringArray(
                ads, kAdRenderId, error_accumulator, fail_fast);
          }
          break;
        }
        case 4: {  // Component ads.
          bool is_component_valid_type =
              IsTypeValid(&cbor_isa_array, ig_entry.value, kAdComponent, kArray,
                          error_accumulator);
          RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);

          if (is_component_valid_type) {
            absl::Span<cbor_item_t*> component_ads(
                cbor_array_handle(ig_entry.value),
                cbor_array_size(ig_entry.value));
            *buyer_interest_group->mutable_component_ads() = DecodeStringArray(
                component_ads, kAdComponentEntry, error_accumulator, fail_fast);
          }
          break;
        }
        case 5: {  // Browser signals.
          *buyer_interest_group->mutable_browser_signals() =
              DecodeBrowserSignals(ig_entry.value, kIgBiddingSignalKeysEntry,
                                   error_accumulator, fail_fast);
          RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);
        }
      }
    }
  }

  return buyer_input;
}

absl::Status CborSerializeReportingUrls(
    absl::string_view key,
    const WinReportingUrls::ReportingUrls& reporting_urls,
    ErrorHandler error_handler, cbor_item_t& root) {
  int key_count = 0;
  if (!reporting_urls.reporting_url().empty()) {
    key_count++;
  }
  if (!reporting_urls.interaction_reporting_urls().empty()) {
    key_count++;
  }
  if (key_count == 0) {
    return absl::OkStatus();
  }
  ScopedCbor serialized_reporting_urls(
      cbor_new_definite_map(kNumReportingUrlsKeys));
  if (!reporting_urls.reporting_url().empty()) {
    PS_RETURN_IF_ERROR(AddKVToMap(kReportingUrl, reporting_urls.reporting_url(),
                                  error_handler, **serialized_reporting_urls));
  }
  if (!reporting_urls.interaction_reporting_urls().empty()) {
    PS_RETURN_IF_ERROR(CborSerializeInteractionReportingUrls(
        reporting_urls.interaction_reporting_urls(), error_handler,
        **serialized_reporting_urls));
  }
  struct cbor_pair serialized_reporting_urls_kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = *serialized_reporting_urls,
  };

  if (!cbor_map_add(&root, std::move(serialized_reporting_urls_kv))) {
    error_handler(absl::StrCat("Failed to serialize ", key, " to CBOR"));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeWinReportingUrls(
    const WinReportingUrls& win_reporting_urls, ErrorHandler error_handler,
    cbor_item_t& root) {
  if (!win_reporting_urls.has_buyer_reporting_urls() &&
      !win_reporting_urls.has_top_level_seller_reporting_urls()) {
    return absl::OkStatus();
  }
  ScopedCbor serialized_win_reporting_urls(
      cbor_new_definite_map(kNumReportingUrlsKeys));
  if (win_reporting_urls.has_buyer_reporting_urls()) {
    PS_RETURN_IF_ERROR(CborSerializeReportingUrls(
        kBuyerReportingUrls, win_reporting_urls.buyer_reporting_urls(),
        error_handler, **serialized_win_reporting_urls));
  }
  if (win_reporting_urls.has_top_level_seller_reporting_urls()) {
    PS_RETURN_IF_ERROR(CborSerializeReportingUrls(
        kTopLevelSellerReportingUrls,
        win_reporting_urls.top_level_seller_reporting_urls(), error_handler,
        **serialized_win_reporting_urls));
  }
  struct cbor_pair serialized_win_reporting_urls_kv = {
      .key = cbor_move(
          cbor_build_stringn(kWinReportingUrls, sizeof(kWinReportingUrls) - 1)),
      .value = *serialized_win_reporting_urls,
  };
  if (!cbor_map_add(&root, std::move(serialized_win_reporting_urls_kv))) {
    error_handler(
        absl::StrCat("Failed to serialize ", kWinReportingUrls, " to CBOR"));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
