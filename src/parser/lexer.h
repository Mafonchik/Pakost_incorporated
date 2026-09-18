#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <cctype>

enum class TokenType {
    CREATE, DATABASE, DROP, USE, TABLE,
    INSERT, INTO, VALUE, VALUES, UPDATE, SET, DELETE, FROM,
    SELECT, WHERE, AS, BETWEEN, AND, LIKE, DEFAULT,
    TYPE_INT, TYPE_STRING, NOT_NULL, INDEXED, NULL_VAL,
    IDENTIFIER,
    STRING_LITERAL,
    NUMBER,
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_LESS,
    OP_GREATER,
    OP_LESS_EQ,
    OP_GREATER_EQ,
    ASSIGN,
    PAREN_LEFT,
    PAREN_RIGHT,
    COMMA,
    DOT,
    ASTERISK,
    SEMICOLON,
    END_OF_FILE
};

struct Token {
    TokenType type;
    std::string value;
};

class Lexer {
private:
    std::string input;
    size_t pos;

    char peek() const { return pos < input.length() ? input[pos] : '\0'; }
    char advance() { return pos < input.length() ? input[pos++] : '\0'; }
    bool isAtEnd() const { return pos >= input.length(); }

    void skipWhitespace() {
        while (!isAtEnd() && std::isspace(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }

    bool isValidCasing(const std::string& word) {
        bool all_upper = true;
        bool all_lower = true;
        for (char c : word) {
            if (std::isalpha(static_cast<unsigned char>(c))) {
                if (std::islower(static_cast<unsigned char>(c))) all_upper = false;
                if (std::isupper(static_cast<unsigned char>(c))) all_lower = false;
            }
        }
        return all_upper || all_lower;
    }

public:
    explicit Lexer(const std::string& source) : input(source), pos(0) {}

    std::vector<Token> tokenize();
};