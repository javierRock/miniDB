#include "minidb/common/value.hpp"

namespace minidb {

std::string ValueToString(const Value& value) {
    if (std::holds_alternative<std::int32_t>(value)) {
        return std::to_string(std::get<std::int32_t>(value));
    }
    return std::get<std::string>(value);
}

bool CompareValues(const Value& left, CompareOperator op, const Value& right) {
    if (left.index() != right.index()) {
        throw QueryError("No se pueden comparar valores de tipos distintos");
    }

    // Both alternatives are totally ordered, so one three-way comparison drives
    // every operator.
    const auto ordering = std::holds_alternative<std::int32_t>(left)
                              ? (std::get<std::int32_t>(left) <=> std::get<std::int32_t>(right))
                              : (std::get<std::string>(left) <=> std::get<std::string>(right));

    switch (op) {
        case CompareOperator::kEqual:
            return ordering == 0;
        case CompareOperator::kNotEqual:
            return ordering != 0;
        case CompareOperator::kLess:
            return ordering < 0;
        case CompareOperator::kLessEqual:
            return ordering <= 0;
        case CompareOperator::kGreater:
            return ordering > 0;
        case CompareOperator::kGreaterEqual:
            return ordering >= 0;
    }
    throw StorageError("Operador de comparación desconocido");
}

}  // namespace minidb
