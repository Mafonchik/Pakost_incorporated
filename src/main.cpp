#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include "parser/lexer.h"
#include "parser/parser.h"
#include "core/executor.h"
#include "logger.h"

namespace {

constexpr int CODE_OK = 0;
constexpr int CODE_SYNTAX_ERROR = 1;
constexpr int CODE_EXEC_ERROR = 2;
constexpr int CODE_INTERNAL_ERROR = 3;

constexpr uint32_t CLIENT_ID = 1;
constexpr uint32_t WORKER_ID = 1;

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::function<void()> compileStatement(QueryExecutor& ex, const std::string& text) {
    Lexer lexer(text);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    TokenType first = tokens[0].type;
    TokenType second = tokens.size() > 1 ? tokens[1].type : TokenType::END_OF_FILE;

    if (first == TokenType::CREATE && second == TokenType::DATABASE) {
        auto s = parser.parseCreateDatabase();
        return [&ex, s] { ex.executeCreateDatabase(s); };
    }
    if (first == TokenType::CREATE && second == TokenType::TABLE) {
        auto s = parser.parseCreateTable();
        return [&ex, s] { ex.executeCreateTable(s); };
    }
    if (first == TokenType::DROP && second == TokenType::DATABASE) {
        auto s = parser.parseDropDatabase();
        return [&ex, s] { ex.executeDropDatabase(s); };
    }
    if (first == TokenType::DROP && second == TokenType::TABLE) {
        auto s = parser.parseDropTable();
        return [&ex, s] { ex.executeDropTable(s); };
    }
    if (first == TokenType::CREATE || first == TokenType::DROP) {
        throw std::runtime_error("Синтаксическая ошибка: после " + tokens[0].value + " ожидалось DATABASE или TABLE.");
    }
    if (first == TokenType::USE) {
        auto s = parser.parseUse();
        return [&ex, s] { ex.executeUse(s); };
    }
    if (first == TokenType::INSERT) {
        auto s = parser.parseInsert();
        return [&ex, s] { ex.executeInsert(s); };
    }
    if (first == TokenType::SELECT) {
        auto s = parser.parseSelect();
        return [&ex, s] { ex.executeSelect(s); };
    }
    if (first == TokenType::UPDATE) {
        auto s = parser.parseUpdate();
        return [&ex, s] { ex.executeUpdate(s); };
    }
    if (first == TokenType::DELETE) {
        auto s = parser.parseDelete();
        return [&ex, s] { ex.executeDelete(s); };
    }
    throw std::runtime_error("Синтаксическая ошибка: неизвестная или неподдерживаемая команда.");
}

void processStatement(QueryExecutor& executor, AccessLogger& logger, const std::string& raw) {
    std::string text = trim(raw);
    if (text.empty()) return;

    auto start = std::chrono::system_clock::now();
    int code = CODE_OK;
    std::string status = "OK";

    try {
        std::function<void()> action;
        try {
            action = compileStatement(executor, text);
        } catch (const std::exception& e) {
            code = CODE_SYNTAX_ERROR;
            throw;
        }
        try {
            action();
        } catch (const std::exception& e) {
            code = CODE_EXEC_ERROR;
            throw;
        }
    } catch (const std::exception& e) {
        status = e.what();
        std::cerr << "ОШИБКА: " << e.what() << "\n";
    } catch (...) {
        code = CODE_INTERNAL_ERROR;
        status = "unknown error";
        std::cerr << "ОШИБКА: Внутренняя ошибка.\n";
    }

    logger.log(text, CLIENT_ID, WORKER_ID, start, std::chrono::system_clock::now(), code, status);
}

class StatementReader {
    std::string buf_;
    bool in_string_ = false;

public:
    void feed(const std::string& line, const std::function<void(const std::string&)>& on_statement) {
        for (char c : line) {
            buf_ += c;
            if (c == '"') {
                in_string_ = !in_string_;
            } else if (c == ';' && !in_string_) {
                on_statement(buf_);
                buf_.clear();
            }
        }
        buf_ += '\n';
    }

    bool hasPending() const { return !trim(buf_).empty(); }
    const std::string& pending() const { return buf_; }
};

bool isExitCommand(const std::string& line) {
    std::string t = trim(line);
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
    return t == "exit" || t == "exit;";
}

void runStream(std::istream& in, QueryExecutor& executor, AccessLogger& logger, bool interactive) {
    StatementReader reader;
    std::string line;
    auto handler = [&](const std::string& stmt) { processStatement(executor, logger, stmt); };

    while (std::getline(in, line)) {
        if (interactive && !reader.hasPending() && isExitCommand(line)) {
            std::cout << "Выход из программы...\n";
            return;
        }
        reader.feed(line, handler);
    }

    if (reader.hasPending()) {
        std::cerr << "ОШИБКА: Команда не завершена символом ';': " << trim(reader.pending()) << "\n";
    }
}

}

int main(int argc, char* argv[]) {
    AccessLogger logger("access.log");
    QueryExecutor executor;

    if (argc > 1) {
        std::ifstream file(argv[1]);
        if (!file.is_open()) {
            std::cerr << "ОШИБКА: Не удалось открыть файл: " << argv[1] << "\n";
            return 1;
        }
        runStream(file, executor, logger, false);
        return 0;
    }

    std::cout << "Введите SQL-запросы (команды завершаются ';', можно писать в несколько строк). Для выхода: EXIT;\n\n";
    runStream(std::cin, executor, logger, true);
    return 0;
}
