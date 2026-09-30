cmake_minimum_required(VERSION 3.12)
project(SimpleDBMS CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.cpp
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/storage/table_file_manager.cpp
    src/core/table.cpp
    src/core/database.cpp
)

add_executable(dbms ${SOURCES})

target_include_directories(dbms PRIVATE src)