#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <regex>
#include <functional>
#include <cstdint>
#include "../parser/parser.h"
#include "../index/index_manager.h"

enum class DataType { INT, STRING };

struct ColumnDefinition {
    std::string name;
    DataType type = DataType::INT;
    bool is_not_null = false;
    bool is_indexed = false;
};

struct TableMeta {
    std::string table_name;
    std::vector<ColumnDefinition> columns;

    int columnIndex(const std::string& name) const {
        for (size_t i = 0; i < columns.size(); ++i) {
            if (columns[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }
};

// Значение поля: nullopt = NULL. INT хранится в каноническом текстовом виде.
using Field = std::optional<std::string>;
using Row = std::vector<Field>;

struct Operand {
    bool is_column = false;
    bool is_null_const = false;
    size_t col_idx = 0;
    DataType type = DataType::INT;
    Field constant;

    bool hasType() const { return is_column || !is_null_const; }
    const Field& get(const Row& row) const { return is_column ? row[col_idx] : constant; }
};

struct Predicate {
    CompOp op = CompOp::EQ;
    Operand a, b, c;                        // c — верхняя граница BETWEEN
    DataType type = DataType::INT;          // общий тип сравнения
    std::shared_ptr<std::regex> regex;      // для LIKE с константным шаблоном

    bool eval(const Row& row) const;
};

class Table {
private:
    using IndexMap = std::unordered_map<std::string, std::unique_ptr<dbms::IndexManager>>;
    enum class RowAction { KEEP, MODIFIED, DELETE_ROW };

    std::string name_;
    std::string dir_;
    std::string data_path_;
    std::string meta_path_;
    TableMeta meta_;
    IndexMap indexes_;

    void saveMeta() const;
    void loadMeta();

    std::string indexPath(const std::string& column) const;
    IndexMap makeEmptyIndexes() const;
    void addToIndexes(IndexMap& map, const Row& row, uint64_t offset) const;
    void openIndexes();
    void rebuildIndexes();
    void saveIndexes() const;

    Operand makeOperand(const ParsedValue& pv) const;
    Predicate compile(const Condition& cond) const;
    bool planIndex(const Predicate& p, std::vector<FileOffset>& offsets) const;

    Field convertValue(const ColumnDefinition& col, const ParsedValue& pv) const;
    size_t rewrite(const std::function<RowAction(Row&)>& visitor);

public:
    Table(const std::string& name, const CreateTableStmt& stmt, const std::string& db_dir);
    Table(const std::string& name, const std::string& db_dir);
    ~Table();

    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;

    size_t insert(const std::vector<std::string>& columns,
                  const std::vector<std::vector<ParsedValue>>& rows);
    size_t remove(const Condition& where);
    size_t update(const std::vector<std::pair<std::string, ParsedValue>>& assignments,
                  const Condition& where);
    std::vector<Row> select(const Condition* where) const;

    const TableMeta& getMeta() const { return meta_; }
    std::vector<std::string> filePaths() const;
};
