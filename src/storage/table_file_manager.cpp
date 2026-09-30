#include "table_file_manager.h"

#include <filesystem>
#include <limits>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

// Защита от повреждённых файлов: значения из файла не доверяем
constexpr uint32_t kMaxFields = 4096;
constexpr uint32_t kMaxValueLen = 1u << 30;

std::string serialize(const Row& row) {
    if (row.size() > kMaxFields) {
        throw std::runtime_error("Слишком много полей в строке таблицы.");
    }
    std::string buf;
    uint32_t n = static_cast<uint32_t>(row.size());
    buf.append(reinterpret_cast<const char*>(&n), sizeof(n));
    for (const auto& field : row) {
        buf.push_back(field ? '\0' : '\1');
        if (field) {
            if (field->size() > kMaxValueLen) {
                throw std::runtime_error("Значение поля слишком велико для хранения.");
            }
            uint32_t len = static_cast<uint32_t>(field->size());
            buf.append(reinterpret_cast<const char*>(&len), sizeof(len));
            buf.append(*field);
        }
    }
    return buf;
}

bool deserialize(std::istream& in, Row& row) {
    uint32_t n = 0;
    if (!in.read(reinterpret_cast<char*>(&n), sizeof(n))) return false;
    if (n > kMaxFields) return false;

    row.assign(n, std::nullopt);
    for (uint32_t i = 0; i < n; ++i) {
        char is_null = 0;
        if (!in.get(is_null)) return false;
        if (is_null) continue;

        uint32_t len = 0;
        if (!in.read(reinterpret_cast<char*>(&len), sizeof(len))) return false;
        if (len > kMaxValueLen) return false;

        std::string value(len, '\0');
        if (len > 0 && !in.read(&value[0], static_cast<std::streamsize>(len))) return false;
        row[i] = std::move(value);
    }
    return true;
}

}

// ---------- Reader ----------

bool TableFileManager::Reader::readAt(uint64_t offset, Row& row) {
    if (!in_.is_open()) return false;
    in_.clear();   // сбрасываем eofbit/failbit после предыдущих неудачных чтений
    in_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!in_) return false;
    return deserialize(in_, row);
}

bool TableFileManager::Reader::next(uint64_t& offset, Row& row) {
    if (!in_.is_open()) return false;
    std::streamoff pos = in_.tellg();
    if (pos < 0) return false;
    if (!deserialize(in_, row)) return false;
    offset = static_cast<uint64_t>(pos);
    return true;
}

// ---------- Writer ----------

TableFileManager::Writer::Writer(const std::string& target_path)
    : target_(target_path), tmp_(target_path + ".tmp"),
      out_(tmp_, std::ios::binary | std::ios::trunc) {
    if (!out_) {
        throw std::runtime_error("Не удалось создать временный файл: " + tmp_);
    }
}

TableFileManager::Writer::~Writer() {
    if (!committed_) {
        out_.close();
        std::error_code ec;
        fs::remove(tmp_, ec);
    }
}

uint64_t TableFileManager::Writer::write(const Row& row) {
    std::string buf = serialize(row);
    uint64_t at = offset_;
    out_.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    if (!out_) {
        throw std::runtime_error("Ошибка записи во временный файл: " + tmp_);
    }
    offset_ += buf.size();
    return at;
}

void TableFileManager::Writer::commit() {
    out_.flush();
    if (!out_) {
        throw std::runtime_error("Ошибка записи во временный файл: " + tmp_);
    }
    out_.close();
    fs::rename(tmp_, target_);   // атомарная подмена
    committed_ = true;
}

// ---------- TableFileManager ----------

bool TableFileManager::exists() const {
    std::error_code ec;
    return fs::exists(path_, ec);
}

uint64_t TableFileManager::size() const {
    std::error_code ec;
    if (!fs::exists(path_, ec)) return 0;
    auto sz = fs::file_size(path_, ec);
    return ec ? 0 : static_cast<uint64_t>(sz);
}

void TableFileManager::createEmpty() const {
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Не удалось создать файл данных: " + path_);
    }
}

void TableFileManager::removeFiles() const noexcept {
    std::error_code ec;
    fs::remove(path_, ec);
    fs::remove(tmpPath(), ec);
}

bool TableFileManager::truncateToValidPrefix() const {
    uint64_t total = size();
    if (total == 0) return false;

    Reader reader = openReader();
    if (!reader.isOpen()) return false;

    Row row;
    uint64_t offset = 0;
    uint64_t valid_end = 0;
    while (reader.next(offset, row)) {
        valid_end = reader.position();
    }
    reader.close();

    if (valid_end >= total) return false;

    std::error_code ec;
    fs::resize_file(path_, valid_end, ec);
    if (ec) {
        throw std::runtime_error("Не удалось отсечь оборванную запись в файле: " + path_);
    }
    return true;
}

std::vector<uint64_t> TableFileManager::appendRows(const std::vector<Row>& rows) {
    // Сначала сериализуем всё в память: если одна из строк некорректна, файл не тронут
    uint64_t base = size();
    std::string buf;
    std::vector<uint64_t> offsets;
    offsets.reserve(rows.size());
    for (const Row& row : rows) {
        offsets.push_back(base + buf.size());
        buf += serialize(row);
    }

    std::ofstream out(path_, std::ios::binary | std::ios::app);
    if (!out) {
        throw std::runtime_error("Не удалось открыть файл данных для записи: " + path_);
    }
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    out.flush();
    if (!out) {
        throw std::runtime_error("Ошибка записи в файл данных: " + path_);
    }
    return offsets;
}
