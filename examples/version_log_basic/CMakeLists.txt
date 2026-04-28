add_executable(example_version_log_basic
    main.cpp
)

target_link_libraries(example_version_log_basic
    PRIVATE
        coursedb_storage
        coursedb_common
        coursedb_project_options
)
