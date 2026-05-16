#include "runtime/storage_node_engine.h"               // интерфейс движка хранения

#include <unordered_map>                                // для кэша открытых таблиц
#include "catalog/catalog_manager.h"                    // работа с метаданными
#include "common/file_utils.h"                          // создание директорий
#include "common/status.h"                              // коды возврата
#include "storage/table_storage.h"                      // доступ к строкам таблицы

namespace db {

StorageNodeEngine::StorageNodeEngine(Config config)    // конструктор принимает конфигурацию
    : config_(std::move(config)),                       // сохраняем настройки
      catalog_manager_(config_.data_dir + "/catalog"),  // каталог лежит в data_dir
      index_manager_(config_.page_size) {}              // менеджер индексов знает размер страницы

Status StorageNodeEngine::Start() {                     // инициализация окружения
    Status status = file_utils::EnsureDir(config_.data_dir); // создаём корень данных, если нет
    if (!status.ok()) return status;                    // при неудаче сразу возвращаем ошибку
    return Status::Ok();                                // всё готово к работе
}

Status StorageNodeEngine::Stop() {                      // остановка движка
    open_tables_.clear();                               // закрываем все открытые таблицы
    return Status::Ok();                                // успешно
}

Status StorageNodeEngine::CreateDatabase(const std::string& db_name) { // создание базы
    return catalog_manager_.CreateDatabase(db_name);    // делегируем каталог-менеджеру
}

Status StorageNodeEngine::DropDatabase(const std::string& db_name) { // удаление базы
    for (auto it = open_tables_.begin(); it != open_tables_.end();) { // убираем таблицы этой бд из кэша
        if (it->first.rfind(db_name + ".", 0) == 0) {
            it = open_tables_.erase(it);                // стираем по итератору
        } else {
            ++it;                                       // идём дальше
        }
    }
    return catalog_manager_.DropDatabase(db_name);      // удаляем метаданные
}

Status StorageNodeEngine::CreateTable(const TableDescriptor& desc) { // создание таблицы
    return catalog_manager_.CreateTable(desc);          // пишем мета-файл через каталог
}

Status StorageNodeEngine::DropTable(const std::string& db_name,
                                    const std::string& table_name) { // удаление таблицы
    std::string key = db_name + "." + table_name;       // ключ для кэша
    open_tables_.erase(key);                            // убираем из открытых
    return catalog_manager_.DropTable(db_name, table_name); // удаляем метаданные
}

Status StorageNodeEngine::Insert(const std::string& db_name,
                                 const std::string& table_name,
                                 const Tuple& tuple,
                                 RowId* out_rid) {      // вставка строки
    TableStorage* storage = nullptr;                    // указатель на хранилище
    Status status = OpenTable(db_name, table_name, &storage); // открываем/находим таблицу
    if (!status.ok()) return status;                    // проброс ошибки
    return storage->Insert(tuple, out_rid);             // делегируем хранилищу строк
}

Status StorageNodeEngine::Update(const std::string& db_name,
                                 const std::string& table_name,
                                 RowId rid,
                                 const Tuple& tuple) {  // обновление строки
    TableStorage* storage = nullptr;
    Status status = OpenTable(db_name, table_name, &storage);
    if (!status.ok()) return status;
    return storage->Update(rid, tuple);                 // обновляем по row_id
}

Status StorageNodeEngine::Delete(const std::string& db_name,
                                 const std::string& table_name,
                                 RowId rid) {           // удаление строки
    TableStorage* storage = nullptr;
    Status status = OpenTable(db_name, table_name, &storage);
    if (!status.ok()) return status;
    return storage->Delete(rid);                        // помечаем удалённой
}

Status StorageNodeEngine::ScanTable(const std::string& db_name,
                                    const std::string& table_name,
                                    std::vector<Row>* out) { // полное сканирование
    TableStorage* storage = nullptr;
    Status status = OpenTable(db_name, table_name, &storage);
    if (!status.ok()) return status;
    return storage->Scan(out);                          // возвращаем все живые строки
}

Status StorageNodeEngine::GetTableDescriptor(const std::string& db_name,
                                             const std::string& table_name,
                                             TableDescriptor* out) { // получение схемы
    return catalog_manager_.GetTable(db_name, table_name, out); // читаем мета-файл
}

CatalogManager& StorageNodeEngine::catalog() {          // доступ к каталогу
    return catalog_manager_;
}

IndexManager& StorageNodeEngine::index_manager() {      // доступ к индексам
    return index_manager_;
}

Status StorageNodeEngine::OpenTable(const std::string& db_name,
                                    const std::string& table_name,
                                    TableStorage** out) { // ленивое открытие таблицы
    std::string key = db_name + "." + table_name;       // составной ключ
    auto it = open_tables_.find(key);                   // ищем в кэше
    if (it != open_tables_.end()) {                     // уже открыта
        *out = it->second.get();                        // отдаём указатель
        return Status::Ok();
    }
    TableDescriptor desc;                               // читаем дескриптор
    Status status = catalog_manager_.GetTable(db_name, table_name, &desc);
    if (!status.ok()) return status;                    // таблицы нет
    auto storage = std::make_unique<TableStorage>(desc, config_.page_size); // создаём хранилище
    status = storage->Open();                           // открываем файл строк
    if (!status.ok()) return status;                    // не удалось
    *out = storage.get();                               // отдаём сырой указатель
    open_tables_[key] = std::move(storage);             // сохраняем в кэше
    return Status::Ok();                                // успех
}

} // namespace db
