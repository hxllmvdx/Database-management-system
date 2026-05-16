readme_agata.txt

include/runtime/storage_node_engine.h
- добавлены приватные члены: catalog_manager_, index_manager_, open_tables_
- добавлен приватный метод OpenTable для ленивого открытия таблиц
- публичный api не изменился

src/runtime/storage_node_engine.cpp
- полная реализация StorageNodeEngine как композита над готовыми модулями CatalogManager, TableStorage, IndexManager
- методы: Start, Stop, CreateDatabase, DropDatabase, CreateTable, DropTable, Insert, Update, Delete, ScanTable, GetTableDescriptor
- OpenTable реализует кэш открытых таблиц по ключу db_name.table_name

include/server/database.h
- добавлен приватный член Config config_

src/server/database.cpp
- реализация Database::Start (создание data_dir + старт движка), Stop, engine

include/server/storage_service.h
- добавлен приватный член std::unordered_map<std::string, SessionContext> session_contexts_

src/server/storage_service.cpp
- реализация HandleRequest: маппинг Session -> SessionContext, вызов QueryProcessor, упаковка QueryResult в Response

src/network/protocol.cpp
- реализация SerializeRequest / DeserializeRequest / SerializeResponse / DeserializeResponse
- используется common/binary_io и common/row_serialization для стабильного бинарного формата

include/network/tcp_server.h
- добавлены приватные члены: listen_socket_ (void*), running_ (atomic<bool>), thread_ (std::thread)

src/network/tcp_server.cpp
- реализация TcpServer на Winsock2: WSAStartup, socket, bind, listen, accept в отдельном потоке
- чтение длины сообщения + тела, десериализация Request, вызов handler, отправка Response
- Stop закрывает слушающий сокет, join-ит поток, вызывает WSACleanup

src/network/tcp_client.cpp
- реализация TcpClient на Winsock2: WSAStartup, connect, отправка Request, чтение Response
- SendAll / RecvAll гарантируют полную передачу байт

apps/server/main.cpp
- полная реализация: парсинг аргументов, запуск Database, QueryProcessor, StorageService, TcpServer
- ожидание Enter для graceful shutdown

apps/cli/main.cpp
- реализация cli: читает sql (из аргументов или stdin), отправляет через TcpClient, печатает результат

apps/storage_node/main.cpp
- локальный repl без сети: читает sql из stdin, выполняет через Database + QueryProcessor напрямую

tests/network/protocol_tests.cpp
- round-trip тесты Request и Response
- проверка защиты от битых данных

tests/network/request_response_tests.cpp
- smoke-test TcpServer + TcpClient: старт, отправка, проверка ответа, остановка

tests/integration/database_tests.cpp
- тест жизненного цикла Database (Start / Stop)
- тест доступа к StorageNodeEngine и создания базы

tests/integration/query_processor_tests.cpp
- end-to-end sql: CREATE DATABASE -> USE -> CREATE TABLE -> INSERT -> SELECT
- проверка обработки синтаксической ошибки

src/CMakeLists.txt
- добавлена линковка ws2_32.lib под Windows для цели coursedb_network

заглушки:
src/network/request.cpp
src/network/response.cpp
src/network/session.cpp
src/server/query_result.cpp
src/server/session_context.cpp
