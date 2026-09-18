#pragma once

#include <algorithm>
#include <filesystem> // Обязательно для удаления папок
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>
#include <iterator>
#include <stdexcept>
#include "database.h"
#include "table.h"
#include "../parser/parser.h"

class QueryExecutor {
private:
    std::unordered_map<std::string, Database> databases;
    Database* current_db = nullptr;

    void ensureDatabaseSelected() const {
        if (!current_db) {
            throw std::runtime_error("Ошибка: не выбрана активная база данных. Сначала выполните команду USE database_name;");
        }
    }

public:
    void executeCreateDatabase(const CreateDatabaseStmt& stmt) {
        if (databases.find(stmt.db_name) != databases.end()) {
            throw std::runtime_error("Ошибка: база данных '" + stmt.db_name + "' уже существует.");
        }
        databases.emplace(stmt.db_name, Database(stmt.db_name));
        current_db = &databases.at(stmt.db_name);
        std::cout << "[ОК] База данных '" << stmt.db_name << "' создана и выбрана.\n";
    }

    void executeDropDatabase(const DropDatabaseStmt& stmt) {
        auto it = databases.find(stmt.db_name);
        
        // Проверяем наличие в памяти ИЛИ физически на диске
        std::string db_path = "./data/" + stmt.db_name;
        bool exists_on_disk = std::filesystem::exists(db_path);

        if (it == databases.end() && !exists_on_disk) {
            throw std::runtime_error("Ошибка: база данных '" + stmt.db_name + "' не существует.");
        }

        // 1. Удаляем из оперативной памяти, если она там была
        if (it != databases.end()) {
            databases.erase(stmt.db_name);
        }

        // 2. ФИЗИЧЕСКИ удаляем папку базы данных со всем содержимым (.db и .idx файлы)
        if (exists_on_disk) {
            std::filesystem::remove_all(db_path);
        }

        // 3. Сбрасываем указатель, если удалили текущую активную базу
        if (current_db && current_db->getName() == stmt.db_name) {
            current_db = nullptr;
        }

        std::cout << "[ОК] База данных '" << stmt.db_name << "' успешно удалена.\n";
    }

    void executeCreateTable(const CreateTableStmt& stmt) {
        ensureDatabaseSelected();
        current_db->createTable(stmt);
    }

    void executeInsert(const InsertStmt& stmt) {
        ensureDatabaseSelected();
        Table* table = current_db->getTable(stmt.table_name);
        
        for (const auto& parsed_row : stmt.rows) {
            Row row;
            row.reserve(parsed_row.size());
            for (const auto& val : parsed_row) {
                row.push_back(val.value);
            }
            table->insertRow(row);
        }
        std::cout << "[ОК] Строки успешно вставлены в таблицу '" << stmt.table_name << "'.\n";
    }


    void executeDelete(const DeleteStmt& stmt) {
        ensureDatabaseSelected();
        Table* table = current_db->getTable(stmt.table_name);
        if (stmt.has_where) {
            table->deleteRow(stmt.where.column, stmt.where.value.value);
            std::cout << "[ОК] Удаление выполнено.\n";
        } else {
            std::cout << "[ОШИБКА] DELETE без WHERE не поддерживается для безопасности.\n";
        }
    }

    void executeUpdate(const UpdateStmt& stmt) {
        ensureDatabaseSelected();
        Table* table = current_db->getTable(stmt.table_name);
        if (stmt.has_where && !stmt.assignments.empty()) {
            table->updateRow(stmt.where.column, stmt.where.value.value, stmt.assignments);
            std::cout << "[ОК] Обновление выполнено.\n";
        } else {
            std::cout << "[ОШИБКА] UPDATE требует наличия условий WHERE и инструкций SET.\n";
        }
    }

    void executeDropTable(const DropTableStmt& stmt) {
        ensureDatabaseSelected();
        current_db->dropTable(stmt.table_name);
    }

    void executeUse(const UseDbStmt& stmt) {
        auto it = databases.find(stmt.db_name);
        if (it == databases.end()) {
            // Если базы нет в памяти, но она физически есть на диске — можно подгрузить, 
            // либо выдать ошибку. Для простоты пока оставим создание/переключение:
            databases.emplace(stmt.db_name, Database(stmt.db_name));
            std::cout << "[ОК] База данных '" << stmt.db_name << "' создана и выбрана.\n";
        } else {
            std::cout << "[ОК] Активная база данных изменена на: " << stmt.db_name << "\n";
        }
        current_db = &databases.at(stmt.db_name);
    }

    void executeSelect(const SelectStmt& stmt) {
        ensureDatabaseSelected();
        Table* table = current_db->getTable(stmt.table_name);
        
        std::vector<Row> results;

        if (stmt.has_where) {
            // Используем универсальный метод фильтрации по условию
            results = table->selectByCondition(stmt.where);
        } else {
            // Полный скан всех строк таблицы без условий
            const auto& meta = table->getMeta();
            if (!meta.columns.empty()) {
                results = table->sequentialScan(meta.columns[0].name, "");
            }
        }

        const auto& meta = table->getMeta();

        // Вывод результатов в формате JSON
        std::cout << "[\n";
        for (size_t i = 0; i < results.size(); ++i) {
            std::cout << "  {\n";
            const auto& row = results[i];

            for (size_t j = 0; j < meta.columns.size() && j < row.size(); ++j) {
                const auto& col = meta.columns[j];
                const auto& val = row[j];

                std::cout << "    \"" << col.name << "\": ";

                bool is_str = (col.type == DataType::STRING);
                
                if (is_str) {
                    std::cout << "\"" << val << "\"";
                } else {
                    std::cout << (val.empty() ? "null" : val);
                }

                if (j + 1 < meta.columns.size() && j + 1 < row.size()) {
                    std::cout << ",";
                }
                std::cout << "\n";
            }

            std::cout << "  }" << (i + 1 < results.size() ? "," : "") << "\n";
        }
        std::cout << "]\n";
    }
};