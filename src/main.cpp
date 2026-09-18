#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include "parser/lexer.h"
#include "parser/parser.h"
#include "core/executor.h"
#include "logger.h"

void processLine(QueryExecutor& executor, const std::string& line) {
    if (line.empty()) return;

    try {
        // 1. Лексический анализ
        Lexer lexer(line);
        auto tokens = lexer.tokenize();
        if (tokens.empty() || tokens[0].type == TokenType::END_OF_FILE) {
            return;
        }

        // 2. Синтаксический анализ
        Parser parser(tokens);
        TokenType first_type = tokens[0].type;

        if (first_type == TokenType::CREATE) {
            if (tokens.size() > 1 && tokens[1].type == TokenType::DATABASE) {
                CreateDatabaseStmt stmt = parser.parseCreateDatabase();
                executor.executeCreateDatabase(stmt);
            } else if (tokens.size() > 1 && tokens[1].type == TokenType::TABLE) {
                CreateTableStmt stmt = parser.parseCreateTable();
                executor.executeCreateTable(stmt);
            } else {
                throw std::runtime_error("Неизвестная команда после CREATE.");
            }
        } 
        else if (first_type == TokenType::DROP) {
            if (tokens.size() > 1 && tokens[1].type == TokenType::DATABASE) {
                DropDatabaseStmt stmt = parser.parseDropDatabase();
                executor.executeDropDatabase(stmt);
            } else if (tokens.size() > 1 && tokens[1].type == TokenType::TABLE) {
                DropTableStmt stmt = parser.parseDropTable();
                executor.executeDropTable(stmt);
            }
        }
        else if (first_type == TokenType::USE) {
            UseDbStmt stmt = parser.parseUse();
            executor.executeUse(stmt);
        }
        else if (first_type == TokenType::INSERT) {
            InsertStmt stmt = parser.parseInsert();
            executor.executeInsert(stmt);
        }
        else if (first_type == TokenType::SELECT) {
            SelectStmt stmt = parser.parseSelect();
            executor.executeSelect(stmt);
        }
        else if (first_type == TokenType::UPDATE) {
            UpdateStmt stmt = parser.parseUpdate();
            executor.executeUpdate(stmt);
        }
        else if (first_type == TokenType::DELETE) {
            DeleteStmt stmt = parser.parseDelete();
            executor.executeDelete(stmt);
        }
        else {
            throw std::runtime_error("Неизвестная или неподдерживаемая команда.");
        }

    } catch (const std::exception& e) {
        std::cerr << "[ОШИБКА] " << e.what() << "\n";
    }
}

int main(int argc, char* argv[]) {
    AccessLogger logger("access.log");
    QueryExecutor executor;

    uint32_t client_id = 1; // ID текущей сессии/клиента
    uint32_t worker_id = 1; // ID потока-обработчика

    // Режим 1: Выполнение SQL-скрипта из файла (аргумент командной строки)
    if (argc > 1) {
        std::ifstream file(argv[1]);
        if (!file.is_open()) {
            std::cerr << "[ОШИБКА] Не удалось открыть файл: " << argv[1] << "\n";
            return 1;
        }

        std::string line;
        while (std::getline(file, line)) {
            processLine(executor, line);
        }
        return 0;
    }

    // Режим 2: Интерактивный консольный режим
    std::cout << "Введите SQL-запросы. Для выхода введите EXIT;\n\n";

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "EXIT;" || line == "exit" || line == "EXIT") {
            std::cout << "Выход из программы...\n";
            break;
        }
        processLine(executor, line);
    }

    return 0;
}