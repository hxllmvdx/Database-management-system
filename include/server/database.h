// исходный код написан человеком 1, комментарии добавлены человеком 3
#pragma once
#include "../common/config.h"  // конфигурация сервера (порт, адрес, директория данных)
#include "../common/status.h"  // коды возврата операций (Ok, Error)

namespace db { // пространство имён базы данных

class StorageNodeEngine; // forward declaration: реализация у человека 1 (runtime/storage_node_engine)

class Database { // композиционный объект: держит engine (человек 1) и сервисы верхнего уровня
public:
    explicit Database(Config config); // принимает конфигурацию (data_dir, host, port)

    Status Start(); // инициализация окружения и запуск StorageNodeEngine (TODO: интеграция с человеком 1)
    Status Stop();  // корректное завершение работы и освобождение ресурсов

    StorageNodeEngine& engine(); // доступ к low-level engine для QueryProcessor (человек 1)
    const Config& config() const; // доступ к конфигурации (используется apps/server)

private:
    Config config_;              // сохранённая конфигурация
    StorageNodeEngine* engine_ = nullptr; // указатель на engine (человек 1), пока nullptr stub
}; // lifecycle: конструктор → Start() → ... → Stop()

} // namespace db
