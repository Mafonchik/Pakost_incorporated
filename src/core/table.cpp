#include "table.h"
#include "../parser/parser.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <filesystem>

// 1. Конструктор для открытия существующей таблицы
// ТЕПЕРЬ ОН ПРИНИМАЕТ db_root_dir
Table::Table(const std::string& name, const std::string& db_root_dir) 
    : table_name_(name), filename_(db_root_dir + "/" + name + ".db") {
    
    // Передаем путь и в загрузку схемы
    meta_ = loadMeta(name, db_root_dir);
    
    // Загружаем индексы, передавая путь к папке БД
    warmUpIndexes(db_root_dir);
}

// 2. Конструктор для создания новой таблицы из CREATE TABLE
Table::Table(const std::string& name, const CreateTableStmt& stmt, const std::string& db_root_dir) 
    : table_name_(name), filename_(db_root_dir + "/" + name + ".db") {
    
    meta_.table_name = name;
    
    for (const auto& parsed_col : stmt.columns) {
        ColumnDefinition col;
        col.name = parsed_col.name;
        col.type = (parsed_col.type == TokenType::TYPE_INT) ? DataType::INT : DataType::STRING;
        col.is_indexed = parsed_col.is_indexed;
        meta_.columns.push_back(col);
    }

    // Передаем db_root_dir в функцию инициализации индексов
    warmUpIndexes(db_root_dir);
}

Table::~Table() {
    // При закрытии таблицы сохраняем все индексы на диск
    for (auto& [col_name, index] : indexes_) {
        index->save();
    }
}

// Инициализация индексов
void Table::warmUpIndexes(const std::string& db_root_dir) {
    for (const auto& col : meta_.columns) {
        if (col.is_indexed) {
            // Формируем правильный путь внутри папки базы данных
            std::string idx_filename = db_root_dir + "/" + table_name_ + "_" + col.name + ".idx";
            bool is_int = (col.type == DataType::INT);
            
            indexes_[col.name] = std::make_unique<dbms::IndexManager>(idx_filename, is_int);
            indexes_[col.name]->load();
        }
    }
}

// Вставка данных (INSERT)
void Table::insertRow(const Row& values) {
    if (values.size() != meta_.columns.size()) {
        throw std::invalid_argument("Insert error: Column count mismatch.");
    }

    // 1. Проверяем на дубликаты ДО физической записи
    for (size_t i = 0; i < meta_.columns.size(); ++i) {
        const auto& col = meta_.columns[i];
        if (col.is_indexed && indexes_.count(col.name)) {
            FileOffset dummy_offset;
            bool exists = false;
            
            if (col.type == DataType::INT) {
                exists = indexes_[col.name]->find(std::stoll(values[i]), dummy_offset);
            } else {
                exists = indexes_[col.name]->find(values[i], dummy_offset);
            }

            if (exists) {
                throw std::runtime_error("Unique constraint violation on column: " + col.name);
            }
        }
    }

    // 2. Физическая запись на диск
    FileOffset new_offset = writeDataToFile(values);

    // 3. Добавляем ключи в индексы
    for (size_t i = 0; i < meta_.columns.size(); ++i) {
        const auto& col = meta_.columns[i];
        if (col.is_indexed && indexes_.count(col.name)) {
            if (col.type == DataType::INT) {
                indexes_[col.name]->insert(std::stoll(values[i]), new_offset);
            } else {
                indexes_[col.name]->insert(values[i], new_offset);
            }
        }
    }
}

// Физическая работа с файлом (.db)
FileOffset Table::writeDataToFile(const Row& row) {
    std::ofstream file(filename_, std::ios::out | std::ios::binary | std::ios::app);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open data file for writing: " + filename_);
    }

    FileOffset offset = file.tellp();

    uint32_t col_count = static_cast<uint32_t>(row.size());
    file.write(reinterpret_cast<const char*>(&col_count), sizeof(col_count));

    for (const auto& val : row) {
        uint32_t len = static_cast<uint32_t>(val.size());
        file.write(reinterpret_cast<const char*>(&len), sizeof(len));
        file.write(val.data(), len);
    }

    file.close();
    return offset;
}

