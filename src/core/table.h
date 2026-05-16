#pragma once

#include <string>
#include <vector>
#include <memory>
#include "../parser/parser.h"
#include "../storage/table_file_manager.h"
#include "../index/bplus_tree.h"

class Table {
private:
    std::string name;
    CreateTableStmt schema;
    std::unique_ptr<TableFileManager> file_manager;
    std::unique_ptr<BPlusTree<int>> int_index;

public:
    explicit Table(const CreateTableStmt& table_schema);

    void insertRow(int key, const std::string& data_payload);
    bool findByKey(int key, Row& out_row);
    
    std::string getName() const;
};