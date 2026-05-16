#include "lexer.h"
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <stdexcept>

// Словарь для быстрого поиска ключевых слов
static const std::unordered_map<std::string, TokenType> keywords = {
    {"CREATE", TokenType::CREATE}, {"DATABASE", TokenType::DATABASE},
    {"DROP", TokenType::DROP}, {"USE", TokenType::USE},
    {"TABLE", TokenType::TABLE}, {"INSERT", TokenType::INSERT},
    {"INTO", TokenType::INTO}, {"VALUES", TokenType::VALUES},
    {"UPDATE", TokenType::UPDATE}, {"SET", TokenType::SET},
    {"DELETE", TokenType::DELETE}, {"FROM", TokenType::FROM},
    {"SELECT", TokenType::SELECT}, {"WHERE", TokenType::WHERE},
    {"AS", TokenType::AS}, {"INT", TokenType::TYPE_INT},
    {"STRING", TokenType::TYPE_STRING}, {"NOT_NULL", TokenType::NOT_NULL},
    {"INDEXED", TokenType::INDEXED}
};

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (!isAtEnd()) {
        skipWhitespace();
        if (isAtEnd()) break;

        char c = peek();

        // 1. Пунктуация и скобки
        if (c == '(') { tokens.push_back(Token{TokenType::PAREN_LEFT, "("}); advance(); continue; }
        if (c == ')') { tokens.push_back(Token{TokenType::PAREN_RIGHT, ")"}); advance(); continue; }
        if (c == ',') { tokens.push_back(Token{TokenType::COMMA, ","}); advance(); continue; }
        if (c == '.') { tokens.push_back(Token{TokenType::DOT, "."}); advance(); continue; }
        if (c == '*') { tokens.push_back(Token{TokenType::ASTERISK, "*"}); advance(); continue; }
        if (c == ';') { tokens.push_back(Token{TokenType::SEMICOLON, ";"}); advance(); continue; }

        // 2. Операторы сравнения и минус / отрицательные числа
        if (c == '-') {
            // Проверяем: если за минусом сразу идет цифра, то это отрицательное число
            // (Используем метод класса getNextCharPeek() или аналог, если метод называется иначе, 
            // но в стандартном каркасе обычно проверяют следующий символ через смещение)
            // Здесь предположим, что у нас есть проверка следующего символа через peek_next() 
            // либо обрабатываем знак минус как число, если метод доступен. 
            // Если в классе нет peek_next(), сделаем безопасную проверку через временный сдвиг или стандартный подход.
            // Давай посмотрим: если метод peek() возвращает текущий, а следующий можно получить, 
            // но раз у нас стандартный интерфейс — внедрим чтение со знаком:
            
            advance(); // временно сдвигаем, чтобы проверить символ после минуса
            if (!isAtEnd() && std::isdigit(peek())) {
                std::string num_str = "-";
                while (!isAtEnd() && std::isdigit(peek())) {
                    num_str += advance();
                }
                tokens.push_back(Token{TokenType::NUMBER, num_str});
                continue;
            } else {
                // Если за минусом не цифра, откатываемся или выбрасываем ошибку (или это минус как символ)
                // Возвращаем указатель назад, если поддерживается, либо бросаем исключение, 
                // так как в данном синтаксисе одиночный минус пока не используется как арифметический оператор.
                throw std::runtime_error("Лексическая ошибка: одиночный символ '-' не поддерживается");
            }
        }

        if (c == '=') {
            advance();
            tokens.push_back(Token{TokenType::OP_EQUAL, "="});
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

        // 3. Строковые литералы (содержимое внутри двойных кавычек)
        if (c == '"') {
            advance(); // Пропускаем открывающую кавычку
            std::string str_val;
            
            while (!isAtEnd() && peek() != '"') {
                str_val += advance();
            }
            
            if (isAtEnd()) {
                throw std::runtime_error("Лексическая ошибка: незакрытая строка, ожидалась '\"'");
            }
            
            advance(); // Пропускаем закрывающую кавычку
            tokens.push_back(Token{TokenType::STRING_LITERAL, str_val});
            continue;
        }

        // 4. Положительные числа (собираем все цифры подряд)
        if (std::isdigit(c)) {
            std::string num_str;
            while (!isAtEnd() && std::isdigit(peek())) {
                num_str += advance();
            }
            tokens.push_back(Token{TokenType::NUMBER, num_str});
            continue;
        }

        // 5. Идентификаторы и ключевые слова (начинаются с буквы или подчеркивания)
        if (std::isalpha(c) || c == '_') {
            std::string word;
            while (!isAtEnd() && (std::isalnum(peek()) || peek() == '_')) {
                word += advance();
            }

            // Проверка строгого правила на смешение регистров
            if (!isValidCasing(word)) {
                throw std::runtime_error("Лексическая ошибка: недопустимое смешение регистров в '" + word + "'");
            }

            // Переводим слово в верхний регистр для проверки по словарю ключевых слов
            std::string upper_word = word;
            std::transform(upper_word.begin(), upper_word.end(), upper_word.begin(), ::toupper);

            std::unordered_map<std::string, TokenType>::const_iterator it = keywords.find(upper_word);
            if (it != keywords.end()) {
                // Если нашли в словаре — это ключевое слово (сохраняем в верхнем регистре)
                tokens.push_back(Token{it->second, upper_word});
            } else {
                // Если не нашли — это пользовательское имя (сохраняем оригинальный регистр)
                tokens.push_back(Token{TokenType::IDENTIFIER, word});
            }
            continue;
        }

        // 6. Обработка неизвестных символов
        throw std::runtime_error(std::string("Лексическая ошибка: неизвестный символ '") + c + "'");
    }

    // Маркер конца потока токенов
    tokens.push_back(Token{TokenType::END_OF_FILE, ""});
    return tokens;
}