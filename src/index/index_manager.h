#pragma once

#include <string>
#include <memory>
#include <variant>
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

class IndexManager {
private:
    std::string filepath_;
    bool isIntType_;
    int tree_degree_; // Минимальная степень B+ дерева

    // Используем BPlusTree с одним параметром, так как значение жестко FileOffset
    std::unique_ptr<BPlusTree<long long>> intTree_;
    std::unique_ptr<BPlusTree<std::string>> strTree_;

public:
    // По умолчанию ставим степень дерева = 3, если не передано иное
    IndexManager(const std::string& filepath, bool isIntType, int degree = 3)
        : filepath_(filepath), isIntType_(isIntType), tree_degree_(degree) {
        if (isIntType_) {
            intTree_ = std::make_unique<BPlusTree<long long>>(tree_degree_);
        } else {
            strTree_ = std::make_unique<BPlusTree<std::string>>(tree_degree_);
        }
    }

    void save() const {
        // Открываем файл в бинарном режиме с перезаписью (trunc)
        std::ofstream out(filepath_, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw IndexError("Cannot open index file for writing: " + filepath_);
        }

        if (isIntType_) {
            auto entries = intTree_->get_all_entries();
            size_t count = entries.size();
            
            // Пишем количество записей
            out.write(reinterpret_cast<const char*>(&count), sizeof(count));
            
            // Пишем пары [long long key, FileOffset offset]
            for (const auto& [key, offset] : entries) {
                out.write(reinterpret_cast<const char*>(&key), sizeof(key));
                out.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
            }
        } else {
            auto entries = strTree_->get_all_entries();
            size_t count = entries.size();
            
            out.write(reinterpret_cast<const char*>(&count), sizeof(count));
            
            for (const auto& [key, offset] : entries) {
                size_t len = key.size();
                // Для строк: пишем длину, затем символы, затем смещение
                out.write(reinterpret_cast<const char*>(&len), sizeof(len));
                out.write(key.data(), len);
                out.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
            }
        }
    }

    void load() {
        if (!std::filesystem::exists(filepath_)) return; // Индекса еще нет, это нормально

        std::ifstream in(filepath_, std::ios::binary);
        if (!in) {
            throw IndexError("Cannot open index file for reading: " + filepath_);
        }

        size_t count = 0;
        // Читаем количество записей. Если файл пустой, выходим
        if (!in.read(reinterpret_cast<char*>(&count), sizeof(count))) {
            return;
        }

        if (isIntType_) {
            for (size_t i = 0; i < count; ++i) {
                long long key;
                FileOffset offset;
                in.read(reinterpret_cast<char*>(&key), sizeof(key));
                in.read(reinterpret_cast<char*>(&offset), sizeof(offset));
                intTree_->insert(key, offset); // Восстанавливаем дерево
            }
        } else {
            for (size_t i = 0; i < count; ++i) {
                size_t len;
                in.read(reinterpret_cast<char*>(&len), sizeof(len));
                
                std::string key(len, '\0'); // Выделяем память под строку
                in.read(key.data(), len);
                
                FileOffset offset;
                in.read(reinterpret_cast<char*>(&offset), sizeof(offset));
                
                strTree_->insert(key, offset); // Восстанавливаем дерево
            }
        }
    }

    void insert(const std::variant<long long, std::string>& key, FileOffset offset) {
        // 1. Проверяем на дубликаты (для поля INDEXED значения должны быть уникальными)
        FileOffset existingOffset = INVALID_OFFSET;
        if (find(key, existingOffset)) {
            throw IndexError("Duplicate key insertion: indexed columns must be unique.");
        }

        // 2. Вставляем в нужное дерево
        if (isIntType_ && std::holds_alternative<long long>(key)) {
            intTree_->insert(std::get<long long>(key), offset);
        } else if (!isIntType_ && std::holds_alternative<std::string>(key)) {
            strTree_->insert(std::get<std::string>(key), offset);
        } else {
            throw IndexError("Type mismatch in index insertion.");
        }
    }

    void remove(const std::variant<long long, std::string>& key) {
        if (isIntType_ && std::holds_alternative<long long>(key)) {
            intTree_->remove(std::get<long long>(key));
        } else if (!isIntType_ && std::holds_alternative<std::string>(key)) {
            strTree_->remove(std::get<std::string>(key));
        } else {
            throw IndexError("Type mismatch in index removal.");
        }
    }

    // Обертка для поиска: возвращает true и записывает результат в outOffset
    bool find(const std::variant<long long, std::string>& key, FileOffset& outOffset) const {
        FileOffset result = INVALID_OFFSET;
        
        if (isIntType_ && std::holds_alternative<long long>(key)) {
            result = intTree_->search(std::get<long long>(key));
        } else if (!isIntType_ && std::holds_alternative<std::string>(key)) {
            result = strTree_->search(std::get<std::string>(key));
        } else {
            throw IndexError("Type mismatch in index search.");
        }

        if (result != INVALID_OFFSET) {
            outOffset = result;
            return true;
        }
        
        return false;
    }

    std::vector<FileOffset> rangeSearch(const std::variant<long long, std::string>& start_key, 
                                        const std::variant<long long, std::string>& end_key) const {
        if (isIntType_ && std::holds_alternative<long long>(start_key) && std::holds_alternative<long long>(end_key)) {
            // Предполагается, что в BPlusTree реализован метод range_search(start, end)
            return intTree_->range_search(std::get<long long>(start_key), std::get<long long>(end_key));
        } 
        else if (!isIntType_ && std::holds_alternative<std::string>(start_key) && std::holds_alternative<std::string>(end_key)) {
            return strTree_->range_search(std::get<std::string>(start_key), std::get<std::string>(end_key));
        }
        
        throw IndexError("Type mismatch in range search.");
    }
};

} // namespace dbms