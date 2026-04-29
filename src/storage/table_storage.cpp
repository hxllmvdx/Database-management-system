#include "storage/table_storage.h"
#include "catalog/schema.h"
#include "common/status.h"
#include "execution/value.h"
#include "storage/row_id.h"
#include <cstdint>
#include <sstream>

namespace db {

TableStorage::TableStorage(TableDescriptor descriptor, std::size_t page_size)
    : descriptor_(descriptor), row_store_(descriptor.data_file, page_size), is_open_(false)
{}

Status TableStorage::Open() {
    if (is_open_) {
        return Status::Ok();
    }
    if (descriptor_.data_file.empty()) {
        return Status::Error(StatusCode::kInvalidArgument, "Table data file path is empty");
    }

    Status status = row_store_.Open();
    if (!status.ok()) {
        return status;
    }

    is_open_ = true;
    return Status::Ok();
}

Status TableStorage::PrepareTupleForWrite(const Tuple& input, Tuple* output) const {
    if (output == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "output must not be nullptr");
    }

    const TableSchema& schema = descriptor_.schema;
    if (schema.empty()) {
        return Status::Error(StatusCode::kInternalError, "Table schema in descriptor is empty");
    }

    if (input.values.size() > schema.column_count()) {
        return Status::Error(StatusCode::kInvalidArgument, "Got tuple with too many columns");
    }

    output->values.clear();
    output->values.reserve(schema.column_count());

    for (std::size_t i = 0; i < schema.column_count(); i++) {
        if (i < input.values.size()) {
            const Value& candidate = input.values[i];

            Status status = ValidateValueAgainstColumn(candidate, schema.columns[i]);
            if (!status.ok()) {
                return status;
            }

            output->values.push_back(candidate);
        } else {
            const ColumnSchema& column = schema.columns[i];
            const auto& opt_candidate = column.default_value;
            if (!opt_candidate.has_value()) {
                if (column.not_null) {
                    std::ostringstream oss;
                    oss << "Missing value for required column '" << column.name << "'";
                    return Status::Error(StatusCode::kInvalidArgument, oss.str());
                }

                output->values.push_back(Value::Null());
                continue;
            }

            const Value& candidate = opt_candidate.value();
            Status status = ValidateValueAgainstColumn(candidate, column);
            if (!status.ok()) {
                return status;
            }

            output->values.push_back(candidate);
        }
    }

    return Status::Ok();
}

Status TableStorage::ValidateValueAgainstColumn(const Value& value, const ColumnSchema& column) const {
    if (value.is_null()) {
        if (column.not_null) {
            std::ostringstream oss;
            oss << "Column '" << column.name << "' does not allow NULL values";
            return Status::Error(StatusCode::kInvalidArgument, oss.str());
        }
        return Status::Ok();
    }

    switch (column.type) {
    case db::ColumnType::kInt: {
        if (value.type() != ValueType::kInt) {
            std::ostringstream oss;
            oss << "Column '" << column.name << "' expects INT value";
            return Status::Error(StatusCode::kInvalidArgument, oss.str());
        }
        break;
    }
    case db::ColumnType::kBool: {
        if (value.type() != ValueType::kBool) {
            std::ostringstream oss;
            oss << "Column '" << column.name << "' expects BOOL value";
            return Status::Error(StatusCode::kInvalidArgument, oss.str());
        }
        break;
    }
    case db::ColumnType::kString: {
        if (value.type() != ValueType::kString) {
            std::ostringstream oss;
            oss << "Column '" << column.name << "' expects STRING value";
            return Status::Error(StatusCode::kInvalidArgument, oss.str());
        }
        break;
    }
    }

    return Status::Ok();
}

Status TableStorage::Insert(const Tuple& tuple, RowId* out_rid) {
    if (out_rid == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "TableStorage is not opened");
    }

    Tuple prepared;
    Status status = PrepareTupleForWrite(tuple, &prepared);
    if (!status.ok()) {
        return status;
    }

    Row row;
    row.tuple = prepared;
    row.deleted = false;

    return row_store_.Insert(row, out_rid);
}

Status TableStorage::Update(RowId rid, const Tuple& tuple) {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "TableStorage is not opened");
    }

    Tuple prepared;
    Status status = PrepareTupleForWrite(tuple, &prepared);
    if (!status.ok()) {
        return status;
    }

    return row_store_.Update(rid, prepared);
}

Status TableStorage::Get(RowId rid, Row* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "TableStorage is not opened");
    }

    return row_store_.Get(rid, out);
}

Status TableStorage::Scan(std::vector<Row>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "TableStorage is not opened");
    }

    return row_store_.Scan(out);
}

Status TableStorage::Delete(RowId rid) {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "TableStorage is not opened");
    }

    return row_store_.Delete(rid);
}

}
