#pragma once

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <stdexcept>
#include "database.h"
#include "table.h"
#include "../parser/parser.h"

class QueryExecutor {
private:
    std::unordered_map<std::string, std::unique_ptr<Database>> databases;
    Database* current_db = nullptr;

    Database& requireDatabase(const std::string& name) {
        auto it = databases.find(name);
        if (it != databases.end()) return *it->second;
        if (!Database::existsOnDisk(name)) {
            throw std::runtime_error("Ошибка: база данных '" + name + "' не существует.");
        }
        auto db = std::make_unique<Database>(name, false);
        Database* ptr = db.get();
        databases[name] = std::move(db);
        return *ptr;
    }

    Database& currentDatabase() const {
        if (!current_db) {
            throw std::runtime_error("Ошибка: не выбрана активная база данных.");
        }
        return *current_db;
    }

    // "table" -> (текущая БД, "table");  "db.table" -> (БД db, "table")
    std::pair<Database*, std::string> resolveTable(const std::string& qualified) {
        size_t dot = qualified.find('.');
        if (dot == std::string::npos) return {&currentDatabase(), qualified};
        return {&requireDatabase(qualified.substr(0, dot)), qualified.substr(dot + 1)};
    }

    Table& getTable(const std::string& qualified) {
        auto [db, name] = resolveTable(qualified);
        return db->getTable(name);
    }

    static std::string jsonEscape(const std::string& s) {
        std::string out;
        for (unsigned char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out += static_cast<char>(c);
                    }
            }
        }
        return out;
    }

public:
    void executeCreateDatabase(const CreateDatabaseStmt& stmt) {
        if (databases.count(stmt.db_name) || Database::existsOnDisk(stmt.db_name)) {
            throw std::runtime_error("Ошибка: база данных '" + stmt.db_name + "' уже существует.");
        }
        databases[stmt.db_name] = std::make_unique<Database>(stmt.db_name, true);
        std::cout << "[ОК] База данных '" << stmt.db_name << "' создана.\n";
    }

    void executeDropDatabase(const DropDatabaseStmt& stmt) {
        bool in_memory = databases.count(stmt.db_name) > 0;
        if (!in_memory && !Database::existsOnDisk(stmt.db_name)) {
            throw std::runtime_error("Ошибка: база данных '" + stmt.db_name + "' не существует.");
        }

        if (current_db && current_db->getName() == stmt.db_name) {
            current_db = nullptr;
        }
        databases.erase(stmt.db_name);

        std::error_code ec;
        std::filesystem::remove_all(Database::pathFor(stmt.db_name), ec);
        if (ec) {
            throw std::runtime_error("Ошибка: не удалось удалить каталог базы данных: " + ec.message());
        }
        std::cout << "[ОК] База данных '" << stmt.db_name << "' удалена.\n";
    }

    void executeUse(const UseDbStmt& stmt) {
        current_db = &requireDatabase(stmt.db_name);
        std::cout << "[ОК] Активная база данных: " << stmt.db_name << "\n";
    }

    void executeCreateTable(const CreateTableStmt& stmt) {
        auto [db, name] = resolveTable(stmt.table_name);
        CreateTableStmt local = stmt;
        local.table_name = name;
        db->createTable(local);
        std::cout << "[ОК] Таблица '" << name << "' создана.\n";
    }

    void executeDropTable(const DropTableStmt& stmt) {
        auto [db, name] = resolveTable(stmt.table_name);
        db->dropTable(name);
        std::cout << "[ОК] Таблица '" << name << "' удалена.\n";
    }

    void executeInsert(const InsertStmt& stmt) {
        Table& table = getTable(stmt.table_name);
        size_t n = table.insert(stmt.columns, stmt.rows);
        std::cout << "[ОК] Вставлено строк: " << n << ".\n";
    }

    void executeDelete(const DeleteStmt& stmt) {
        Table& table = getTable(stmt.table_name);
        size_t n = table.remove(stmt.where);
        std::cout << "[ОК] Удалено строк: " << n << ".\n";
    }

    void executeUpdate(const UpdateStmt& stmt) {
        Table& table = getTable(stmt.table_name);
        size_t n = table.update(stmt.assignments, stmt.where);
        std::cout << "[ОК] Обновлено строк: " << n << ".\n";
    }

    void executeSelect(const SelectStmt& stmt) {
        Table& table = getTable(stmt.table_name);
        const auto& meta = table.getMeta();

        std::vector<size_t> cols;
        std::vector<std::string> names;
        if (stmt.select_all) {
            for (size_t i = 0; i < meta.columns.size(); ++i) {
                cols.push_back(i);
                names.push_back(meta.columns[i].name);
            }
        } else {
            for (size_t k = 0; k < stmt.columns.size(); ++k) {
                int idx = meta.columnIndex(stmt.columns[k]);
                if (idx < 0) {
                    throw std::runtime_error("Ошибка семантики: столбец '" + stmt.columns[k] + "' не найден в таблице '" + meta.table_name + "'.");
                }
                cols.push_back(static_cast<size_t>(idx));
                names.push_back(stmt.aliases[k].empty() ? stmt.columns[k] : stmt.aliases[k]);
            }
        }

        std::vector<Row> rows = table.select(stmt.has_where ? &stmt.where : nullptr);

        if (rows.empty()) {
            std::cout << "[]\n";
            return;
        }

        std::cout << "[\n";
        for (size_t r = 0; r < rows.size(); ++r) {
            std::cout << "  {\n";
            for (size_t k = 0; k < cols.size(); ++k) {
                const Field& val = rows[r][cols[k]];
                std::cout << "    \"" << jsonEscape(names[k]) << "\": ";
                if (!val) {
                    std::cout << "null";
                } else if (meta.columns[cols[k]].type == DataType::STRING) {
                    std::cout << "\"" << jsonEscape(*val) << "\"";
                } else {
                    std::cout << *val;
                }
                std::cout << (k + 1 < cols.size() ? "," : "") << "\n";
            }
            std::cout << "  }" << (r + 1 < rows.size() ? "," : "") << "\n";
        }
        std::cout << "]\n";
    }
};
