add_library(coursedb_common STATIC)

target_sources(coursedb_common
    PRIVATE
        common/status.cpp
        common/error.cpp
        common/bytes.cpp
        common/file_utils.cpp
        common/config.cpp
        common/binary_io.cpp
        common/catalog_serialization.cpp
        common/value_serialization.cpp
        common/row_serialization.cpp
        execution/value.cpp
)

target_include_directories(coursedb_common
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_common
    PUBLIC
        coursedb_project_options
)

# ── coursedb_logging ─────────────────────────────────────────────────────────
add_library(coursedb_logging STATIC)

target_sources(coursedb_logging
    PRIVATE
        logging/logger.cpp
)

target_include_directories(coursedb_logging
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_logging
    PUBLIC
        coursedb_common
        nlohmann_json::nlohmann_json
        coursedb_project_options
)

# ── coursedb_telemetry ────────────────────────────────────────────────────────
add_library(coursedb_telemetry STATIC)

target_sources(coursedb_telemetry
    PRIVATE
        telemetry/metrics.cpp
)

target_include_directories(coursedb_telemetry
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_telemetry
    PUBLIC
        coursedb_common
        coursedb_project_options
)

# ── coursedb_auth ─────────────────────────────────────────────────────────────
add_library(coursedb_auth STATIC)

target_sources(coursedb_auth
    PRIVATE
        auth/rbac.cpp
        auth/jwt_auth.cpp
)

target_include_directories(coursedb_auth
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_auth
    PUBLIC
        coursedb_common
        coursedb_logging
        nlohmann_json::nlohmann_json
        jwt-cpp::jwt-cpp
        OpenSSL::Crypto
        OpenSSL::SSL
        coursedb_project_options
)

# ── coursedb_queue ────────────────────────────────────────────────────────────
add_library(coursedb_queue STATIC)

target_sources(coursedb_queue
    PRIVATE
        queue/task_queue.cpp
)

target_include_directories(coursedb_queue
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_queue
    PUBLIC
        coursedb_common
        coursedb_logging
        nlohmann_json::nlohmann_json
        coursedb_project_options
)

# ── coursedb_storage ──────────────────────────────────────────────────────────
add_library(coursedb_storage STATIC)

target_sources(coursedb_storage
    PRIVATE
        storage/page_id.cpp
        storage/page.cpp
        storage/page_manager.cpp
        storage/row_id.cpp
        storage/row.cpp
        storage/row_store.cpp
        storage/table_storage.cpp

        catalog/catalog_manager.cpp
        catalog/schema.cpp
        catalog/table_descriptor.cpp
        catalog/index_descriptor.cpp

        index/bstar_tree.cpp
        index/index_manager.cpp
        index/key_encoder.cpp

        versioning/version_log.cpp
        versioning/version_record.cpp
        versioning/revert.cpp

        runtime/storage_node_engine.cpp
)

target_include_directories(coursedb_storage
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_storage
    PUBLIC
        coursedb_common
        coursedb_project_options
)

# ── coursedb_parser ───────────────────────────────────────────────────────────
add_library(coursedb_parser STATIC)

target_sources(coursedb_parser
    PRIVATE
        parser/token.cpp
        parser/lexer.cpp
        parser/parser.cpp
        parser/ast.cpp
        parser/sql_statement.cpp
        parser/parse_error.cpp
)

target_include_directories(coursedb_parser
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_parser
    PUBLIC
        coursedb_common
        coursedb_project_options
)

# ── coursedb_planner ──────────────────────────────────────────────────────────
add_library(coursedb_planner STATIC)

target_sources(coursedb_planner
    PRIVATE
        planner/binder.cpp
        planner/logical_plan.cpp
        planner/physical_plan.cpp
        planner/planner.cpp
        planner/optimizer.cpp
)

target_include_directories(coursedb_planner
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_planner
    PUBLIC
        coursedb_common
        coursedb_parser
        coursedb_storage
        coursedb_project_options
)

# ── coursedb_execution ────────────────────────────────────────────────────────
add_library(coursedb_execution STATIC)

target_sources(coursedb_execution
    PRIVATE
        execution/executor_context.cpp
        execution/executor.cpp
        execution/seq_scan_executor.cpp
        execution/index_scan_executor.cpp
        execution/insert_executor.cpp
        execution/update_executor.cpp
        execution/delete_executor.cpp
        execution/create_table_executor.cpp
        execution/create_index_executor.cpp
        execution/expression.cpp
        execution/tuple.cpp
)

target_include_directories(coursedb_execution
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_execution
    PUBLIC
        coursedb_common
        coursedb_storage
        coursedb_parser
        coursedb_planner
        coursedb_project_options
)

# ── coursedb_transaction ──────────────────────────────────────────────────────
add_library(coursedb_transaction STATIC)

target_sources(coursedb_transaction
    PRIVATE
        transaction/transaction.cpp
        transaction/transaction_manager.cpp
        transaction/lock_manager.cpp
        transaction/isolation_level.cpp
)

target_include_directories(coursedb_transaction
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_transaction
    PUBLIC
        coursedb_common
        coursedb_storage
        coursedb_project_options
)

# ── coursedb_network ──────────────────────────────────────────────────────────
add_library(coursedb_network STATIC)

target_sources(coursedb_network
    PRIVATE
        network/protocol.cpp
        network/request.cpp
        network/response.cpp
        network/session.cpp
        network/tcp_server.cpp
        network/tcp_client.cpp
)

target_include_directories(coursedb_network
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_network
    PUBLIC
        coursedb_common
        coursedb_project_options
)

if(WIN32)
    target_link_libraries(coursedb_network PUBLIC ws2_32)
endif()

# ── coursedb_cluster ──────────────────────────────────────────────────────────
add_library(coursedb_cluster STATIC)

target_sources(coursedb_cluster
    PRIVATE
        cluster/consistent_hash.cpp
        cluster/entrypoint.cpp
)

target_include_directories(coursedb_cluster
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_cluster
    PUBLIC
        coursedb_common
        coursedb_network
        coursedb_logging
        coursedb_telemetry
        nlohmann_json::nlohmann_json
        coursedb_project_options
)

# ── coursedb_server ───────────────────────────────────────────────────────────
add_library(coursedb_server STATIC)

target_sources(coursedb_server
    PRIVATE
        server/database.cpp
        server/query_processor.cpp
        server/query_result.cpp
        server/storage_service.cpp
        server/session_context.cpp
)

target_include_directories(coursedb_server
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_server
    PUBLIC
        coursedb_common
        coursedb_storage
        coursedb_parser
        coursedb_planner
        coursedb_execution
        coursedb_transaction
        coursedb_network
        coursedb_logging
        coursedb_telemetry
        coursedb_auth
        coursedb_queue
        nlohmann_json::nlohmann_json
        coursedb_project_options
)
