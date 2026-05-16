#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

using FileOffset = uint64_t;

template <typename T>
struct BPlusNode {
    bool is_leaf;
    std::vector<T> keys; 
    std::vector<BPlusNode<T>*> children;
    std::vector<FileOffset> record_pointers; 
    BPlusNode<T>* next_leaf;

    BPlusNode(bool leaf) {
        is_leaf = leaf;
        next_leaf = nullptr;
    }
};

template <typename T>
class BPlusTree {
private:
    BPlusNode<T>* root;
    int t; 
    
    void splitChild(BPlusNode<T>* parent, int child_index, BPlusNode<T>* child) {
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

    void insertNonFull(BPlusNode<T>* node, T key, FileOffset offset) {
        int i = static_cast<int>(node->keys.size()) - 1;

        if (node->is_leaf) {
            node->keys.push_back(T()); // Универсальная инициализация пустого значения
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

            if (node->children[i]->keys.size() == 2 * t - 1) {
                splitChild(node, i, node->children[i]);
                if (node->keys[i] < key) {
                    i++;
                }
            }

            insertNonFull(node->children[i], key, offset);
        }
    }

    // Вспомогательный метод для быстрого поиска индекса ключа в узле
    int findKeyIndex(BPlusNode<T>* node, T key) {
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        return std::distance(node->keys.begin(), it);
    }

    // Рекурсивное удаление ключа
    void removeInternal(BPlusNode<T>* node, T key) {
        int idx = findKeyIndex(node, key);

        if (node->is_leaf) {
            // ==========================================
            // СЛУЧАЙ 1: Ключ найден в листе
            // ==========================================
            if (idx < node->keys.size() && node->keys[idx] == key) {
                // Удаляем ключ и соответствующий ему указатель на файл
                node->keys.erase(node->keys.begin() + idx);
                node->record_pointers.erase(node->record_pointers.begin() + idx);
            } else {
                // Ключа нет в дереве (можно выбросить исключение или просто выйти)
                return; 
            }
        } else {
            // ==========================================
            // СЛУЧАЙ 2: Спуск по внутренним узлам
            // ==========================================
            bool is_last_child = (idx == node->keys.size());
            BPlusNode<T>* child = node->children[idx];

            // Рекурсивно спускаемся к нужному ребенку
            removeInternal(child, key);

            // После возврата из рекурсии проверяем, не нарушилось ли свойство B-дерева
            if (child->keys.size() < t - 1) {
                balance(node, idx);
            }
        }
    }

    // Метод восстановления баланса (вызывается, когда в child стало t - 2 ключей)
    void balance(BPlusNode<T>* parent, int child_idx) {
        BPlusNode<T>* child = parent->children[child_idx];

        // 1. Пытаемся занять у левого брата
        if (child_idx > 0 && parent->children[child_idx - 1]->keys.size() >= t) {
            borrowFromPrev(parent, child_idx);
            return;
        }
        
        // 2. Пытаемся занять у правого брата
        if (child_idx < parent->children.size() - 1 && parent->children[child_idx + 1]->keys.size() >= t) {
            borrowFromNext(parent, child_idx);
            return;
        }

        // 3. Если занять не удалось, сливаем (Merge)
        if (child_idx < parent->children.size() - 1) {
            // Сливаем с правым братом
            merge(parent, child_idx);
        } else {
            // Если ребенок последний, сливаем с левым братом
            merge(parent, child_idx - 1);
        }
    }

    // --- Заготовки для методов балансировки ---
    // Для полноценной работы СУБД их необходимо будет детализировать, 
    // аккуратно перекладывая элементы std::vector между узлами.

    void borrowFromPrev(BPlusNode<T>* parent, int child_idx) {
        BPlusNode<T>* child = parent->children[child_idx];
        BPlusNode<T>* left_sibling = parent->children[child_idx - 1];

        if (child->is_leaf) {
            // 1. Для листьев: переносим последний элемент из left_sibling в начало child
            child->keys.insert(child->keys.begin(), left_sibling->keys.back());
            child->values.insert(child->values.begin(), left_sibling->values.back());
            
            left_sibling->keys.pop_back();
            left_sibling->values.pop_back();

            // 2. Обновляем разделитель в родителе (теперь минимальный ключ child равен его первому ключу)
            parent->keys[child_idx - 1] = child->keys[0];
        } else {
            // Для внутренних узлов учитываем указатели на детей
            child->keys.insert(child->keys.begin(), parent->keys[child_idx - 1]);
            parent->keys[child_idx - 1] = left_sibling->keys.back();
            left_sibling->keys.pop_back();

            child->children.insert(child->children.begin(), left_sibling->children.back());
            left_sibling->children.pop_back();

            // Исправляем указатель на родителя у перенесенного ребенка, если он хранится
            if (!child->children.empty() && child->children[0]) {
                // child->children[0]->parent = child; // (если поддерживается указатель на parent)
            }
        }
    }

    void borrowFromNext(BPlusNode<T>* parent, int child_idx) {
        BPlusNode<T>* child = parent->children[child_idx];
        BPlusNode<T>* right_sibling = parent->children[child_idx + 1];

        if (child->is_leaf) {
            // 1. Для листьев: переносим первый элемент из right_sibling в конец child
            child->keys.push_back(right_sibling->keys.front());
            child->values.push_back(right_sibling->values.front());

            right_sibling->keys.erase(right_sibling->keys.begin());
            right_sibling->values.erase(right_sibling->values.begin());

            // 2. Обновляем разделитель в родителе (теперь он равен новому первому ключу правого брата)
            parent->keys[child_idx] = right_sibling->keys[0];
        } else {
            // Для внутренних узлов
            child->keys.push_back(parent->keys[child_idx]);
            parent->keys[child_idx] = right_sibling->keys.front();
            right_sibling->keys.erase(right_sibling->keys.begin());

            child->children.push_back(right_sibling->children.front());
            right_sibling->children.erase(right_sibling->children.begin());
        }
    }

    void merge(BPlusNode<T>* parent, int child_idx) {
        BPlusNode<T>* child = parent->children[child_idx];
        BPlusNode<T>* right_sibling = parent->children[child_idx + 1];

        if (child->is_leaf) {
            // 1. Переносим все ключи и значения из правого соседа в текущий узел
            child->keys.insert(child->keys.end(), right_sibling->keys.begin(), right_sibling->keys.end());
            child->values.insert(child->values.end(), right_sibling->values.begin(), right_sibling->values.end());

            // 2. ВАЖНО для листьев: перевязываем указатель связного списка листьев (next_leaf)
            child->next_leaf = right_sibling->next_leaf;

            // 3. Удаляем разделитель из родителя и указатель на правого брата
            parent->keys.erase(parent->keys.begin() + child_idx);
            parent->children.erase(parent->children.begin() + child_idx + 1);

            // 4. Освобождаем память правого брата
            delete right_sibling;
        } else {
            // Для внутренних узлов спускаем ключ-разделитель из родителя внутрь левого узла
            child->keys.push_back(parent->keys[child_idx]);
            
            // Добавляем ключи и детей правого брата
            child->keys.insert(child->keys.end(), right_sibling->keys.begin(), right_sibling->keys.end());
            child->children.insert(child->children.end(), right_sibling->children.begin(), right_sibling->children.end());

            // Удаляем разделитель и ссылку на правого брата из родителя
            parent->keys.erase(parent->keys.begin() + child_idx);
            parent->children.erase(parent->children.begin() + child_idx + 1);

            delete right_sibling;
        }
    }

public:
    BPlusTree(int _t) : t(_t) {
        root = new BPlusNode<T>(true); 
    }

    FileOffset search(T key) {
        BPlusNode<T>* current = root;
        
        while (!current->is_leaf) {
            auto it = std::upper_bound(current->keys.begin(), current->keys.end(), key);
            int idx = std::distance(current->keys.begin(), it);
            current = current->children[idx];
        }

        auto it = std::lower_bound(current->keys.begin(), current->keys.end(), key);
        if (it != current->keys.end() && *it == key) {
            int idx = std::distance(current->keys.begin(), it);
            return current->record_pointers[idx];
        }

        return 0; 
    }

    void insert(T key, FileOffset offset) {
        BPlusNode<T>* r = root;
        if (r->keys.size() == 2 * t - 1) {
            BPlusNode<T>* new_root = new BPlusNode<T>(false);
            new_root->children.push_back(r);
            root = new_root;
            splitChild(new_root, 0, r);
            insertNonFull(new_root, key, offset);
        } else {
            insertNonFull(r, key, offset);
        }
    }

    // Диапазонный поиск: возвращает список смещений для интервала [start_key, end_key)
    std::vector<FileOffset> range_search(T start_key, T end_key) {
        std::vector<FileOffset> result;
        BPlusNode<T>* current = root;
        
        // Шаг 1: Спускаемся до нужного листа, где должен быть start_key
        while (!current->is_leaf) {
            auto it = std::upper_bound(current->keys.begin(), current->keys.end(), start_key);
            int idx = std::distance(current->keys.begin(), it);
            current = current->children[idx];
        }

        // Шаг 2: Итерируемся по текущему листу и связанным следующим листьям
        while (current != nullptr) {
            for (size_t i = 0; i < current->keys.size(); ++i) {
                // Так как интервал [start_key, end_key) - правая граница не включается.
                // Дерево отсортировано, поэтому если текущий ключ >= end_key, 
                // дальше проверять нет смысла, выходим.
                if (current->keys[i] >= end_key) {
                    return result;
                }
                
                // Если ключ больше или равен левой границе, добавляем его в результат
                if (current->keys[i] >= start_key) {
                    result.push_back(current->record_pointers[i]);
                }
            }
            // Если элементы в текущем листе закончились, переходим к следующему листу (брату)
            current = current->next_leaf;
        }

        return result;
    }
    
    // Публичный интерфейс для вызова удаления
    void remove(T key) {
        if (!root) return;

        removeInternal(root, key);

        // Если после удаления корень стал пустым (но он не лист)
        if (root->keys.empty() && !root->is_leaf) {
            BPlusNode<T>* tmp = root;
            root = root->children[0]; // Единственный ребенок становится новым корнем
            delete tmp; // Освобождаем память
        }
    }
};