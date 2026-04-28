add_executable(coursedb_cli
    main.cpp
)

target_link_libraries(coursedb_cli
    PRIVATE
        coursedb_server
        coursedb_common
        coursedb_project_options
)
