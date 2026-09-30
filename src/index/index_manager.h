#pragma once

#include <string>
#include <memory>
#include <variant>
#include <optional>
#include <vector>
#include <utility>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <fstream>
#include "bplus_tree.h"

namespace dbms {

class IndexError : public std::runtime_error {
public:
    explicit IndexError(const std::string& message)
        : std::runtime_error(message) {}
};

using Key = std::variant<long long, std::string>;

class IndexManager {
private:
    std::string filepath_;
    bool isIntType_;
    int tree_degree_;

    std::unique_ptr<BPlusTree<long long>> intTree_;
    std::unique_ptr<BPlusTree<std::string>> strTree_;

    template <typename T>
    static void writePod(std::ostream& out, const T& v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }

    template <typename T>
    static bool readPod(std::istream& in, T& v) {
        return static_cast<bool>(in.read(reinterpret_cast<char*>(&v), sizeof(T)));
    }

public:
    IndexManager(const std::string& filepath, bool isIntType, int degree = 3)
        : filepath_(filepath), isIntType_(isIntType), tree_degree_(degree) {
        if (isIntType_) {
            intTree_ = std::make_unique<BPlusTree<long long>>(tree_degree_);
        } else {
            strTree_ = std::make_unique<BPlusTree<std::string>>(tree_degree_);
        }
    }

    const std::string& path() const { return filepath_; }

    void save(uint64_t data_file_size) const {
        std::string tmp = filepath_ + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) {
                throw IndexError("Не удается открыть индекс файл для записи: " + tmp);
            }
            writePod<uint64_t>(out, data_file_size);

            if (isIntType_) {
                auto entries = intTree_->get_all_entries();
                writePod<uint64_t>(out, entries.size());
                for (const auto& [key, offset] : entries) {
                    writePod(out, key);
                    writePod(out, offset);
                }
            } else {
                auto entries = strTree_->get_all_entries();
                writePod<uint64_t>(out, entries.size());
                for (const auto& [key, offset] : entries) {
                    writePod<uint64_t>(out, key.size());
                    out.write(key.data(), static_cast<std::streamsize>(key.size()));
                    writePod(out, offset);
                }
            }
            out.flush();
            if (!out) {
                throw IndexError("Не удалось записать файл индекса: " + tmp);
            }
        }
        std::filesystem::rename(tmp, filepath_);
    }

    bool load(uint64_t expected_data_file_size) {
        std::error_code ec;
        if (!std::filesystem::exists(filepath_, ec)) return false;
        uint64_t file_size = std::filesystem::file_size(filepath_, ec);
        if (ec) return false;

        std::ifstream in(filepath_, std::ios::binary);
        if (!in) return false;

        uint64_t stored_size = 0, count = 0;
        if (!readPod(in, stored_size) || !readPod(in, count)) return false;
        if (stored_size != expected_data_file_size) return false;
        if (count > file_size / 16) return false;   // минимум 16 байт на запись

        if (isIntType_) {
            std::vector<std::pair<long long, FileOffset>> entries;
            entries.reserve(count);
            for (uint64_t i = 0; i < count; ++i) {
                long long key; FileOffset offset;
                if (!readPod(in, key) || !readPod(in, offset)) return false;
                entries.emplace_back(key, offset);
            }
            for (const auto& [k, o] : entries) intTree_->insert(k, o);
        } else {
            std::vector<std::pair<std::string, FileOffset>> entries;
            entries.reserve(count);
            for (uint64_t i = 0; i < count; ++i) {
                uint64_t len;
                if (!readPod(in, len) || len > file_size) return false;
                std::string key(len, '\0');
                if (len > 0 && !in.read(&key[0], static_cast<std::streamsize>(len))) return false;
                FileOffset offset;
                if (!readPod(in, offset)) return false;
                entries.emplace_back(std::move(key), offset);
            }
            for (auto& [k, o] : entries) strTree_->insert(k, o);
        }
        return true;
    }

    void insert(const Key& key, FileOffset offset) {
        FileOffset existing = INVALID_OFFSET;
        if (find(key, existing)) {
            throw IndexError("Вставка дубликата ключа: значения в индексированных столбцах должны быть уникальными.");
        }

        if (isIntType_ && std::holds_alternative<long long>(key)) {
            intTree_->insert(std::get<long long>(key), offset);
        } else if (!isIntType_ && std::holds_alternative<std::string>(key)) {
            strTree_->insert(std::get<std::string>(key), offset);
        } else {
            throw IndexError("Несоответствие типов при вставке в индекс.");
        }
    }

    void remove(const Key& key) {
        if (isIntType_ && std::holds_alternative<long long>(key)) {
            intTree_->remove(std::get<long long>(key));
        } else if (!isIntType_ && std::holds_alternative<std::string>(key)) {
            strTree_->remove(std::get<std::string>(key));
        } else {
            throw IndexError("Несоответствие типов при удалении индекса.");
        }
    }

    bool find(const Key& key, FileOffset& outOffset) const {
        FileOffset result = INVALID_OFFSET;

        if (isIntType_ && std::holds_alternative<long long>(key)) {
            result = intTree_->search(std::get<long long>(key));
        } else if (!isIntType_ && std::holds_alternative<std::string>(key)) {
            result = strTree_->search(std::get<std::string>(key));
        } else {
            throw IndexError("Несоответствие типов при поиске по индексу.");
        }

        if (result != INVALID_OFFSET) {
            outOffset = result;
            return true;
        }
        return false;
    }

    std::vector<FileOffset> rangeSearch(const std::optional<Key>& lo,
                                        const std::optional<Key>& hi) const {
        if (isIntType_) {
            long long l = 0, h = 0;
            const long long *pl = nullptr, *ph = nullptr;
            if (lo) {
                if (!std::holds_alternative<long long>(*lo)) throw IndexError("Несоответствие типов при поиске в диапазоне.");
                l = std::get<long long>(*lo); pl = &l;
            }
            if (hi) {
                if (!std::holds_alternative<long long>(*hi)) throw IndexError("Несоответствие типов при поиске в диапазоне.");
                h = std::get<long long>(*hi); ph = &h;
            }
            return intTree_->range_search(pl, ph);
        }

        std::string l, h;
        const std::string *pl = nullptr, *ph = nullptr;
        if (lo) {
            if (!std::holds_alternative<std::string>(*lo)) throw IndexError("Несоответствие типов при поиске в диапазоне.");
            l = std::get<std::string>(*lo); pl = &l;
        }
        if (hi) {
            if (!std::holds_alternative<std::string>(*hi)) throw IndexError("Несоответствие типов при поиске в диапазоне.");
            h = std::get<std::string>(*hi); ph = &h;
        }
        return strTree_->range_search(pl, ph);
    }
};

}
