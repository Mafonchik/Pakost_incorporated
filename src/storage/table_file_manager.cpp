#include "table_file_manager.h"
#include <iostream>

TableFileManager::TableFileManager(const std::string& filepath) : filename(filepath) {
    // Открываем файл для чтения и бинарной записи, создаем если нет
    file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        file.open(filename, std::ios::out | std::ios::binary);
        file.close();
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    }
}

TableFileManager::~TableFileManager() {
    if (file.is_open()) {
        file.close();
    }
}

uint64_t TableFileManager::appendRow(const Row& row) {
    file.seekp(0, std::ios::end);
    uint64_t offset = file.tellp();

    // Записываем ID (int)
    file.write(reinterpret_cast<const char*>(&row.id), sizeof(row.id));

    // Записываем длину строки и саму строку data
    size_t data_len = row.data.size();
    file.write(reinterpret_cast<const char*>(&data_len), sizeof(data_len));
    file.write(row.data.c_str(), data_len);

    file.flush();
    return offset;
}

Row TableFileManager::readRow(uint64_t offset) {
    file.seekg(offset, std::ios::beg);

    Row row;
    // Читаем ID
    file.read(reinterpret_cast<char*>(&row.id), sizeof(row.id));

    // Читаем длину строки и саму строку
    size_t data_len = 0;
    file.read(reinterpret_cast<char*>(&data_len), sizeof(data_len));

    row.data.resize(data_len);
    file.read(&row.data[0], data_len);

    return row;
}