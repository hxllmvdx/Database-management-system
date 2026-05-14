add_library(coursedb_common STATIC)

target_sources(coursedb_common
    PRIVATE
        common/status.cpp
        common/config.cpp
        common/value.cpp
        common/tuple.cpp
)

target_include_directories(coursedb_common
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_common
    PUBLIC
        coursedb_project_options
)

add_library(coursedb_network STATIC)

target_sources(coursedb_network
    PRIVATE
        network/protocol.cpp
        network/request.cpp
        network/response.cpp
        network/session.cpp
        network/tcp_server.cpp
        network/tcp_client.cpp
)

target_include_directories(coursedb_network
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_network
    PUBLIC
        coursedb_common
        coursedb_project_options
)

if(WIN32)
    target_link_libraries(coursedb_network
        PUBLIC
            ws2_32
    )
endif()

add_library(coursedb_server STATIC)

target_sources(coursedb_server
    PRIVATE
        server/database.cpp
        server/query_processor.cpp
        server/query_result.cpp
        server/storage_service.cpp
        server/session_context.cpp
)

target_include_directories(coursedb_server
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(coursedb_server
    PUBLIC
        coursedb_common
        coursedb_network
        coursedb_project_options
)
