#include "lexer.h"
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <stdexcept>

static const std::unordered_map<std::string, TokenType> keywords = {
    {"CREATE", TokenType::CREATE},
    {"DATABASE", TokenType::DATABASE},
    {"DROP", TokenType::DROP},
    {"USE", TokenType::USE},
    {"TABLE", TokenType::TABLE},
    {"INSERT", TokenType::INSERT},
    {"INTO", TokenType::INTO},
    {"VALUE", TokenType::VALUE},
    {"VALUES", TokenType::VALUES},
    {"UPDATE", TokenType::UPDATE},
    {"SET", TokenType::SET},
    {"DELETE", TokenType::DELETE},
    {"FROM", TokenType::FROM},
    {"SELECT", TokenType::SELECT},
    {"WHERE", TokenType::WHERE},
    {"AS", TokenType::AS},
    {"BETWEEN", TokenType::BETWEEN},
    {"AND", TokenType::AND},
    {"LIKE", TokenType::LIKE},
    {"DEFAULT", TokenType::DEFAULT},
    {"INT", TokenType::TYPE_INT},
    {"STRING", TokenType::TYPE_STRING},
    {"NOT_NULL", TokenType::NOT_NULL},
    {"INDEXED", TokenType::INDEXED},
    {"NULL", TokenType::NULL_VAL}
};

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (!isAtEnd()) {
        skipWhitespace();
        if (isAtEnd()) break;

        char c = peek();

        if (c == '(') { tokens.push_back(Token{TokenType::PAREN_LEFT, "("}); advance(); continue; }
        if (c == ')') { tokens.push_back(Token{TokenType::PAREN_RIGHT, ")"}); advance(); continue; }
        if (c == ',') { tokens.push_back(Token{TokenType::COMMA, ","}); advance(); continue; }
        if (c == '.') { tokens.push_back(Token{TokenType::DOT, "."}); advance(); continue; }
        if (c == '*') { tokens.push_back(Token{TokenType::ASTERISK, "*"}); advance(); continue; }
        if (c == ';') { tokens.push_back(Token{TokenType::SEMICOLON, ";"}); advance(); continue; }

        if (c == '=') {
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back(Token{TokenType::OP_EQUAL, "=="});
            } else {
                tokens.push_back(Token{TokenType::ASSIGN, "="});
            }
            continue;
        }

        if (c == '!') {
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back(Token{TokenType::OP_NOT_EQUAL, "!="});
            } else {
                throw std::runtime_error("Лексическая ошибка: ожидалось '=' после '!'");
            }
            continue;
        }

        if (c == '<') {
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back(Token{TokenType::OP_LESS_EQ, "<="});
            } else {
                tokens.push_back(Token{TokenType::OP_LESS, "<"});
            }
            continue;
        }

        if (c == '>') {
            advance();
            if (peek() == '=') {
                advance();
                tokens.push_back(Token{TokenType::OP_GREATER_EQ, ">="});
            } else {
                tokens.push_back(Token{TokenType::OP_GREATER, ">"});
            }
            continue;
        }

        if (c == '-') {
            advance();
            if (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                std::string num_str = "-";
                while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                    num_str += advance();
                }
                tokens.push_back(Token{TokenType::NUMBER, num_str});
                continue;
            }
            throw std::runtime_error("Лексическая ошибка: одиночный символ '-' не поддерживается.");
        }

        if (c == '"') {
            advance();
            std::string str_val;
            while (!isAtEnd() && peek() != '"') {
                str_val += advance();
            }
            if (isAtEnd()) {
                throw std::runtime_error("Лексическая ошибка: незакрытая строка, ожидалась '\"'.");
            }
            advance();
            tokens.push_back(Token{TokenType::STRING_LITERAL, str_val});
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string num_str;
            while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                num_str += advance();
            }
            tokens.push_back(Token{TokenType::NUMBER, num_str});
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string word;
            while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
                word += advance();
            }

            if (!isValidCasing(word)) {
                throw std::runtime_error("Лексическая ошибка: недопустимое смешение регистров в '" + word + "'.");
            }

            std::string upper_word = word;
            std::transform(upper_word.begin(), upper_word.end(), upper_word.begin(), [](unsigned char ch) {
                return static_cast<char>(std::toupper(ch));
            });

            auto it = keywords.find(upper_word);
            if (it != keywords.end()) {
                tokens.push_back(Token{it->second, upper_word});
            } else {
                tokens.push_back(Token{TokenType::IDENTIFIER, word});
            }
            continue;
        }

        throw std::runtime_error(std::string("Лексическая ошибка: неизвестный символ '") + c + "'.");
    }

    tokens.push_back(Token{TokenType::END_OF_FILE, ""});
    return tokens;
}