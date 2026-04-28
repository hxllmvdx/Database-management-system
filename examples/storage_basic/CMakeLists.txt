add_executable(example_storage_basic
    main.cpp
)

target_link_libraries(example_storage_basic
    PRIVATE
        coursedb_storage
        coursedb_common
        coursedb_project_options
)
