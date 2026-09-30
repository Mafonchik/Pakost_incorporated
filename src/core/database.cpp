#include "database.h"

#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

std::string Database::pathFor(const std::string& name) {
    return std::string(dataRoot()) + "/" + name;
}

bool Database::existsOnDisk(const std::string& name) {
    std::error_code ec;
    return fs::is_directory(pathFor(name), ec);
}

Database::Database(std::string name, bool create)
    : db_name(std::move(name)), root_dir(pathFor(db_name)) {
    if (create) {
        fs::create_directories(root_dir);
        return;
    }

    if (!existsOnDisk(db_name)) {
        throw std::runtime_error("Ошибка: база данных '" + db_name + "' не существует.");
    }

    for (const auto& entry : fs::directory_iterator(root_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".meta") {
            std::string table_name = entry.path().stem().string();
            tables[table_name] = std::make_unique<Table>(table_name, root_dir);
        }
    }
}

void Database::createTable(const CreateTableStmt& stmt) {
    if (tables.count(stmt.table_name)) {
        throw std::runtime_error("Ошибка: таблица '" + stmt.table_name + "' уже существует в базе данных '" + db_name + "'.");
    }
    tables[stmt.table_name] = std::make_unique<Table>(stmt.table_name, stmt, root_dir);
}

void Database::dropTable(const std::string& table_name) {
    auto it = tables.find(table_name);
    if (it == tables.end()) {
        throw std::runtime_error("Ошибка: таблица '" + table_name + "' не существует в базе данных '" + db_name + "'.");
    }

    std::vector<std::string> files = it->second->filePaths();

    tables.erase(it);

    for (const auto& path : files) {
        std::error_code ec;
        fs::remove(path, ec);
    }
}

Table& Database::getTable(const std::string& name) {
    auto it = tables.find(name);
    if (it == tables.end()) {
        throw std::runtime_error("Ошибка: таблица '" + name + "' не найдена в базе данных '" + db_name + "'.");
    }
    return *it->second;
}