Row Table::readRowFromfile(FileOffset offset) const {
    std::ifstream file(filename_, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open data file for reading: " + filename_);
    }

    file.seekg(offset);
    if (!file) {
        throw std::runtime_error("Failed to seek to offset in data file.");
    }

    uint32_t col_count = 0;
    file.read(reinterpret_cast<char*>(&col_count), sizeof(col_count));

    Row row;
    row.resize(col_count);

    for (uint32_t i = 0; i < col_count; ++i) {
        uint32_t len = 0;
        file.read(reinterpret_cast<char*>(&len), sizeof(len));

        std::string val(len, '\0');
        file.read(&val[0], len);
        row[i] = val;
    }

    file.close();
    return row;
}

// Выборка данных (SELECT)
std::vector<Row> Table::selectByEquality(const std::string& column_name, const std::string& target_value) {
    std::vector<Row> result;

    if (indexes_.count(column_name)) {
        FileOffset offset = 0;
        bool found = false;

        DataType col_type = DataType::STRING;
        for (const auto& col : meta_.columns) {
            if (col.name == column_name) {
                col_type = col.type;
                break;
            }
        }

        if (col_type == DataType::INT) {
            found = indexes_[column_name]->find(std::stoll(target_value), offset);
        } else {
            found = indexes_[column_name]->find(target_value, offset);
        }

        if (found) {
            result.push_back(readRowFromfile(offset));
        }
        return result;
    }

    return sequentialScan(column_name, target_value);
}

std::vector<Row> Table::selectByRange(const std::string& column_name, const std::string& start_val, const std::string& end_val) {
    if (indexes_.find(column_name) == indexes_.end()) {
        throw std::runtime_error("Range search is only supported on indexed columns.");
    }

    DataType col_type = DataType::STRING;
    for (const auto& col : meta_.columns) {
        if (col.name == column_name) {
            col_type = col.type;
            break;
        }
    }

    std::vector<FileOffset> offsets;
    if (col_type == DataType::INT) {
        offsets = indexes_[column_name]->rangeSearch(std::stoll(start_val), std::stoll(end_val));
    } else {
        offsets = indexes_[column_name]->rangeSearch(start_val, end_val);
    }

    std::vector<Row> result;
    result.reserve(offsets.size()); 
    for (FileOffset off : offsets) {
        result.push_back(readRowFromfile(off));
    }

    return result;
}

// Полное сканирование (Full Table Scan)
std::vector<Row> Table::sequentialScan(const std::string& column_name, const std::string& target_value) const {
    std::vector<Row> result;
    
    int search_idx = -1;
    DataType search_type = DataType::STRING;
    for (size_t i = 0; i < meta_.columns.size(); ++i) {
        if (meta_.columns[i].name == column_name) {
            search_idx = i;
            search_type = meta_.columns[i].type;
            break;
        }
    }
    
    if (search_idx == -1) {
        throw std::invalid_argument("Column not found in meta.");
    }

    std::ifstream file(filename_, std::ios::in | std::ios::binary);
    if (!file.is_open()) return result;

    long long target_int = 0;
    if (search_type == DataType::INT && !target_value.empty()) {
        target_int = std::stoll(target_value);
    }

    while (file.peek() != EOF) {
        uint32_t col_count = 0;
        if (!file.read(reinterpret_cast<char*>(&col_count), sizeof(col_count))) {
            break; 
        }

        Row row(col_count);
        for (uint32_t i = 0; i < col_count; ++i) {
            uint32_t len = 0;
            file.read(reinterpret_cast<char*>(&len), sizeof(len));
            std::string val(len, '\0');
            file.read(&val[0], len);
            row[i] = val;
        }

        if (target_value.empty()) {
            result.push_back(row);
        } else if (search_type == DataType::INT) {
            if (std::stoll(row[search_idx]) == target_int) {
                result.push_back(row);
            }
        } else {
            if (row[search_idx] == target_value) {
                result.push_back(row);
            }
        }
    }

    return result;
}

