#include "minidb/storage/record.hpp"

#include <utility>

#include "minidb/common/serialization.hpp"

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

const Value& Record::GetValue(std::size_t index) const {
    if (index >= values_.size()) {
        throw StorageError("Índice de valor fuera de rango: " + std::to_string(index));
    }
    return values_[index];
}

void Record::SetValue(std::size_t index, Value value) {
    if (index >= values_.size()) {
        throw StorageError("Índice de valor fuera de rango: " + std::to_string(index));
    }
    values_[index] = std::move(value);
}

void Record::Validate(const Schema& schema) const {
    if (values_.size() != schema.ColumnCount()) {
        throw QueryError("El registro tiene " + std::to_string(values_.size()) +
                         " valores pero la tabla tiene " + std::to_string(schema.ColumnCount()) +
                         " columnas");
    }

    for (std::size_t i = 0; i < values_.size(); ++i) {
        const Column& column = schema.GetColumn(i);

        if (column.type == ColumnType::kInteger) {
            if (!std::holds_alternative<std::int32_t>(values_[i])) {
                throw QueryError("La columna '" + column.name + "' es INT y recibió una cadena");
            }
        } else {
            if (!std::holds_alternative<std::string>(values_[i])) {
                throw QueryError("La columna '" + column.name +
                                 "' es VARCHAR y recibió un entero");
            }
            // The limit is in UTF-8 bytes, not characters: an accented Spanish
            // string occupies more bytes than it has characters.
            const std::string& text = std::get<std::string>(values_[i]);
            if (text.size() > column.max_length) {
                throw QueryError("El valor de la columna '" + column.name + "' ocupa " +
                                 std::to_string(text.size()) + " bytes y el máximo es " +
                                 std::to_string(column.max_length));
            }
        }
    }
}

std::size_t Record::SerializedSize(const Schema& schema) const {
    std::size_t total = 0;
    for (std::size_t i = 0; i < values_.size(); ++i) {
        total += (schema.GetColumn(i).type == ColumnType::kInteger)
                     ? sizeof(std::int32_t)
                     : serialization::StringSize(std::get<std::string>(values_[i]));
    }
    return total;
}

void Record::SerializeTo(const Schema& schema, std::span<std::byte> destination) const {
    std::size_t offset = 0;
    for (std::size_t i = 0; i < values_.size(); ++i) {
        if (schema.GetColumn(i).type == ColumnType::kInteger) {
            serialization::WriteI32(destination, offset, std::get<std::int32_t>(values_[i]));
            offset += sizeof(std::int32_t);
        } else {
            offset += serialization::WriteString(destination, offset,
                                                 std::get<std::string>(values_[i]));
        }
    }
}

Record Record::DeserializeFrom(const Schema& schema, std::span<const std::byte> source) {
    std::vector<Value> values;
    values.reserve(schema.ColumnCount());

    std::size_t offset = 0;
    for (const Column& column : schema.Columns()) {
        if (column.type == ColumnType::kInteger) {
            values.emplace_back(serialization::ReadI32(source, offset));
            offset += sizeof(std::int32_t);
        } else {
            std::size_t consumed = 0;
            values.emplace_back(serialization::ReadString(source, offset, &consumed));
            offset += consumed;
        }
    }
    return Record(std::move(values));
}

}  // namespace minidb
