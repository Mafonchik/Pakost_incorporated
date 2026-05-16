#pragma once

#include <fstream>
#include <string>
#include <cstdint> // Для типа uint64_t

// Базовая структура для тестирования (представляет одну строку таблицы)
struct Row {
    int id;
    std::string data;
};

class TableFileManager {
private:
    std::fstream file;
    std::string filename;

public:
    // Конструктор и деструктор
    TableFileManager(const std::string& name);
    ~TableFileManager();

    // Запись новой строки. Возвращает смещение (FileOffset), куда была записана строка.
    uint64_t appendRow(const Row& row);

    // Чтение строки по заранее известному смещению
    Row readRow(uint64_t offset);
};