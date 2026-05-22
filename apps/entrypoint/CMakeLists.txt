add_executable(coursedb_entrypoint
    main.cpp
)

target_link_libraries(coursedb_entrypoint
    PRIVATE
        coursedb_cluster
        coursedb_network
        coursedb_logging
        coursedb_telemetry
        coursedb_common
        nlohmann_json::nlohmann_json
        coursedb_project_options
)
