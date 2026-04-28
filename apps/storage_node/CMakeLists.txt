add_executable(coursedb_storage_node
    main.cpp
)

target_link_libraries(coursedb_storage_node
    PRIVATE
        coursedb_storage
        coursedb_common
        coursedb_project_options
)
