#include "database.h"
#include <stdexcept>
#include <iostream>

Database::Database(std::string name) : db_name(std::move(name)) {}

void Database::createTable(const CreateTableStmt& stmt) {
    if (tables.find(stmt.table_name) != tables.end()) {
        throw std::runtime_error("Ошибка: Таблица '" + stmt.table_name + "' уже существует в базе данных '" + db_name + "'.");
    }

    // Создаем новую таблицу и сохраняем в мапу
    tables[stmt.table_name] = std::make_unique<Table>(stmt);
    std::cout << "[ОК] Таблица '" << stmt.table_name << "' успешно создана.\n";
}

void Database::dropTable(const std::string& table_name) {
    // 1. Проверяем, существует ли такая таблица в памяти
    if (tables.find(table_name) == tables.end()) {
        throw std::runtime_error("Ошибка: таблица '" + table_name + "' не существует.");
    }

    // 2. Удаляем бинарный файл таблицы с диска (например, "users.db")
    std::string filename = table_name + ".db";
    if (std::remove(filename.c_str()) != 0) {
        std::cout << "[ПРЕДУПРЕЖДЕНИЕ] Не удалось удалить файл диска: " << filename << "\n";
    }

    // 3. Удаляем объект таблицы из мапы
    tables.erase(table_name);
    std::cout << "[ОК] Таблица '" << table_name << "' успешно удалена.\n";
}

Table* Database::getTable(const std::string& name) {
    auto it = tables.find(name);
    if (it == tables.end()) {
        throw std::runtime_error("Ошибка: Таблица '" + name + "' не найдена в текущей базе данных.");
    }
    return it->second.get();
}

std::string Database::getName() const {
    return db_name;
}