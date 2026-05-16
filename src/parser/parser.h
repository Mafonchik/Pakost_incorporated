#pragma once
#include "lexer.h"
#include <vector>
#include <string>
#include <stdexcept>

// Описание одной колонки таблицы
struct ColumnDef {
    std::string name;
    TokenType type;          // Ожидаем TYPE_INT или TYPE_STRING
    bool is_not_null = false;
    bool is_indexed = false;
};

// Описание всей команды CREATE TABLE
struct CreateTableStmt {
    std::string table_name;
    std::vector<ColumnDef> columns;
};

// Структура для хранения одного значения из VALUES
struct ParsedValue {
    TokenType type;      // Ожидаем NUMBER или STRING_LITERAL
    std::string value;   // Само значение в виде текста
};

// Описание команды INSERT INTO
struct InsertStmt {
    std::string table_name;
    std::vector<ParsedValue> values;
};

struct UseDbStmt {
    std::string db_name;
};

struct SelectStmt {
    std::string table_name;
    std::string column_name; // Какую колонку фильтруем (например, "id")
    TokenType op_type;       // Тип оператора (OP_EQUAL, OP_LESS, OP_GREATER и т.д.)
    int search_key; // пока сделаем поиск по точному целочисленному ключу (id)
};

// В класс Parser нужно добавить объявление нового метода:
// InsertStmt parseInsert();

struct DropTableStmt {
    std::string table_name;
};

// Класс Парсера
class Parser {
private:
    std::vector<Token> tokens;
    size_t pos;

    // Вспомогательные методы для навигации по токенам
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

    // Проверяет текущий токен и "съедает" его, если тип совпадает.
    // Иначе выбрасывает синтаксическую ошибку (согласно ТЗ валидация обязательна)
    Token consume(TokenType expected_type, const std::string& error_message) {
        if (peek().type == expected_type) {
            return advance();
        }
        throw std::runtime_error("Синтаксическая ошибка: " + error_message + 
                                 " Получено: '" + peek().value + "'");
    }

public:
    Parser(const std::vector<Token>& token_list) : tokens(token_list), pos(0) {}

    // Метод парсинга CREATE TABLE
    CreateTableStmt parseCreateTable();
    InsertStmt parseInsert();
    SelectStmt parseSelect();
    UseDbStmt parseUse();
    DropTableStmt parseDropTable();
};