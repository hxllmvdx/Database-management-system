#include "catalog/catalog_manager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "catalog/schema.h"
#include "common/binary_io.h"
#include "common/catalog_serialization.h"
#include "common/file_utils.h"
#include "execution/value.h"

namespace {

namespace fs = std::filesystem;

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

std::string DefaultTableIndexDir(const std::string& root_dir,
                                 const std::string& db_name,
                                 const std::string& table_name) {
    return IndexesDirPath(root_dir, db_name) + "/" + table_name;
}

std::string DefaultIndexFilePath(const std::string& index_dir, const std::string& index_name) {
    return index_dir + "/" + index_name + ".idx";
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
    if (column.type == db::ColumnType::kBool && default_type != db::ValueType::kBool) {
        return db::Status::Error(db::StatusCode::kInvalidArgument,
                                 "Invalid default value for bool column");
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
    if (index.file_path.empty()) {
        return db::Status::Error(db::StatusCode::kInvalidArgument, "Index file path is empty");
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
    if (desc.index_dir.empty()) {
        return db::Status::Error(db::StatusCode::kInvalidArgument, "Table index directory path is empty");
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
    if (desc.index_dir.empty()) {
        desc.index_dir = DefaultTableIndexDir(root_dir, desc.database_name, desc.table_name);
    }

    for (db::IndexDescriptor& index : desc.indexes) {
        if (index.table_name.empty()) {
            index.table_name = desc.table_name;
        }
        if (index.file_path.empty()) {
            index.file_path = DefaultIndexFilePath(desc.index_dir, index.name);
        }
    }

    return desc;
}

db::Status WriteTableMetaFile(const std::string& path, const db::TableDescriptor& desc) {
    db::ByteBuffer bytes;
    db::catalog_serialization::SerializeTableDescriptor(desc, &bytes);
    return db::file_utils::WriteAllBytes(path, bytes);
}

db::Status ReadTableMetaFile(const std::string& path, db::TableDescriptor* out) {
    db::ByteBuffer bytes;
    db::Status status = db::file_utils::ReadAllBytes(path, &bytes);
    if (!status.ok()) {
        return status;
    }

    std::size_t offset = 0;
    status = db::catalog_serialization::DeserializeTableDescriptor(bytes, &offset, out);
    if (!status.ok()) {
        return status;
    }

    if (offset != bytes.size()) {
        return db::Status::Error(db::StatusCode::kInvalidArgument,
                                 "Corrupted metadata: trailing bytes after table descriptor");
    }

    return ValidateTableDescriptor(*out);
}

}  

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
