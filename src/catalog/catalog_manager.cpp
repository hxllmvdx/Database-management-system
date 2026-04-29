#include "catalog/catalog_manager.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/bytes.h"
#include "common/file_utils.h"

namespace {

namespace fs = std::filesystem;

using db::Byte;
using db::ByteBuffer;

std::string DatabasePath(const std::string& root_dir, const std::string& db_name) {
    return root_dir + "/" + db_name;
}

std::string CatalogDirPath(const std::string& root_dir, const std::string& db_name) {
    return DatabasePath(root_dir, db_name) + "/catalog";
}

std::string TablesDirPath(const std::string& root_dir, const std::string& db_name) {
    return DatabasePath(root_dir, db_name) + "/tables";
}

std::string IndexesDirPath(const std::string& root_dir, const std::string& db_name) {
    return DatabasePath(root_dir, db_name) + "/indexes";
}

std::string VersionsDirPath(const std::string& root_dir, const std::string& db_name) {
    return DatabasePath(root_dir, db_name) + "/versions";
}

std::string TableMetaPath(const std::string& root_dir,
                          const std::string& db_name,
                          const std::string& table_name) {
    return CatalogDirPath(root_dir, db_name) + "/" + table_name + ".meta";
}

std::string DefaultTableDataPath(const std::string& root_dir,
                                 const std::string& db_name,
                                 const std::string& table_name) {
    return TablesDirPath(root_dir, db_name) + "/" + table_name + ".tbl";
}

std::string DefaultTableIndexPath(const std::string& root_dir,
                                  const std::string& db_name,
                                  const std::string& table_name) {
    return IndexesDirPath(root_dir, db_name) + "/" + table_name + ".idx";
}

bool IsValidName(std::string_view name) {
    if (name.empty()) {
        return false;
    }

    bool has_non_space = false;
    bool prev_dot = false;

    for (char ch_raw : name) {
        const unsigned char ch = static_cast<unsigned char>(ch_raw);
        if (ch == '/' || ch == '\\') {
            return false;
        }
        if (ch == '.' && prev_dot) {
            return false;
        }
        prev_dot = (ch == '.');
        if (!std::isspace(ch)) {
            has_non_space = true;
        }
    }

    return has_non_space;
}

db::Status ValidateDatabaseName(std::string_view db_name) {
    if (!IsValidName(db_name)) {
        return db::Status::Error(db::StatusCode::kInvalidArgument, "Invalid database name");
    }
    return db::Status::Ok();
}

db::Status ValidateTableName(std::string_view table_name) {
    if (!IsValidName(table_name)) {
        return db::Status::Error(db::StatusCode::kInvalidArgument, "Invalid table name");
    }
    return db::Status::Ok();
}

db::Status ValidateColumnSchema(const db::ColumnSchema& column) {
    if (!IsValidName(column.name)) {
        return db::Status::Error(db::StatusCode::kInvalidArgument, "Invalid column name");
    }

    if (!column.default_value.has_value()) {
        return db::Status::Ok();
    }

    const db::ValueType default_type = column.default_value->type();
    if (column.type == db::ColumnType::kInt && default_type != db::ValueType::kInt) {
        return db::Status::Error(db::StatusCode::kInvalidArgument,
                                 "Invalid default value for int column");
    }
    if (column.type == db::ColumnType::kString && default_type != db::ValueType::kString) {
        return db::Status::Error(db::StatusCode::kInvalidArgument,
                                 "Invalid default value for string column");
    }

    return db::Status::Ok();
}

db::Status ValidateTableSchema(const db::TableSchema& schema) {
    if (schema.columns.empty()) {
        return db::Status::Error(db::StatusCode::kInvalidArgument,
                                 "Table schema must have at least one column");
    }

    std::unordered_set<std::string> seen_columns;
    for (const db::ColumnSchema& column : schema.columns) {
        db::Status status = ValidateColumnSchema(column);
        if (!status.ok()) {
            return status;
        }

        const auto [_, inserted] = seen_columns.insert(column.name);
        if (!inserted) {
            return db::Status::Error(db::StatusCode::kInvalidArgument, "Duplicate column name");
        }
    }

    return db::Status::Ok();
}

db::Status ValidateIndexDescriptor(const db::IndexDescriptor& index,
                                   const db::TableSchema& schema,
                                   const std::string& table_name) {
    if (!IsValidName(index.name)) {
        return db::Status::Error(db::StatusCode::kInvalidArgument, "Invalid index name");
    }
    if (index.table_name != table_name) {
        return db::Status::Error(db::StatusCode::kInvalidArgument,
                                 "Index table name does not match table descriptor");
    }
    if (schema.FindColumn(index.column_name) < 0) {
        return db::Status::Error(db::StatusCode::kInvalidArgument,
                                 "Index column does not exist in table schema");
    }

    return db::Status::Ok();
}

db::Status ValidateTableDescriptor(const db::TableDescriptor& desc) {
    db::Status status = ValidateDatabaseName(desc.database_name);
    if (!status.ok()) {
        return status;
    }

    status = ValidateTableName(desc.table_name);
    if (!status.ok()) {
        return status;
    }

    status = ValidateTableSchema(desc.schema);
    if (!status.ok()) {
        return status;
    }

    std::unordered_set<std::string> seen_indexes;
    for (const db::IndexDescriptor& index : desc.indexes) {
        status = ValidateIndexDescriptor(index, desc.schema, desc.table_name);
        if (!status.ok()) {
            return status;
        }

        const auto [_, inserted] = seen_indexes.insert(index.name);
        if (!inserted) {
            return db::Status::Error(db::StatusCode::kInvalidArgument, "Duplicate index name");
        }
    }

    if (desc.data_file.empty()) {
        return db::Status::Error(db::StatusCode::kInvalidArgument, "Table data file path is empty");
    }
    if (desc.index_file.empty()) {
        return db::Status::Error(db::StatusCode::kInvalidArgument, "Table index file path is empty");
    }

    return db::Status::Ok();
}

bool DatabaseExists(const std::string& root_dir, const std::string& db_name) {
    std::error_code ec;
    return fs::exists(DatabasePath(root_dir, db_name), ec) &&
           fs::is_directory(DatabasePath(root_dir, db_name), ec);
}

bool TableMetaExists(const std::string& root_dir,
                     const std::string& db_name,
                     const std::string& table_name) {
    bool exists = false;
    const db::Status status = db::file_utils::FileExists(
        TableMetaPath(root_dir, db_name, table_name), &exists);
    return status.ok() && exists;
}

db::TableDescriptor NormalizeTableDescriptor(const std::string& root_dir,
                                             db::TableDescriptor desc) {
    if (desc.data_file.empty()) {
        desc.data_file = DefaultTableDataPath(root_dir, desc.database_name, desc.table_name);
    }
    if (desc.index_file.empty()) {
        desc.index_file = DefaultTableIndexPath(root_dir, desc.database_name, desc.table_name);
    }

    for (db::IndexDescriptor& index : desc.indexes) {
        if (index.table_name.empty()) {
            index.table_name = desc.table_name;
        }
    }

    return desc;
}

void WriteBytes(ByteBuffer* buffer, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const Byte*>(data);
    buffer->insert(buffer->end(), bytes, bytes + size);
}

void WriteUint8(ByteBuffer* buffer, std::uint8_t value) {
    buffer->push_back(static_cast<Byte>(value));
}

void WriteUint32(ByteBuffer* buffer, std::uint32_t value) {
    WriteBytes(buffer, &value, sizeof(value));
}

void WriteInt64(ByteBuffer* buffer, std::int64_t value) {
    WriteBytes(buffer, &value, sizeof(value));
}

void WriteBool(ByteBuffer* buffer, bool value) {
    WriteUint8(buffer, value ? 1U : 0U);
}

void WriteString(ByteBuffer* buffer, const std::string& value) {
    WriteUint32(buffer, static_cast<std::uint32_t>(value.size()));
    WriteBytes(buffer, value.data(), value.size());
}

db::Status EnsureReadable(const ByteBuffer& bytes, std::size_t offset, std::size_t need) {
    if (offset > bytes.size() || bytes.size() - offset < need) {
        return db::Status::Error(db::StatusCode::kInvalidArgument,
                                 "Corrupted metadata: unexpected end of file");
    }
    return db::Status::Ok();
}

template <typename T>
db::Status ReadPod(const ByteBuffer& bytes, std::size_t* offset, T* out) {
    const db::Status status = EnsureReadable(bytes, *offset, sizeof(T));
    if (!status.ok()) {
        return status;
    }

    std::memcpy(out, bytes.data() + *offset, sizeof(T));
    *offset += sizeof(T);
    return db::Status::Ok();
}

db::Status ReadUint8(const ByteBuffer& bytes, std::size_t* offset, std::uint8_t* out) {
    return ReadPod(bytes, offset, out);
}

db::Status ReadUint32(const ByteBuffer& bytes, std::size_t* offset, std::uint32_t* out) {
    return ReadPod(bytes, offset, out);
}

db::Status ReadInt64(const ByteBuffer& bytes, std::size_t* offset, std::int64_t* out) {
    return ReadPod(bytes, offset, out);
}

db::Status ReadBool(const ByteBuffer& bytes, std::size_t* offset, bool* out) {
    std::uint8_t raw = 0;
    db::Status status = ReadUint8(bytes, offset, &raw);
    if (!status.ok()) {
        return status;
    }

    if (raw > 1U) {
        return db::Status::Error(db::StatusCode::kInvalidArgument,
                                 "Corrupted metadata: invalid bool value");
    }

    *out = (raw == 1U);
    return db::Status::Ok();
}

db::Status ReadString(const ByteBuffer& bytes, std::size_t* offset, std::string* out) {
    std::uint32_t size = 0;
    db::Status status = ReadUint32(bytes, offset, &size);
    if (!status.ok()) {
        return status;
    }

    status = EnsureReadable(bytes, *offset, size);
    if (!status.ok()) {
        return status;
    }

    out->assign(reinterpret_cast<const char*>(bytes.data() + *offset), size);
    *offset += size;
    return db::Status::Ok();
}

void SerializeValue(const db::Value& value, ByteBuffer* buffer) {
    WriteUint32(buffer, static_cast<std::uint32_t>(value.type()));
    switch (value.type()) {
        case db::ValueType::kNull:
            break;
        case db::ValueType::kInt:
            WriteInt64(buffer, value.AsInt());
            break;
        case db::ValueType::kString:
            WriteString(buffer, value.AsString());
            break;
        case db::ValueType::kBool:
            WriteBool(buffer, value.AsBool());
            break;
    }
}

db::Status DeserializeValue(const ByteBuffer& bytes, std::size_t* offset, db::Value* out) {
    std::uint32_t raw_type = 0;
    db::Status status = ReadUint32(bytes, offset, &raw_type);
    if (!status.ok()) {
        return status;
    }

    const auto type = static_cast<db::ValueType>(raw_type);
    switch (type) {
        case db::ValueType::kNull:
            *out = db::Value::Null();
            return db::Status::Ok();
        case db::ValueType::kInt: {
            std::int64_t value = 0;
            status = ReadInt64(bytes, offset, &value);
            if (!status.ok()) {
                return status;
            }
            *out = db::Value::Int(value);
            return db::Status::Ok();
        }
        case db::ValueType::kString: {
            std::string value;
            status = ReadString(bytes, offset, &value);
            if (!status.ok()) {
                return status;
            }
            *out = db::Value::String(std::move(value));
            return db::Status::Ok();
        }
        case db::ValueType::kBool: {
            bool value = false;
            status = ReadBool(bytes, offset, &value);
            if (!status.ok()) {
                return status;
            }
            *out = db::Value::Bool(value);
            return db::Status::Ok();
        }
    }

    return db::Status::Error(db::StatusCode::kInvalidArgument,
                             "Corrupted metadata: invalid value type");
}

void SerializeColumnSchema(const db::ColumnSchema& column, ByteBuffer* buffer) {
    WriteString(buffer, column.name);
    WriteUint32(buffer, static_cast<std::uint32_t>(column.type));
    WriteBool(buffer, column.not_null);
    WriteBool(buffer, column.indexed);
    WriteBool(buffer, column.default_value.has_value());
    if (column.default_value.has_value()) {
        SerializeValue(*column.default_value, buffer);
    }
}

db::Status DeserializeColumnSchema(const ByteBuffer& bytes,
                                   std::size_t* offset,
                                   db::ColumnSchema* out) {
    db::Status status = ReadString(bytes, offset, &out->name);
    if (!status.ok()) {
        return status;
    }

    std::uint32_t raw_type = 0;
    status = ReadUint32(bytes, offset, &raw_type);
    if (!status.ok()) {
        return status;
    }
    out->type = static_cast<db::ColumnType>(raw_type);

    status = ReadBool(bytes, offset, &out->not_null);
    if (!status.ok()) {
        return status;
    }

    status = ReadBool(bytes, offset, &out->indexed);
    if (!status.ok()) {
        return status;
    }

    bool has_default = false;
    status = ReadBool(bytes, offset, &has_default);
    if (!status.ok()) {
        return status;
    }

    out->default_value.reset();
    if (has_default) {
        db::Value value;
        status = DeserializeValue(bytes, offset, &value);
        if (!status.ok()) {
            return status;
        }
        out->default_value = std::move(value);
    }

    return db::Status::Ok();
}

void SerializeTableSchema(const db::TableSchema& schema, ByteBuffer* buffer) {
    WriteUint32(buffer, static_cast<std::uint32_t>(schema.columns.size()));
    for (const db::ColumnSchema& column : schema.columns) {
        SerializeColumnSchema(column, buffer);
    }
}

db::Status DeserializeTableSchema(const ByteBuffer& bytes,
                                  std::size_t* offset,
                                  db::TableSchema* out) {
    std::uint32_t column_count = 0;
    db::Status status = ReadUint32(bytes, offset, &column_count);
    if (!status.ok()) {
        return status;
    }

    out->columns.clear();
    out->columns.reserve(column_count);
    for (std::uint32_t i = 0; i < column_count; ++i) {
        db::ColumnSchema column;
        status = DeserializeColumnSchema(bytes, offset, &column);
        if (!status.ok()) {
            return status;
        }
        out->columns.push_back(std::move(column));
    }

    return db::Status::Ok();
}

void SerializeIndexDescriptor(const db::IndexDescriptor& index, ByteBuffer* buffer) {
    WriteString(buffer, index.name);
    WriteString(buffer, index.table_name);
    WriteString(buffer, index.column_name);
    WriteBool(buffer, index.unique);
}

db::Status DeserializeIndexDescriptor(const ByteBuffer& bytes,
                                      std::size_t* offset,
                                      db::IndexDescriptor* out) {
    db::Status status = ReadString(bytes, offset, &out->name);
    if (!status.ok()) {
        return status;
    }

    status = ReadString(bytes, offset, &out->table_name);
    if (!status.ok()) {
        return status;
    }

    status = ReadString(bytes, offset, &out->column_name);
    if (!status.ok()) {
        return status;
    }

    status = ReadBool(bytes, offset, &out->unique);
    return status;
}

void SerializeTableDescriptor(const db::TableDescriptor& desc, ByteBuffer* buffer) {
    WriteString(buffer, desc.database_name);
    WriteString(buffer, desc.table_name);
    WriteString(buffer, desc.data_file);
    WriteString(buffer, desc.index_file);
    SerializeTableSchema(desc.schema, buffer);
    WriteUint32(buffer, static_cast<std::uint32_t>(desc.indexes.size()));
    for (const db::IndexDescriptor& index : desc.indexes) {
        SerializeIndexDescriptor(index, buffer);
    }
}

db::Status DeserializeTableDescriptor(const ByteBuffer& bytes,
                                      std::size_t* offset,
                                      db::TableDescriptor* out) {
    db::Status status = ReadString(bytes, offset, &out->database_name);
    if (!status.ok()) {
        return status;
    }

    status = ReadString(bytes, offset, &out->table_name);
    if (!status.ok()) {
        return status;
    }

    status = ReadString(bytes, offset, &out->data_file);
    if (!status.ok()) {
        return status;
    }

    status = ReadString(bytes, offset, &out->index_file);
    if (!status.ok()) {
        return status;
    }

    status = DeserializeTableSchema(bytes, offset, &out->schema);
    if (!status.ok()) {
        return status;
    }

    std::uint32_t index_count = 0;
    status = ReadUint32(bytes, offset, &index_count);
    if (!status.ok()) {
        return status;
    }

    out->indexes.clear();
    out->indexes.reserve(index_count);
    for (std::uint32_t i = 0; i < index_count; ++i) {
        db::IndexDescriptor index;
        status = DeserializeIndexDescriptor(bytes, offset, &index);
        if (!status.ok()) {
            return status;
        }
        out->indexes.push_back(std::move(index));
    }

    return db::Status::Ok();
}

db::Status WriteTableMetaFile(const std::string& path, const db::TableDescriptor& desc) {
    ByteBuffer bytes;
    SerializeTableDescriptor(desc, &bytes);
    return db::file_utils::WriteAllBytes(path, bytes);
}

db::Status ReadTableMetaFile(const std::string& path, db::TableDescriptor* out) {
    ByteBuffer bytes;
    db::Status status = db::file_utils::ReadAllBytes(path, &bytes);
    if (!status.ok()) {
        return status;
    }

    std::size_t offset = 0;
    status = DeserializeTableDescriptor(bytes, &offset, out);
    if (!status.ok()) {
        return status;
    }

    if (offset != bytes.size()) {
        return db::Status::Error(db::StatusCode::kInvalidArgument,
                                 "Corrupted metadata: trailing bytes after table descriptor");
    }

    return ValidateTableDescriptor(*out);
}

}  // namespace

