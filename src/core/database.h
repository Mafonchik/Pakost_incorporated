#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "table.h"
#include "../parser/parser.h"

class Database {
private:
    std::string db_name;
    std::string root_dir;
    std::unordered_map<std::string, std::unique_ptr<Table>> tables;

public:
    static const char* dataRoot() { return "./data"; }
    static std::string pathFor(const std::string& name);
    static bool existsOnDisk(const std::string& name);

    // create == true: создать каталог базы; false: открыть существующую и загрузить все её таблицы
    Database(std::string name, bool create);

    void createTable(const CreateTableStmt& stmt);
    void dropTable(const std::string& table_name);
    Table& getTable(const std::string& name);

    const std::string& getName() const { return db_name; }
    const std::string& getRootDir() const { return root_dir; }
};
