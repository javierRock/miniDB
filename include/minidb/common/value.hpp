#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "minidb/common/types.hpp"

namespace minidb {

/// A single column value. INT and VARCHAR are the only supported types.
///
/// This lives in common/ rather than next to Record because the parser needs it
/// to represent literals, and the parsing layer must not depend on storage.
using Value = std::variant<std::int32_t, std::string>;

[[nodiscard]] std::string ValueToString(const Value& value);

/// Compares two values of the same type. Strings compare byte by byte, so the
/// ordering is UTF-8 code-unit order rather than a linguistic collation.
/// Throws QueryError when the types differ.
[[nodiscard]] bool CompareValues(const Value& left, CompareOperator op, const Value& right);

}  // namespace minidb