const TableMeta& Table::getMeta() const {
    return meta_;
}

// Теперь функция загрузки схемы тоже принимает путь
TableMeta Table::loadMeta(const std::string& name, const std::string& db_root_dir) {
    // Временная заглушка (если схема пока не сериализуется отдельно)
    TableMeta meta;
    meta.table_name = name;
    return meta;
}

#include <regex>
#include <algorithm>

// Преобразование SQL LIKE шаблона (% и _) в std::regex
static std::regex sqlLikeToRegex(const std::string& pattern) {
    std::string regex_str = "^";
    for (char c : pattern) {
        if (c == '%') {
            regex_str += ".*";
        } else if (c == '_') {
            regex_str += ".";
        } else {
            // Экранируем спецсимволы регулярных выражений
            if (std::string(".+?^$()[]{}|\\").find(c) != std::string::npos) {
                regex_str += '\\';
            }
            regex_str += c;
        }
    }
    regex_str += "$";
    return std::regex(regex_str, std::regex::icase);
}

// Вспомогательная функция проверки удовлетворяет ли строка условию
static bool matchCondition(const Row& row, const TableMeta& meta, const Condition& cond) {
    // Находим индекс колонки в схеме
    int col_idx = -1;
    DataType col_type = DataType::STRING;
    for (size_t i = 0; i < meta.columns.size(); ++i) {
        if (meta.columns[i].name == cond.column) {
            col_idx = static_cast<int>(i);
            col_type = meta.columns[i].type;
            break;
        }
    }

    if (col_idx == -1 || static_cast<size_t>(col_idx) >= row.size()) {
        throw std::runtime_error("Column '" + cond.column + "' not found in table schema.");
    }

    const std::string& val_str = row[col_idx];

    // Обработка BETWEEN
    if (cond.is_between) {
        if (col_type == DataType::INT) {
            long long val = std::stoll(val_str);
            long long low = std::stoll(cond.lower.value);
            long long high = std::stoll(cond.upper.value);
            return val >= low && val <= high;
        } else {
            return val_str >= cond.lower.value && val_str <= cond.upper.value;
        }
    }

    // Обработка LIKE
    if (cond.op == "LIKE" || cond.op == "like") {
        std::regex rx = sqlLikeToRegex(cond.value.value);
        return std::regex_match(val_str, rx);
    }

    // Обработка операторов сравнения (=, !=, <, >, <=, >=)
    if (col_type == DataType::INT) {
        long long row_val = std::stoll(val_str);
        long long cond_val = std::stoll(cond.value.value);

        if (cond.op == "=")  return row_val == cond_val;
        if (cond.op == "!=") return row_val != cond_val;
        if (cond.op == "<")  return row_val < cond_val;
        if (cond.op == ">")  return row_val > cond_val;
        if (cond.op == "<=") return row_val <= cond_val;
        if (cond.op == ">=") return row_val >= cond_val;
    } else {
        if (cond.op == "=")  return val_str == cond.value.value;
        if (cond.op == "!=") return val_str != cond.value.value;
        if (cond.op == "<")  return val_str < cond.value.value;
        if (cond.op == ">")  return val_str > cond.value.value;
        if (cond.op == "<=") return val_str <= cond.value.value;
        if (cond.op == ">=") return val_str >= cond.value.value;
    }

    return false;
}

