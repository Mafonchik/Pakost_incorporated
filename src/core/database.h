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
    explicit Database(std::string name);

    void createTable(const CreateTableStmt& stmt);
    void dropTable(const std::string& table_name);
    Table* getTable(const std::string& name);
    std::string getName() const;
    const std::string& getRootDir() const;
};