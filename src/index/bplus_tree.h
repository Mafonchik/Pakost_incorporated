#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>

using FileOffset = uint64_t;
constexpr FileOffset INVALID_OFFSET = static_cast<FileOffset>(-1);

template <typename T>
struct BPlusNode {
    bool is_leaf;
    std::vector<T> keys;
    std::vector<BPlusNode<T>*> children;
    std::vector<FileOffset> record_pointers; // Смещения записей в файле для листьев
    BPlusNode<T>* next_leaf;                 // Указатель на следующий лист

    explicit BPlusNode(bool leaf) : is_leaf(leaf), next_leaf(nullptr) {}
};

template <typename T>
class BPlusTree {
private:
    BPlusNode<T>* root;
    int t; // Минимальная степень B+ дерева

    void splitChild(BPlusNode<T>* parent, size_t child_index, BPlusNode<T>* child) {
        BPlusNode<T>* z = new BPlusNode<T>(child->is_leaf);

        if (child->is_leaf) {
            for (int i = 0; i < t - 1; i++) {
                z->keys.push_back(child->keys[i + t]);
                z->record_pointers.push_back(child->record_pointers[i + t]);
            }

            child->keys.resize(t);
            child->record_pointers.resize(t);

            z->next_leaf = child->next_leaf;
            child->next_leaf = z;

            parent->keys.insert(parent->keys.begin() + child_index, z->keys[0]);
            parent->children.insert(parent->children.begin() + child_index + 1, z);
        } else {
            for (int i = 0; i < t - 1; i++) {
                z->keys.push_back(child->keys[i + t]);
            }

            for (int i = 0; i < t; i++) {
                z->children.push_back(child->children[i + t]);
            }

            T median_key = child->keys[t - 1];

            child->keys.resize(t - 1);
            child->children.resize(t);

            parent->keys.insert(parent->keys.begin() + child_index, median_key);
            parent->children.insert(parent->children.begin() + child_index + 1, z);
        }
    }

    void insertNonFull(BPlusNode<T>* node, const T& key, FileOffset offset) {
        int i = static_cast<int>(node->keys.size()) - 1;

        if (node->is_leaf) {
            node->keys.push_back(T());
            node->record_pointers.push_back(0);

            while (i >= 0 && node->keys[i] > key) {
                node->keys[i + 1] = node->keys[i];
                node->record_pointers[i + 1] = node->record_pointers[i];
                i--;
            }

            node->keys[i + 1] = key;
            node->record_pointers[i + 1] = offset;
        } else {
            while (i >= 0 && node->keys[i] > key) {
                i--;
            }
            i++;

            if (node->children[i]->keys.size() == static_cast<size_t>(2 * t - 1)) {
                splitChild(node, i, node->children[i]);
                if (node->keys[i] < key) {
                    i++;
                }
            }

            insertNonFull(node->children[i], key, offset);
        }
    }

    void removeInternal(BPlusNode<T>* node, const T& key) {
        if (node->is_leaf) {
            // В листе ищем точное совпадение
            auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
            if (it != node->keys.end() && *it == key) {
                size_t idx = std::distance(node->keys.begin(), it);
                node->keys.erase(node->keys.begin() + idx);
                node->record_pointers.erase(node->record_pointers.begin() + idx);
            }
            return;
        }

        // Внутренний узел: спускаемся к потомку.
        // Используем upper_bound, чтобы правильно маршрутизировать поиск.
        auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
        size_t child_idx = std::distance(node->keys.begin(), it);

        BPlusNode<T>* child = node->children[child_idx];
        removeInternal(child, key);

        // Балансируем дерево при возврате из рекурсии, если элементов стало меньше t-1
        if (child->keys.size() < static_cast<size_t>(t - 1)) {
            balance(node, child_idx);
        }
    }

    void balance(BPlusNode<T>* parent, size_t child_idx) {
        BPlusNode<T>* child = parent->children[child_idx];

        // Пробуем одолжить у левого соседа
        if (child_idx > 0 && parent->children[child_idx - 1]->keys.size() >= static_cast<size_t>(t)) {
            borrowFromPrev(parent, child_idx);
            return;
        }
        
        // Пробуем одолжить у правого соседа
        if (child_idx < parent->children.size() - 1 && parent->children[child_idx + 1]->keys.size() >= static_cast<size_t>(t)) {
            borrowFromNext(parent, child_idx);
            return;
        }

        // Если одолжить нельзя — сливаем узлы
        if (child_idx < parent->children.size() - 1) {
            merge(parent, child_idx);
        } else {
            merge(parent, child_idx - 1);
        }
    }

    void borrowFromPrev(BPlusNode<T>* parent, size_t child_idx) {
        BPlusNode<T>* child = parent->children[child_idx];
        BPlusNode<T>* left_sibling = parent->children[child_idx - 1];

        if (child->is_leaf) {
            child->keys.insert(child->keys.begin(), left_sibling->keys.back());
            child->record_pointers.insert(child->record_pointers.begin(), left_sibling->record_pointers.back());
            
            left_sibling->keys.pop_back();
            left_sibling->record_pointers.pop_back();

            parent->keys[child_idx - 1] = child->keys[0];
        } else {
            child->keys.insert(child->keys.begin(), parent->keys[child_idx - 1]);
            parent->keys[child_idx - 1] = left_sibling->keys.back();
            left_sibling->keys.pop_back();

            child->children.insert(child->children.begin(), left_sibling->children.back());
            left_sibling->children.pop_back();
        }
    }

