cmake_minimum_required(VERSION 3.12)
project(SimpleDBMS CXX)

# Устанавливаем стандарт C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Все исходные файлы проекта
set(SOURCES
    src/main.cpp
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/storage/table_file_manager.cpp
    src/core/table.cpp
    src/core/database.cpp
)

# Создаем исполняемый файл нашей СУБД
add_executable(dbms ${SOURCES})

# Если нужно, чтобы заголовочные файлы искались по относительным путям вида #include "parser/lexer.h",
# можно добавить корневую папку src в директории включения:
target_include_directories(dbms PRIVATE src)