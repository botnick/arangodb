////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2018 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Tobias Gödderz
/// @author Jan Christoph Uhde
////////////////////////////////////////////////////////////////////////////////

// Contains free standing functions that are mean for use in
// RestBatchDocumentHander.cpp only. You MUST NOT include
// this file in other places.

#ifndef ARANGOD_REST_HANDLER_REST_BATCH_DOCUMENT_HANDLER_HELPER_H
#define ARANGOD_REST_HANDLER_REST_BATCH_DOCUMENT_HANDLER_HELPER_H 1

#endif

#include "RestBatchDocumentHandler.h"

#include <velocypack/Iterator.h>
#include <velocypack/velocypack-aliases.h>
#include <boost/range/join.hpp>


namespace arangodb {
namespace rest {
namespace batch_document_handler {

// Operations /////////////////////////////////////////////////////////////////
enum class BatchOperation {
  READ,
  INSERT,
  REMOVE,
  REPLACE,
  UPDATE,
  UPSERT,
  REPSERT,
};

struct BatchOperationHash {
  size_t operator()(BatchOperation value) const noexcept {
    typedef std::underlying_type<decltype(value)>::type UnderlyingType;
    return std::hash<UnderlyingType>()(UnderlyingType(value));
  }
};

std::unordered_map<BatchOperation, std::string, BatchOperationHash>
    // NOLINTNEXTLINE(cert-err58-cpp)
    batchToStringMap{
        {BatchOperation::READ, "read"},
        {BatchOperation::INSERT, "insert"},
        {BatchOperation::REMOVE, "remove"},
        {BatchOperation::REPLACE, "replace"},
        {BatchOperation::UPDATE, "update"},
        {BatchOperation::UPSERT, "upsert"},
        {BatchOperation::REPSERT, "repsert"},
    };

std::unordered_map<std::string, BatchOperation> stringToBatchMap;

static std::string batchToString(BatchOperation op) {
  return batchToStringMap.at(op);
}

static void ensureStringToBatchMapIsInitialized() {
  if (!stringToBatchMap.empty()) {
    return;
  }

  for (auto const& it : batchToStringMap) {
    stringToBatchMap.insert({it.second, it.first});
  }
}

static boost::optional<BatchOperation> stringToBatch(std::string const& op) {
  ensureStringToBatchMapIsInitialized();

  auto it = stringToBatchMap.find(op);

  if (it == stringToBatchMap.end()) {
    return boost::none;
  }

  return it->second;
}

////////////////////////////////////////////////////////////////////////////////
// Request structs and parsers
////////////////////////////////////////////////////////////////////////////////

using AttributeSet = std::unordered_set<std::string>;

static Result expectedType(VPackValueType expected, VPackValueType got){
  if(expected == got) { return {}; };
  std::stringstream err;
  err << "Expected type " << valueTypeName(expected) << ", got " << valueTypeName(got) << " instead.";
  return Result{TRI_ERROR_ARANGO_VALIDATION_FAILED, err.str()};
}

static Result unexpectedAttributeError(AttributeSet const& required,
                                       AttributeSet const& optional,
                                       AttributeSet const& deprecated,
                                       std::string const& got) {
  std::stringstream err;
  err << "Encountered unexpected attribute `" << got
      << "`, allowed attributes are {";

  auto attributes = boost::join(boost::join(required, optional), deprecated);
  bool first = true;
  for (auto const& it : attributes) {
    if (!first) {
      err << ", ";
    }
    first = false;

    err << it;
  }
  err << "}.";

  return Result{TRI_ERROR_ARANGO_VALIDATION_FAILED, err.str()};
}

static ResultT<AttributeSet> isObjectAndDoesNotHaveExtraAttributes(
    VPackSlice slice,
    AttributeSet const& required,
    AttributeSet const& optional,
    AttributeSet const& deprecated
    ) {

  AttributeSet rv;

  auto result = expectedType(VPackValueType::Object, slice.type());
  if(result.fail()){
    return result;
  }

  auto contains = [](AttributeSet const& set,
                     std::string const& needle) -> bool {
    return set.find(needle) != set.end();
  };

  for (auto const& it : VPackObjectIterator(slice)) {
    std::string const key = it.key.copyString();

    if (contains(required, key) || contains(optional, key)) {
      rv.insert(key);
      // ok, continue
    } else if (contains(deprecated, key)) {
      // ok but warn
      // TODO some more context information would be nice, but this would have
      // have to be passed down somehow.
      LOG_TOPIC(WARN, Logger::FIXME)
          << "Deprecated attribute `" << key
          << "` encountered during request to "
          << RestVocbaseBaseHandler::BATCH_DOCUMENT_PATH;
      rv.insert(key);
    } else {
      return unexpectedAttributeError(required, optional, deprecated, key);
    }
  }

  return ResultT<AttributeSet>::success(rv);
}

struct PatternWithKey {
  std::string const key;
  VPackSlice const pattern;
  static ResultT<PatternWithKey> fromVelocypack(VPackSlice slice);

 protected:
  // NOLINTNEXTLINE(modernize-pass-by-value)
  PatternWithKey(std::string const& key_, VPackSlice const pattern_)
      : key(key_), pattern(pattern_) {}

  PatternWithKey(std::string&& key_, VPackSlice const pattern_)
      : key(std::move(key_)), pattern(pattern_) {}
};

ResultT<PatternWithKey> PatternWithKey::fromVelocypack(VPackSlice const slice) {
  auto result = expectedType(VPackValueType::Object, slice.type());
  if(result.fail()){
    return result;
  }

  VPackSlice key = slice.get(StaticStrings::KeyString);

  if (key.isNone()) {
    std::stringstream err;
    err << "Attribute '_key' missing";
    return ResultT<PatternWithKey>::error(TRI_ERROR_ARANGO_VALIDATION_FAILED,
                                          err.str());
  }

  result = expectedType(VPackValueType::String, key.type());
  if (result.fail()) {
    return prefixResultMessage(result, "When parsing attribute '_key'");
  }

  return ResultT<PatternWithKey>::success(
      PatternWithKey(key.copyString(), slice));
}

}
}
}