db::CatalogManager::CatalogManager(std::string root_dir)
    : root_dir_(std::move(root_dir)) {}

db::Status db::CatalogManager::CreateDatabase(const std::string& db_name) {
    Status status = ValidateDatabaseName(db_name);
    if (!status.ok()) {
        return status;
    }

    status = file_utils::EnsureDir(root_dir_);
    if (!status.ok()) {
        return status;
    }

    if (DatabaseExists(root_dir_, db_name)) {
        return Status::Error(StatusCode::kAlreadyExists, "Database already exists");
    }

    status = file_utils::EnsureDir(DatabasePath(root_dir_, db_name));
    if (!status.ok()) {
        return status;
    }
    status = file_utils::EnsureDir(CatalogDirPath(root_dir_, db_name));
    if (!status.ok()) {
        return status;
    }
    status = file_utils::EnsureDir(TablesDirPath(root_dir_, db_name));
    if (!status.ok()) {
        return status;
    }
    status = file_utils::EnsureDir(IndexesDirPath(root_dir_, db_name));
    if (!status.ok()) {
        return status;
    }
    return file_utils::EnsureDir(VersionsDirPath(root_dir_, db_name));
}

db::Status db::CatalogManager::DropDatabase(const std::string& db_name) {
    Status status = ValidateDatabaseName(db_name);
    if (!status.ok()) {
        return status;
    }

    const std::string path = DatabasePath(root_dir_, db_name);
    if (!DatabaseExists(root_dir_, db_name)) {
        return Status::Error(StatusCode::kNotFound, "Database not found");
    }

    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec) {
        return Status::Error(StatusCode::kIoError, "Failed to remove database directory");
    }

    return Status::Ok();
}

