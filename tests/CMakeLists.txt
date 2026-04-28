include(GoogleTest)

add_executable(coursedb_storage_tests
    storage/page_manager_tests.cpp
    storage/row_store_tests.cpp
    storage/table_storage_tests.cpp
    storage/bstar_tree_tests.cpp
    storage/version_log_tests.cpp
)

target_link_libraries(coursedb_storage_tests
    PRIVATE
        coursedb_storage
        coursedb_common
        coursedb_project_options
        GTest::gtest_main
)

gtest_discover_tests(coursedb_storage_tests)

add_executable(coursedb_parser_tests
    parser/lexer_tests.cpp
    parser/parser_tests.cpp
    parser/ast_tests.cpp
)

target_link_libraries(coursedb_parser_tests
    PRIVATE
        coursedb_parser
        coursedb_common
        coursedb_project_options
        GTest::gtest_main
)

gtest_discover_tests(coursedb_parser_tests)

add_executable(coursedb_planner_tests
    planner/binder_tests.cpp
    planner/planner_tests.cpp
    planner/optimizer_tests.cpp
)

target_link_libraries(coursedb_planner_tests
    PRIVATE
        coursedb_planner
        coursedb_parser
        coursedb_storage
        coursedb_common
        coursedb_project_options
        GTest::gtest_main
)

gtest_discover_tests(coursedb_planner_tests)

add_executable(coursedb_execution_tests
    execution/seq_scan_executor_tests.cpp
    execution/insert_executor_tests.cpp
    execution/update_executor_tests.cpp
    execution/delete_executor_tests.cpp
    execution/expression_tests.cpp
)

target_link_libraries(coursedb_execution_tests
    PRIVATE
        coursedb_execution
        coursedb_planner
        coursedb_parser
        coursedb_storage
        coursedb_common
        coursedb_project_options
        GTest::gtest_main
)

gtest_discover_tests(coursedb_execution_tests)

add_executable(coursedb_transaction_tests
    transaction/transaction_manager_tests.cpp
    transaction/lock_manager_tests.cpp
)

target_link_libraries(coursedb_transaction_tests
    PRIVATE
        coursedb_transaction
        coursedb_storage
        coursedb_common
        coursedb_project_options
        GTest::gtest_main
)

gtest_discover_tests(coursedb_transaction_tests)

add_executable(coursedb_network_tests
    network/protocol_tests.cpp
    network/request_response_tests.cpp
)

target_link_libraries(coursedb_network_tests
    PRIVATE
        coursedb_network
        coursedb_common
        coursedb_project_options
        GTest::gtest_main
)

gtest_discover_tests(coursedb_network_tests)

add_executable(coursedb_integration_tests
    integration/database_tests.cpp
    integration/query_processor_tests.cpp
)

target_link_libraries(coursedb_integration_tests
    PRIVATE
        coursedb_server
        coursedb_execution
        coursedb_planner
        coursedb_parser
        coursedb_storage
        coursedb_transaction
        coursedb_network
        coursedb_common
        coursedb_project_options
        GTest::gtest_main
)

gtest_discover_tests(coursedb_integration_tests)
