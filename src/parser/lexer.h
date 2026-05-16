#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <cctype>

// Перечисление всех возможных типов токенов
enum class TokenType {
    // Ключевые слова
    CREATE, DATABASE, DROP, USE, TABLE,
    INSERT, INTO, VALUES, UPDATE, SET, DELETE, FROM,
    SELECT, WHERE, AS, 
    
    // Типы данных и модификаторы
    TYPE_INT, TYPE_STRING, NOT_NULL, INDEXED,

    // Идентификаторы и значения
    IDENTIFIER,       // Имена таблиц, баз, колонок
    STRING_LITERAL,   // Строки в кавычках: "hello"
    NUMBER,           // Числа: 42
    
    // Операторы
    OP_EQUAL,         // ==
    OP_NOT_EQUAL,     // !=
    OP_LESS,          // <
    OP_GREATER,       // >
    OP_LESS_EQ,       // <=
    OP_GREATER_EQ,    // >=
    
    // Пунктуация
    PAREN_LEFT,       // (
    PAREN_RIGHT,      // )
    COMMA,            // ,
    DOT,              // .
    ASTERISK,         // *
    SEMICOLON,        // ;
    
    END_OF_FILE       // Конец запроса
};

// Структура токена
struct Token {
    TokenType type;
    std::string value;
};

// Класс лексического анализатора
class Lexer {
private:
    std::string input;
    size_t pos;

    // Вспомогательные методы для чтения символов
    char peek() const { return pos < input.length() ? input[pos] : '\0'; }
    char advance() { return pos < input.length() ? input[pos++] : '\0'; }
    bool isAtEnd() const { return pos >= input.length(); }
    
    void skipWhitespace() {
        while (!isAtEnd() && std::isspace(peek())) {
            advance();
        }
    }

    // Проверка слова на смешение регистров (ТЗ)
    bool isValidCasing(const std::string& word) {
        bool all_upper = true;
        bool all_lower = true;
        for (char c : word) {
            if (std::isalpha(c)) {
                if (std::islower(c)) all_upper = false;
                if (std::isupper(c)) all_lower = false;
            }
        }
        return all_upper || all_lower;
    }

public:
    Lexer(const std::string& source) : input(source), pos(0) {}

    // Главный метод: разбивает всю строку на токены
    std::vector<Token> tokenize(); 
};