#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "../parser/parser.h"
#include "../index/index_manager.h"

// Предварительное объявление для избежания ошибок компиляции
struct CreateTableStmt; 

// Структура типа данных колонки
enum class DataType {
    INT,
    STRING
};

// Описание колонки таблицы
struct ColumnDefinition {
    std::string name;
    DataType type;
    bool is_indexed;
};

// Схема таблицы
struct TableMeta {
    std::string table_name;
    std::vector<ColumnDefinition> columns;
};

// Представление строки таблицы в памяти (всё хранится как строки до обработки)
using Row = std::vector<std::string>;

class Table {
private:
    std::string table_name_;
    std::string filename_;
    TableMeta meta_;

    // Единое хранилище индексов: ключ — имя колонки, значение — менеджер
    std::unordered_map<std::string, std::unique_ptr<dbms::IndexManager>> indexes_;
    
    // Внутренние вспомогательные методы для работы с файлами/памятью
    FileOffset writeDataToFile(const Row& row);
    Row readRowFromfile(FileOffset offset) const;
    TableMeta loadMeta(const std::string& name, const std::string& db_root_dir);

    // Метод загрузки/инициализации индексов при старте (в нём будем делать load)
    void warmUpIndexes(const std::string& db_root_dir);

public:
    Table(const std::string& name, const std::string& db_root_dir);
    Table(const std::string& name, const CreateTableStmt& stmt, const std::string& db_root_dir);
    explicit Table(const std::string& name);
    ~Table(); // Реализуем в .cpp, чтобы корректно вызывать save() для индексов

    // Основные операции СУБД с поддержкой индексов
    void insertRow(const Row& values);
    void deleteRow(const std::string& column_name, const std::string& target_value);
    void updateRow(const std::string& search_column, const std::string& target_value, const std::vector<std::pair<std::string, ParsedValue>>& assignments);
    
    // Поиск по равенству (использует IndexManager, если колонка индексирована)
    std::vector<Row> selectByEquality(const std::string& column_name, const std::string& target_value);
    
    // Диапазонный поиск (использует range_search из B+ дерева)
    std::vector<Row> selectByRange(const std::string& column_name, const std::string& start_val, const std::string& end_val);

    // Полное сканирование (fallback, если индекс отсутствует)
    std::vector<Row> sequentialScan(const std::string& column_name, const std::string& target_value) const;
    std::vector<Row> selectByCondition(const Condition& cond) const;
    const TableMeta& getMeta() const;
};