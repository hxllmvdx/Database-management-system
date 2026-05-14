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
        coursedb_network
        coursedb_common
)
