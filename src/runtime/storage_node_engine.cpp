#include "runtime/storage_node_engine.h"
#include "catalog/catalog_manager.h"
#include "storage/table_storage.h"
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace db {

struct StorageNodeEngine::Impl {
    Config config;
    std::unique_ptr<CatalogManager> catalog;
    IndexManager index_manager;
    std::unordered_map<std::string, std::unique_ptr<TableStorage>> tables; // кэш открытых таблиц
};

StorageNodeEngine::StorageNodeEngine(Config config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
}

StorageNodeEngine::~StorageNodeEngine() = default;

Status StorageNodeEngine::Start() {
    std::filesystem::create_directories(impl_->config.data_dir);
    impl_->catalog = std::make_unique<CatalogManager>(impl_->config.data_dir);
    return Status::Ok();
}

Status StorageNodeEngine::Stop() {
    impl_->tables.clear(); // закрываем все открытые таблицы
    impl_->catalog.reset();
    return Status::Ok();
}

Status StorageNodeEngine::CreateDatabase(const std::string& db_name) {
    if (!impl_->catalog) return Status::Error(StatusCode::kInternalError, "engine not started");
    return impl_->catalog->CreateDatabase(db_name);
}

Status StorageNodeEngine::DropDatabase(const std::string& db_name) {
    if (!impl_->catalog) return Status::Error(StatusCode::kInternalError, "engine not started");
    return impl_->catalog->DropDatabase(db_name);
}

Status StorageNodeEngine::CreateTable(const TableDescriptor& desc) {
    if (!impl_->catalog) return Status::Error(StatusCode::kInternalError, "engine not started");
    auto st = impl_->catalog->CreateTable(desc);
    if (!st.ok()) return st;
    // получаем нормализованный дескриптор (с путями)
    TableDescriptor normalized;
    st = impl_->catalog->GetTable(desc.database_name, desc.table_name, &normalized);
    if (!st.ok()) return st;
    // открываем таблицу, чтобы создать файлы
    TableStorage storage(normalized, impl_->config.page_size);
    return storage.Open();
}

Status StorageNodeEngine::DropTable(const std::string& db_name,
                                    const std::string& table_name) {
    if (!impl_->catalog) return Status::Error(StatusCode::kInternalError, "engine not started");
    return impl_->catalog->DropTable(db_name, table_name);
}

namespace {

// ключ для кэша таблиц
std::string MakeTableKey(const std::string& db, const std::string& tbl) {
    return db + "." + tbl;
}

} // anonymous namespace

// получаем или создаём tablestorage для таблицы
TableStorage* StorageNodeEngine::GetTableStorage(const std::string& db_name,
                                                  const std::string& table_name) {
    if (!impl_->catalog) return nullptr;
    std::string key = MakeTableKey(db_name, table_name);
    auto it = impl_->tables.find(key);
    if (it != impl_->tables.end()) return it->second.get();

    TableDescriptor desc;
    auto st = impl_->catalog->GetTable(db_name, table_name, &desc);
    if (!st.ok()) return nullptr;

    auto storage = std::make_unique<TableStorage>(desc, impl_->config.page_size);
    st = storage->Open();
    if (!st.ok()) return nullptr;

    TableStorage* ptr = storage.get();
    impl_->tables.emplace(key, std::move(storage));
    return ptr;
}

Status StorageNodeEngine::Insert(const std::string& db_name,
                                 const std::string& table_name,
                                 const Tuple& tuple,
                                 RowId* out_rid) {
    auto* storage = GetTableStorage(db_name, table_name);
    if (!storage) return Status::Error(StatusCode::kNotFound, "table not found");
    return storage->Insert(tuple, out_rid);
}

Status StorageNodeEngine::Update(const std::string& db_name,
                                 const std::string& table_name,
                                 RowId rid,
                                 const Tuple& tuple) {
    auto* storage = GetTableStorage(db_name, table_name);
    if (!storage) return Status::Error(StatusCode::kNotFound, "table not found");
    return storage->Update(rid, tuple);
}

Status StorageNodeEngine::Delete(const std::string& db_name,
                                 const std::string& table_name,
                                 RowId rid) {
    auto* storage = GetTableStorage(db_name, table_name);
    if (!storage) return Status::Error(StatusCode::kNotFound, "table not found");
    return storage->Delete(rid);
}

Status StorageNodeEngine::ScanTable(const std::string& db_name,
                                    const std::string& table_name,
                                    std::vector<Row>* out) {
    auto* storage = GetTableStorage(db_name, table_name);
    if (!storage) return Status::Error(StatusCode::kNotFound, "table not found");
    return storage->Scan(out);
}

Status StorageNodeEngine::GetTableDescriptor(const std::string& db_name,
                                             const std::string& table_name,
                                             TableDescriptor* out) {
    if (!impl_->catalog) return Status::Error(StatusCode::kInternalError, "engine not started");
    return impl_->catalog->GetTable(db_name, table_name, out);
}

CatalogManager& StorageNodeEngine::catalog() {
    return *impl_->catalog;
}

IndexManager& StorageNodeEngine::index_manager() {
    return impl_->index_manager;
}

} // namespace db
