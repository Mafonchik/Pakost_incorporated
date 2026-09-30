#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include "row.h"

// Слой физического хранения таблицы. Единственное место, которое знает формат файла данных (.db):
//
//   файл  := строка*
//   строка := [uint32 число полей] { поле }*
//   поле   := [uint8 флаг NULL: 1 = NULL] и, если не NULL, [uint32 длина][байты значения]
//
// Смещение строки (FileOffset в индексе) — позиция её первого байта в файле.
class TableFileManager {
public:
    // Чтение строк: последовательное (next) или по смещению из индекса (readAt)
    class Reader {
        std::ifstream in_;
    public:
        explicit Reader(const std::string& path) : in_(path, std::ios::binary) {}
        bool isOpen() const { return in_.is_open(); }
        void close() { in_.close(); }
        // false — смещение неверно или данные повреждены
        bool readAt(uint64_t offset, Row& row);
        // false — конец файла (или повреждённая/оборванная запись)
        bool next(uint64_t& offset, Row& row);
        // Позиция сразу после последней успешно прочитанной строки
        uint64_t position() { return static_cast<uint64_t>(in_.tellg()); }
    };

    // Переписывание файла: строки пишутся во временный файл <path>.tmp,
    // commit() атомарно подменяет им исходный файл. Если commit() не вызван
    // (в том числе при исключении) — временный файл удаляется деструктором.
    class Writer {
        std::string target_;
        std::string tmp_;
        std::ofstream out_;
        uint64_t offset_ = 0;
        bool committed_ = false;
    public:
        explicit Writer(const std::string& target_path);
        ~Writer();
        Writer(const Writer&) = delete;
        Writer& operator=(const Writer&) = delete;

        // Записывает строку и возвращает её смещение в НОВОМ файле
        uint64_t write(const Row& row);
        void commit();
    };

    explicit TableFileManager(std::string path) : path_(std::move(path)) {}

    const std::string& path() const { return path_; }
    std::string tmpPath() const { return path_ + ".tmp"; }

    bool exists() const;
    uint64_t size() const;                 // 0, если файла нет
    void createEmpty() const;              // создать/обнулить файл
    void removeFiles() const noexcept;     // удалить файл данных и временный файл

    // Восстановление после сбоя: если в конце файла осталась оборванная запись
    // (процесс упал посреди записи), отсекает её. true — хвост был отброшен.
    bool truncateToValidPrefix() const;

    // Дописывает строки в конец файла одной операцией записи.
    // Возвращает смещение каждой строки. При ошибке ввода-вывода бросает std::runtime_error.
    std::vector<uint64_t> appendRows(const std::vector<Row>& rows);

    Reader openReader() const { return Reader(path_); }

    // Обход всех строк: fn(offset, row)
    template <class F>
    void scan(F&& fn) const {
        Reader reader = openReader();
        if (!reader.isOpen()) return;
        Row row;
        uint64_t offset = 0;
        while (reader.next(offset, row)) {
            fn(offset, row);
        }
    }

private:
    std::string path_;
};
