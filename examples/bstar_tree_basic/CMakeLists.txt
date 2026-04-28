add_executable(example_bstar_tree_basic
    main.cpp
)

target_link_libraries(example_bstar_tree_basic
    PRIVATE
        coursedb_storage
        coursedb_common
        coursedb_project_options
)
