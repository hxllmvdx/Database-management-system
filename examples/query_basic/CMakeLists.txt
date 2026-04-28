add_executable(example_query_basic
    main.cpp
)

target_link_libraries(example_query_basic
    PRIVATE
        coursedb_server
        coursedb_common
        coursedb_project_options
)
