#pragma once

#include <unordered_map>
#include <memory>
#include <iostream>
#include "database.h"
#include "table.h"
#include "../parser/parser.h"

class QueryExecutor {
private:
    // Коллекция всех баз данных на уровне системы
    std::unordered_map<std::string, Database> databases;
    Database* current_db = nullptr; // Указатель на активную базу

    // Вспомогательный метод для проверки контекста БД
    void ensureDatabaseSelected() const {
        if (!current_db) {
            throw std::runtime_error("Ошибка: не выбрана активная база данных. Сначала выполните команду USE database_name;");
        }
    }

public:
    void executeCreateTable(const CreateTableStmt& stmt) {
        ensureDatabaseSelected();
        current_db->createTable(stmt);
    }

    void executeInsert(const InsertStmt& stmt) {
        ensureDatabaseSelected();
        Table* table = current_db->getTable(stmt.table_name);
        
        int key = std::stoi(stmt.values[0].value);
        std::string payload = stmt.values[1].value;

        table->insertRow(key, payload);
        std::cout << "[ОК] Строка успешно вставлена в таблицу '" << stmt.table_name << "'.\n";
    }

    // Метод для демонстрации поиска (SELECT по ключу)
    void executeSelectByKey(const std::string& table_name, int key) {
        ensureDatabaseSelected();
        Table* table = current_db->getTable(table_name);

        Row row;
        if (table->findByKey(key, row)) {
            std::cout << "[НАЙДЕНО] ID: " << row.id << ", Data: \"" << row.data << "\"\n";
        } else {
            std::cout << "[РЕЗУЛЬТАТ] Запись с ключом " << key << " не найдена.\n";
        }
    }    

    void executeSelect(const SelectStmt& stmt) {
        ensureDatabaseSelected();
        Table* table = current_db->getTable(stmt.table_name);
        
        std::cout << "[РЕЗУЛЬТАТЫ ВЫБОРКИ]:\n";
        bool found = false;

        // Если это строгое равенство по индексу — используем быстрый поиск
        if (stmt.op_type == TokenType::OP_EQUAL) {
            Row row;
            if (table->findByKey(stmt.search_key, row)) {
                std::cout << "  ID: " << row.id << ", Data: " << row.data << "\n";
                found = true;
            }
        } else {
            // Для остальных операторов (<, >, != и т.д.) проверяем диапазон ключей
            for (int i = 1; i <= 100; ++i) {
                Row row;
                if (table->findByKey(i, row)) {
                    bool match = false;
                    if (stmt.op_type == TokenType::OP_NOT_EQUAL && row.id != stmt.search_key) match = true;
                    else if (stmt.op_type == TokenType::OP_LESS && row.id < stmt.search_key) match = true;
                    else if (stmt.op_type == TokenType::OP_GREATER && row.id > stmt.search_key) match = true;
                    else if (stmt.op_type == TokenType::OP_LESS_EQ && row.id <= stmt.search_key) match = true;
                    else if (stmt.op_type == TokenType::OP_GREATER_EQ && row.id >= stmt.search_key) match = true;

                    if (match) {
                        std::cout << "  ID: " << row.id << ", Data: " << row.data << "\n";
                        found = true;
                    }
                }
            }
        }

        if (!found) {
            std::cout << "  Записи по заданному условию не найдены.\n";
        }
    }

    void executeDropTable(const DropTableStmt& stmt) {
        ensureDatabaseSelected();
        current_db->dropTable(stmt.table_name);
    }

    void executeUse(const UseDbStmt& stmt) {
        if (databases.find(stmt.db_name) == databases.end()) {
            databases.emplace(stmt.db_name, Database(stmt.db_name));
            std::cout << "[ОК] Создана и выбрана новая база данных: " << stmt.db_name << "\n";
        } else {
            std::cout << "[ОК] Активная база данных изменена на: " << stmt.db_name << "\n";
        }
        
        current_db = &databases.at(stmt.db_name);
    }
};