add_executable(coursedb_server_app
    main.cpp
)

target_link_libraries(coursedb_server_app
    PRIVATE
        coursedb_server
        coursedb_logging
        coursedb_auth
        coursedb_queue
        coursedb_telemetry
        coursedb_common
        coursedb_project_options
)