db::Status db::CatalogManager::CreateTable(const TableDescriptor& table) {
    if (!DatabaseExists(root_dir_, table.database_name)) {
        return Status::Error(StatusCode::kNotFound, "Database not found");
    }

    TableDescriptor normalized = NormalizeTableDescriptor(root_dir_, table);
    Status status = ValidateTableDescriptor(normalized);
    if (!status.ok()) {
        return status;
    }

    const std::string meta_path =
        TableMetaPath(root_dir_, normalized.database_name, normalized.table_name);
    if (TableMetaExists(root_dir_, normalized.database_name, normalized.table_name)) {
        return Status::Error(StatusCode::kAlreadyExists, "Table already exists");
    }

    return WriteTableMetaFile(meta_path, normalized);
}

db::Status db::CatalogManager::DropTable(const std::string& db_name,
                                         const std::string& table_name) {
    Status status = ValidateDatabaseName(db_name);
    if (!status.ok()) {
        return status;
    }

    status = ValidateTableName(table_name);
    if (!status.ok()) {
        return status;
    }

    if (!DatabaseExists(root_dir_, db_name)) {
        return Status::Error(StatusCode::kNotFound, "Database not found");
    }

    const std::string meta_path = TableMetaPath(root_dir_, db_name, table_name);
    bool exists = false;
    status = file_utils::FileExists(meta_path, &exists);
    if (!status.ok()) {
        return status;
    }
    if (!exists) {
        return Status::Error(StatusCode::kNotFound, "Table metadata not found");
    }

    return file_utils::RemoveFile(meta_path);
}

