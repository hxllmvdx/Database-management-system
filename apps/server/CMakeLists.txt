add_executable(coursedb_server_app
    main.cpp
)

target_link_libraries(coursedb_server_app
    PRIVATE
        coursedb_server
        coursedb_common
        coursedb_project_options
)