    void borrowFromNext(BPlusNode<T>* parent, size_t child_idx) {
        BPlusNode<T>* child = parent->children[child_idx];
        BPlusNode<T>* right_sibling = parent->children[child_idx + 1];

        if (child->is_leaf) {
            child->keys.push_back(right_sibling->keys.front());
            child->record_pointers.push_back(right_sibling->record_pointers.front());

            right_sibling->keys.erase(right_sibling->keys.begin());
            right_sibling->record_pointers.erase(right_sibling->record_pointers.begin());

            parent->keys[child_idx] = right_sibling->keys[0];
        } else {
            child->keys.push_back(parent->keys[child_idx]);
            parent->keys[child_idx] = right_sibling->keys.front();
            right_sibling->keys.erase(right_sibling->keys.begin());

            child->children.push_back(right_sibling->children.front());
            right_sibling->children.erase(right_sibling->children.begin());
        }
    }

    void merge(BPlusNode<T>* parent, size_t child_idx) {
        BPlusNode<T>* child = parent->children[child_idx];
        BPlusNode<T>* right_sibling = parent->children[child_idx + 1];

        if (child->is_leaf) {
            child->keys.insert(child->keys.end(), right_sibling->keys.begin(), right_sibling->keys.end());
            child->record_pointers.insert(child->record_pointers.end(), right_sibling->record_pointers.begin(), right_sibling->record_pointers.end());

            child->next_leaf = right_sibling->next_leaf;

            parent->keys.erase(parent->keys.begin() + child_idx);
            parent->children.erase(parent->children.begin() + child_idx + 1);

            delete right_sibling;
        } else {
            child->keys.push_back(parent->keys[child_idx]);
            
            child->keys.insert(child->keys.end(), right_sibling->keys.begin(), right_sibling->keys.end());
            child->children.insert(child->children.end(), right_sibling->children.begin(), right_sibling->children.end());

            parent->keys.erase(parent->keys.begin() + child_idx);
            parent->children.erase(parent->children.begin() + child_idx + 1);

            delete right_sibling;
        }
    }

    void destroyNode(BPlusNode<T>* node) {
        if (node == nullptr) {
            return;
        }

        if (!node->is_leaf) {
            for (BPlusNode<T>* child : node->children) {
                destroyNode(child);
            }
        }

        delete node;
    }

public:
    explicit BPlusTree(int _t) : t(_t) {
        root = new BPlusNode<T>(true); 
    }

    ~BPlusTree() {
        destroyNode(root);
    }

    FileOffset search(T key) {
        BPlusNode<T>* current = root;
        
        while (!current->is_leaf) {
            auto it = std::upper_bound(current->keys.begin(), current->keys.end(), key);
            size_t idx = std::distance(current->keys.begin(), it);
            current = current->children[idx];
        }

        auto it = std::lower_bound(current->keys.begin(), current->keys.end(), key);
        if (it != current->keys.end() && *it == key) {
            size_t idx = std::distance(current->keys.begin(), it);
            return current->record_pointers[idx];
        }

        // Заменили возврат 0 на INVALID_OFFSET
        return INVALID_OFFSET;
    }

    void insert(T key, FileOffset offset) {
        BPlusNode<T>* r = root;
        if (r->keys.size() == static_cast<size_t>(2 * t - 1)) {
            BPlusNode<T>* new_root = new BPlusNode<T>(false);
            new_root->children.push_back(r);
            root = new_root;
            splitChild(new_root, 0, r);
            insertNonFull(new_root, key, offset);
        } else {
            insertNonFull(r, key, offset);
        }
    }

    std::vector<FileOffset> range_search(T start_key, T end_key) {
        std::vector<FileOffset> result;
        BPlusNode<T>* current = root;
        
        while (!current->is_leaf) {
            auto it = std::upper_bound(current->keys.begin(), current->keys.end(), start_key);
            size_t idx = std::distance(current->keys.begin(), it);
            current = current->children[idx];
        }

        while (current != nullptr) {
            for (size_t i = 0; i < current->keys.size(); ++i) {
                // Если вышли за верхнюю границу диапазона, прерываем поиск
                if (current->keys[i] > end_key) {
                    return result; 
                }
                
                if (current->keys[i] >= start_key) {
                    result.push_back(current->record_pointers[i]);
                }
            }
            current = current->next_leaf;
        }

        return result;
    }
    
    void remove(T key) {
        if (!root) return;

        removeInternal(root, key);

        // Если корень стал пустым, но он не лист — понижаем высоту дерева
        if (root->keys.empty() && !root->is_leaf) {
            BPlusNode<T>* tmp = root;
            root = root->children[0]; 
            delete tmp; 
        }
    }

    std::vector<std::pair<T, FileOffset>> get_all_entries() const {
        std::vector<std::pair<T, FileOffset>> result;
        BPlusNode<T>* current = root;
        
        if (!current) return result;

        // Спускаемся к самому левому листу
        while (!current->is_leaf) {
            if (current->children.empty()) break;
            current = current->children[0];
        }

        // Проходим по связанному списку листьев
        while (current != nullptr) {
            for (size_t i = 0; i < current->keys.size(); ++i) {
                result.push_back({current->keys[i], current->record_pointers[i]});
            }
            current = current->next_leaf;
        }

        return result;
    }
};