// Новый универсальный метод выполнения SELECT с условной фильтрацией
std::vector<Row> Table::selectByCondition(const Condition& cond) const {
    std::vector<Row> result;

    std::ifstream file(filename_, std::ios::in | std::ios::binary);
    if (!file.is_open()) return result;

    while (file.peek() != EOF) {
        uint32_t col_count = 0;
        if (!file.read(reinterpret_cast<char*>(&col_count), sizeof(col_count))) {
            break;
        }

        Row row(col_count);
        for (uint32_t i = 0; i < col_count; ++i) {
            uint32_t len = 0;
            file.read(reinterpret_cast<char*>(&len), sizeof(len));
            std::string val(len, '\0');
            file.read(&val[0], len);
            row[i] = val;
        }

        if (matchCondition(row, meta_, cond)) {
            result.push_back(row);
        }
    }

    return result;
}

// Удаление данных (DELETE) с поддержкой последовательного сканирования
void Table::deleteRow(const std::string& column_name, const std::string& target_value) {
    // 1. Находим индекс целевой колонки и ее тип в схеме
    int search_idx = -1;
    DataType search_type = DataType::STRING;
    for (size_t i = 0; i < meta_.columns.size(); ++i) {
        if (meta_.columns[i].name == column_name) {
            search_idx = static_cast<int>(i);
            search_type = meta_.columns[i].type;
            break;
        }
    }

    if (search_idx == -1) {
        throw std::invalid_argument("Column '" + column_name + "' not found in table schema.");
    }

    std::ifstream file(filename_, std::ios::in | std::ios::binary);
    if (!file.is_open()) return;

    std::string temp_filename = filename_ + ".tmp";
    std::ofstream temp_file(temp_filename, std::ios::out | std::ios::binary);
    if (!temp_file.is_open()) {
        file.close();
        throw std::runtime_error("Failed to create temporary file for delete operation.");
    }

    // Переинициализируем индексы для пересчета смещений
    std::string db_dir = std::filesystem::path(filename_).parent_path().string();
    for (const auto& col : meta_.columns) {
        if (col.is_indexed) {
            std::string idx_filename = db_dir + "/" + table_name_ + "_" + col.name + ".idx";
            bool is_int = (col.type == DataType::INT);
            indexes_[col.name] = std::make_unique<dbms::IndexManager>(idx_filename, is_int);
        }
    }

    long long target_int = 0;
    if (search_type == DataType::INT && !target_value.empty()) {
        target_int = std::stoll(target_value);
    }

    // 2. Полное сканирование (Sequential Scan)
    while (file.peek() != EOF) {
        uint32_t col_count = 0;
        if (!file.read(reinterpret_cast<char*>(&col_count), sizeof(col_count))) break;

        Row row(col_count);
        for (uint32_t i = 0; i < col_count; ++i) {
            uint32_t len = 0;
            file.read(reinterpret_cast<char*>(&len), sizeof(len));
            std::string val(len, '\0');
            file.read(&val[0], len);
            row[i] = val;
        }

        // Проверяем совпадение условий
        bool is_match = false;
        if (search_type == DataType::INT) {
            if (!row[search_idx].empty() && std::stoll(row[search_idx]) == target_int) {
                is_match = true;
            }
        } else {
            if (row[search_idx] == target_value) {
                is_match = true;
            }
        }

        // Если строка совпала — пропускаем ее (удаляем)
        if (is_match) {
            continue;
        }

        // Если не совпала — сохраняем во временный файл и обновляем индексы
        FileOffset new_offset = temp_file.tellp();
        temp_file.write(reinterpret_cast<const char*>(&col_count), sizeof(col_count));
        for (const auto& val : row) {
            uint32_t len = static_cast<uint32_t>(val.size());
            temp_file.write(reinterpret_cast<const char*>(&len), sizeof(len));
            temp_file.write(val.data(), len);
        }

        for (size_t i = 0; i < meta_.columns.size() && i < row.size(); ++i) {
            const auto& col = meta_.columns[i];
            if (col.is_indexed && indexes_.count(col.name)) {
                if (col.type == DataType::INT) {
                    indexes_[col.name]->insert(std::stoll(row[i]), new_offset);
                } else {
                    indexes_[col.name]->insert(row[i], new_offset);
                }
            }
        }
    }

    file.close();
    temp_file.close();

    // 3. Заменяем старый файл таблицы обновленным
    std::filesystem::rename(temp_filename, filename_);

    // Сохраняем актуальные индексы
    for (auto& [col_name, index] : indexes_) {
        index->save();
    }
}

