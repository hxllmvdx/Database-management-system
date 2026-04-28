include(GoogleTest)

function(coursedb_add_test TARGET_NAME)
    set(options)
    set(one_value_args)
    set(multi_value_args SOURCES LIBRARIES)
    cmake_parse_arguments(TEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    set(existing_sources "")
    foreach(source IN LISTS TEST_SOURCES)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
            list(APPEND existing_sources "${source}")
        endif()
    endforeach()

    if(NOT existing_sources)
        return()
    endif()

    add_executable(${TARGET_NAME} ${existing_sources})
    target_link_libraries(${TARGET_NAME}
        PRIVATE
            ${TEST_LIBRARIES}
            coursedb_project_options
            GTest::gtest_main
    )
    gtest_discover_tests(${TARGET_NAME})
endfunction()

coursedb_add_test(coursedb_storage_tests
    SOURCES
        storage/page_manager_tests.cpp
        storage/row_store_tests.cpp
        storage/table_storage_tests.cpp
        storage/bstar_tree_tests.cpp
        storage/version_log_tests.cpp
    LIBRARIES
        coursedb_storage
        coursedb_common
)

coursedb_add_test(coursedb_parser_tests
    SOURCES
        parser/lexer_tests.cpp
        parser/parser_tests.cpp
        parser/ast_tests.cpp
    LIBRARIES
        coursedb_parser
        coursedb_common
)

coursedb_add_test(coursedb_planner_tests
    SOURCES
        planner/binder_tests.cpp
        planner/planner_tests.cpp
        planner/optimizer_tests.cpp
    LIBRARIES
        coursedb_planner
        coursedb_parser
        coursedb_storage
        coursedb_common
)

coursedb_add_test(coursedb_execution_tests
    SOURCES
        execution/seq_scan_executor_tests.cpp
        execution/insert_executor_tests.cpp
        execution/update_executor_tests.cpp
        execution/delete_executor_tests.cpp
        execution/expression_tests.cpp
    LIBRARIES
        coursedb_execution
        coursedb_planner
        coursedb_parser
        coursedb_storage
        coursedb_common
)

coursedb_add_test(coursedb_transaction_tests
    SOURCES
        transaction/transaction_manager_tests.cpp
        transaction/lock_manager_tests.cpp
    LIBRARIES
        coursedb_transaction
        coursedb_storage
        coursedb_common
)

coursedb_add_test(coursedb_network_tests
    SOURCES
        network/protocol_tests.cpp
        network/request_response_tests.cpp
    LIBRARIES
        coursedb_network
        coursedb_common
)

coursedb_add_test(coursedb_integration_tests
    SOURCES
        integration/database_tests.cpp
        integration/query_processor_tests.cpp
    LIBRARIES
        coursedb_server
        coursedb_execution
        coursedb_planner
        coursedb_parser
        coursedb_storage
        coursedb_transaction
        coursedb_network
        coursedb_common
)
