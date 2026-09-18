#include "database.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

Database::Database(std::string name) : db_name(std::move(name)) {
    root_dir = "./data/" + db_name;
    std::filesystem::create_directories(root_dir);
}

void Database::createTable(const CreateTableStmt& stmt) {
    // Используем .db единообразно для всей СУБД
    std::string file_path = root_dir + "/" + stmt.table_name + ".db"; 
    
    // Проверяем наличие таблицы в памяти ИЛИ на диске
    if (tables.find(stmt.table_name) != tables.end() || std::filesystem::exists(file_path)) {
        throw std::runtime_error("Ошибка: Таблица '" + stmt.table_name + "' уже существует в базе данных '" + db_name + "'.");
    }
    
    tables[stmt.table_name] = std::make_unique<Table>(stmt.table_name, stmt, root_dir);
    std::cout << "[ОК] Таблица '" << stmt.table_name << "' успешно создана.\n";
}

void Database::dropTable(const std::string& table_name) {
    std::string data_file_path = root_dir + "/" + table_name + ".db";
    bool in_memory = (tables.find(table_name) != tables.end());
    bool on_disk = std::filesystem::exists(data_file_path);

    // Если таблицы нет ни в ОЗУ, ни на диске — выдаем ошибку
    if (!in_memory && !on_disk) {
        throw std::runtime_error("Ошибка: Таблица '" + table_name + "' не существует.");
    }

    // 1. Удаляем основной файл таблицы (.db)
    if (on_disk) {
        std::filesystem::remove(data_file_path);
    }

    // 2. Удаляем все возможные файлы индексов для этой таблицы (*.idx)
    // Сканируем директорию базы на наличие файлов вида "tablename_*.idx"
    for (const auto& entry : std::filesystem::directory_iterator(root_dir)) {
        std::string filename = entry.path().filename().string();
        if (filename.rfind(table_name + "_", 0) == 0 && entry.path().extension() == ".idx") {
            std::filesystem::remove(entry.path());
        }
    }

    // 3. Удаляем из оперативной памяти
    if (in_memory) {
        tables.erase(table_name);
    }

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

const std::string& Database::getRootDir() const {
    return root_dir;
}