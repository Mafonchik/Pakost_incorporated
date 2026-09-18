#pragma once

#include "lexer.h" // Подключаем лексер, где объявлены TokenType и Token
#include <vector>
#include <string>
#include <utility>
#include <stdexcept>

// Значение поля в запросе
struct ParsedValue {
    TokenType type = TokenType::NUMBER;
    std::string value;
    bool is_string = false;
    bool is_null = false;
};

// Определение колонки при создании таблицы
struct ColumnDef {
    std::string name;
    TokenType type; // TokenType::TYPE_INT или TokenType::TYPE_STRING
    bool is_not_null = false;
    bool is_indexed = false;
    bool has_default = false;
    ParsedValue default_value;
};

// AST (Abstract Syntax Tree) структуры для разных типов запросов
struct CreateTableStmt {
    std::string table_name;
    std::vector<ColumnDef> columns;
};

struct CreateDatabaseStmt {
    std::string db_name;
};

struct DropDatabaseStmt {
    std::string db_name;
};

struct InsertStmt {
    std::string table_name;
    std::vector<std::string> columns;
    std::vector<std::vector<ParsedValue>> rows;
};

struct UseDbStmt {
    std::string db_name;
};

// Условия WHERE (поддерживает и равенство, и BETWEEN)
struct Condition {
    std::string column;
    std::string op;          // Например, "=", "!=", "<", ">", etc.
    ParsedValue value;       // Для обычного сравнения
    ParsedValue lower;       // Нижняя граница для BETWEEN
    ParsedValue upper;       // Верхняя граница для BETWEEN
    bool is_between = false;
    bool has_where = false;
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
    Condition where;
    bool has_where = false;
};

struct DeleteStmt {
    std::string table_name;
    Condition where;
    bool has_where = false;
};

struct DropTableStmt {
    std::string table_name;
};

// Класс синтаксического анализатора (Parser)
class Parser {
private:
    std::vector<Token> tokens;
    size_t pos;

    // Вспомогательные методы навигации по токенам
    Token peek() const {
        return pos < tokens.size() ? tokens[pos] : Token{TokenType::END_OF_FILE, ""};
    }

    Token advance() {
        if (pos < tokens.size()) pos++;
        return tokens[pos - 1];
    }

    bool isAtEnd() const {
        return peek().type == TokenType::END_OF_FILE;
    }

    Token consume(TokenType expected_type, const std::string& error_message) {
        if (peek().type == expected_type) {
            return advance();
        }
        throw std::runtime_error("Синтаксическая ошибка: " + error_message + " Получено: '" + peek().value + "'.");
    }

    std::string parseQualifiedName() {
        std::string result = consume(TokenType::IDENTIFIER, "Ожидалось имя сущности.").value;
        if (peek().type == TokenType::DOT) {
            advance();
            result += "." + consume(TokenType::IDENTIFIER, "Ожидалось имя после '.'.").value;
        }
        return result;
    }

    ParsedValue parseValue() {
        Token token = peek();
        if (token.type == TokenType::NUMBER || 
            token.type == TokenType::STRING_LITERAL || 
            token.type == TokenType::IDENTIFIER || 
            token.type == TokenType::NULL_VAL) {
            advance();
            ParsedValue value;
            value.type = token.type;
            value.value = token.value;
            value.is_string = (token.type == TokenType::STRING_LITERAL || token.type == TokenType::IDENTIFIER);
            value.is_null = (token.type == TokenType::NULL_VAL);
            return value;
        }
        throw std::runtime_error("Синтаксическая ошибка: ожидалось значение, получено '" + token.value + "'.");
    }

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