db::Status db::CatalogManager::GetTable(const std::string& db_name,
                                        const std::string& table_name,
                                        TableDescriptor* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "Output table descriptor is null");
    }

    Status status = ValidateDatabaseName(db_name);
    if (!status.ok()) {
        return status;
    }

    status = ValidateTableName(table_name);
    if (!status.ok()) {
        return status;
    }

    if (!DatabaseExists(root_dir_, db_name)) {
        return Status::Error(StatusCode::kNotFound, "Database not found");
    }

    const std::string meta_path = TableMetaPath(root_dir_, db_name, table_name);
    bool exists = false;
    status = file_utils::FileExists(meta_path, &exists);
    if (!status.ok()) {
        return status;
    }
    if (!exists) {
        return Status::Error(StatusCode::kNotFound, "Table metadata not found");
    }

    return ReadTableMetaFile(meta_path, out);
}

db::Status db::CatalogManager::ListTables(const std::string& db_name,
                                          std::vector<TableDescriptor>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "Output tables vector is null");
    }

    Status status = ValidateDatabaseName(db_name);
    if (!status.ok()) {
        return status;
    }

    if (!DatabaseExists(root_dir_, db_name)) {
        return Status::Error(StatusCode::kNotFound, "Database not found");
    }

    const std::string catalog_dir = CatalogDirPath(root_dir_, db_name);
    std::error_code ec;
    if (!fs::exists(catalog_dir, ec) || !fs::is_directory(catalog_dir, ec)) {
        return Status::Error(StatusCode::kNotFound, "Catalog directory not found");
    }

    out->clear();
    for (const fs::directory_entry& entry : fs::directory_iterator(catalog_dir, ec)) {
        if (ec) {
            return Status::Error(StatusCode::kIoError, "Failed to iterate catalog directory");
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".meta") {
            continue;
        }

        TableDescriptor desc;
        status = ReadTableMetaFile(entry.path().string(), &desc);
        if (!status.ok()) {
            return status;
        }
        out->push_back(std::move(desc));
    }

    std::sort(out->begin(), out->end(), [](const TableDescriptor& lhs, const TableDescriptor& rhs) {
        return lhs.table_name < rhs.table_name;
    });

    return Status::Ok();
}
