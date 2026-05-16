#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "table.h"
#include "../parser/parser.h"

class Database {
private:
    std::string db_name;
    // Словарь: имя_таблицы -> объект Table
    std::unordered_map<std::string, std::unique_ptr<Table>> tables;

public:
    explicit Database(std::string name);

    // Создание таблицы по схеме из парсера
    void createTable(const CreateTableStmt& stmt);
    void dropTable(const std::string& table_name);

    // Получить указатель на таблицу по имени (для INSERT, SELECT и т.д.)
    Table* getTable(const std::string& name);

    std::string getName() const;
};