// Обновление данных (UPDATE) с поддержкой последовательного сканирования
void Table::updateRow(const std::string& search_column, const std::string& target_value, const std::vector<std::pair<std::string, ParsedValue>>& assignments) {
    int search_idx = -1;
    DataType search_type = DataType::STRING;
    for (size_t i = 0; i < meta_.columns.size(); ++i) {
        if (meta_.columns[i].name == search_column) {
            search_idx = static_cast<int>(i);
            search_type = meta_.columns[i].type;
            break;
        }
    }

    if (search_idx == -1) {
        throw std::invalid_argument("Column '" + search_column + "' not found in table schema.");
    }

    std::ifstream file(filename_, std::ios::in | std::ios::binary);
    if (!file.is_open()) return;

    std::string temp_filename = filename_ + ".tmp";
    std::ofstream temp_file(temp_filename, std::ios::out | std::ios::binary);
    if (!temp_file.is_open()) {
        file.close();
        throw std::runtime_error("Failed to create temporary file for update operation.");
    }

    std::string db_dir = std::filesystem::path(filename_).parent_path().string();
    for (const auto& col : meta_.columns) {
        if (col.is_indexed) {
            std::string idx_filename = db_dir + "/" + table_name_ + "_" + col.name + ".idx";
            bool is_int = (col.type == DataType::INT);
            indexes_[col.name] = std::make_unique<dbms::IndexManager>(idx_filename, is_int);
        }
    }

    long long target_int = 0;
    if (search_type == DataType::INT && !target_value.empty()) {
        target_int = std::stoll(target_value);
    }

    while (file.peek() != EOF) {
        uint32_t col_count = 0;
        if (!file.read(reinterpret_cast<char*>(&col_count), sizeof(col_count))) break;

        Row row(col_count);
        for (uint32_t i = 0; i < col_count; ++i) {
            uint32_t len = 0;
            file.read(reinterpret_cast<char*>(&len), sizeof(len));
            std::string val(len, '\0');
            file.read(&val[0], len);
            row[i] = val;
        }

        bool is_match = false;
        if (search_type == DataType::INT) {
            if (!row[search_idx].empty() && std::stoll(row[search_idx]) == target_int) {
                is_match = true;
            }
        } else {
            if (row[search_idx] == target_value) {
                is_match = true;
            }
        }

        // Если совпало — обновляем значения полей в строке
        if (is_match) {
            for (const auto& assignment : assignments) {
                const std::string& col_name = assignment.first;
                const std::string& new_val_str = assignment.second.value;

                for (size_t c = 0; c < meta_.columns.size(); ++c) {
                    if (meta_.columns[c].name == col_name) {
                        row[c] = new_val_str;
                        break;
                    }
                }
            }
        }

        // Записываем строку (измененную или исходную) и обновляем индексы
        FileOffset new_offset = temp_file.tellp();
        temp_file.write(reinterpret_cast<const char*>(&col_count), sizeof(col_count));
        for (const auto& val : row) {
            uint32_t len = static_cast<uint32_t>(val.size());
            temp_file.write(reinterpret_cast<const char*>(&len), sizeof(len));
            temp_file.write(val.data(), len);
        }

        for (size_t i = 0; i < meta_.columns.size() && i < row.size(); ++i) {
            const auto& col = meta_.columns[i];
            if (col.is_indexed && indexes_.count(col.name)) {
                if (col.type == DataType::INT) {
                    indexes_[col.name]->insert(std::stoll(row[i]), new_offset);
                } else {
                    indexes_[col.name]->insert(row[i], new_offset);
                }
            }
        }
    }

    file.close();
    temp_file.close();

    std::filesystem::rename(temp_filename, filename_);

    for (auto& [col_name, index] : indexes_) {
        index->save();
    }
}