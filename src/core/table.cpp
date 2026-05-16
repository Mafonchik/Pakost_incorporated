#include "table.h"

// Конструктор: инициализирует имя, схему, файловый менеджер и B+-дерево
Table::Table(const CreateTableStmt& table_schema) 
    : schema(table_schema), name(table_schema.table_name) {
    
    // Файл данных для конкретной таблицы (например, users.db)
    file_manager = std::make_unique<TableFileManager>(name + ".db");
    
    // Создаем B+-дерево с минимальной степенью t = 3 для индексации
    int_index = std::make_unique<BPlusTree<int>>(3);
}

// Вставка новой строки в таблицу
void Table::insertRow(int key, const std::string& data_payload) {
    Row row = {key, data_payload};

    // 1. Физическая запись строки на диск, получаем смещение (offset)
    uint64_t offset = file_manager->appendRow(row);

    // 2. Добавление пары ключ-смещение в индекс B+-дерева
    int_index->insert(key, offset);
}

// Поиск строки по ключу
bool Table::findByKey(int key, Row& out_row) {
    // 1. Ищем смещение на диске через B+-дерево
    uint64_t offset = int_index->search(key);
    
    // Если смещение равно 0, значит ключ не найден
    if (offset == 0) {
        return false;
    }

    // 2. Читаем данные с диска по найденному смещению
    out_row = file_manager->readRow(offset);
    return true;
}

// Геттер для получения имени таблицы
std::string Table::getName() const {
    return name;
}