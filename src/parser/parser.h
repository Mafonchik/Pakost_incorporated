#pragma once

#include "lexer.h"
#include <vector>
#include <string>
#include <utility>
#include <stdexcept>

struct ParsedValue {
    TokenType type = TokenType::NUMBER;
    std::string value;

    bool isColumn() const { return type == TokenType::IDENTIFIER; }
    bool isNull() const { return type == TokenType::NULL_VAL; }
};

struct ColumnDef {
    std::string name;
    TokenType type = TokenType::TYPE_INT;
    bool is_not_null = false;
    bool is_indexed = false;              // INDEXED подразумевает NOT_NULL и уникальность
};

struct CreateTableStmt {
    std::string table_name;
    std::vector<ColumnDef> columns;
};

struct CreateDatabaseStmt { std::string db_name; };
struct DropDatabaseStmt   { std::string db_name; };
struct UseDbStmt          { std::string db_name; };
struct DropTableStmt      { std::string table_name; };

struct InsertStmt {
    std::string table_name;
    std::vector<std::string> columns;               // пусто => все столбцы по порядку
    std::vector<std::vector<ParsedValue>> rows;
};

enum class CompOp { EQ, NE, LT, GT, LE, GE, BETWEEN, LIKE };

struct Condition {
    CompOp op = CompOp::EQ;
    ParsedValue left;
    ParsedValue right;
    ParsedValue upper;
};

struct SelectStmt {
    std::string table_name;
    std::vector<std::string> columns;
    std::vector<std::string> aliases;
    bool select_all = false;
    Condition where;
    bool has_where = false;
};

struct UpdateStmt {
    std::string table_name;
    std::vector<std::pair<std::string, ParsedValue>> assignments;
    Condition where;                    // WHERE обязателен по грамматике
};

struct DeleteStmt {
    std::string table_name;
    Condition where;
};

class Parser {
private:
    std::vector<Token> tokens;
    size_t pos;

    Token peek() const {
        return pos < tokens.size() ? tokens[pos] : Token{TokenType::END_OF_FILE, ""};
    }

    Token advance() {
        if (pos < tokens.size()) pos++;
        return tokens[pos - 1];
    }

    Token consume(TokenType expected_type, const std::string& error_message) {
        if (peek().type == expected_type) {
            return advance();
        }
        std::string got = peek().type == TokenType::END_OF_FILE ? "конец команды" : "'" + peek().value + "'";
        throw std::runtime_error("Синтаксическая ошибка: " + error_message + " Получено: " + got + ".");
    }

    void consumeEnd() {
        consume(TokenType::SEMICOLON, "Ожидалась ';' в конце команды.");
        if (peek().type != TokenType::END_OF_FILE) {
            throw std::runtime_error("Синтаксическая ошибка: после ';' не должно быть других символов.");
        }
    }

    std::string parseQualifiedName();
    ParsedValue parseValue();     // только константа
    ParsedValue parseOperand();   // константа или столбец
    Condition parseCondition();

public:
    explicit Parser(const std::vector<Token>& token_list) : tokens(token_list), pos(0) {}

    CreateDatabaseStmt parseCreateDatabase();
    DropDatabaseStmt parseDropDatabase();
    CreateTableStmt parseCreateTable();
    InsertStmt parseInsert();
    SelectStmt parseSelect();
    UpdateStmt parseUpdate();
    DeleteStmt parseDelete();
    UseDbStmt parseUse();
    DropTableStmt parseDropTable();
};
