#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/table_file_manager.h"
#include "core/executor.h"

// Создаем глобальный экземпляр исполнителя запросов для текущей сессии
QueryExecutor executor;

void executeCommand(const std::string& query) {
    try {
        Lexer lexer(query);
        std::vector<Token> tokens = lexer.tokenize();

        if (tokens.empty() || tokens[0].type == TokenType::END_OF_FILE) {
            return;
        }

        Parser parser(tokens);
        Token first_token = tokens[0];

        if (first_token.type == TokenType::CREATE) {
            CreateTableStmt stmt = parser.parseCreateTable();
            executor.executeCreateTable(stmt);

        } else if (first_token.type == TokenType::INSERT) {
            InsertStmt stmt = parser.parseInsert();
            executor.executeInsert(stmt);
        
        } else if (first_token.type == TokenType::SELECT) {
            SelectStmt stmt = parser.parseSelect();
            executor.executeSelect(stmt);

        } else if (first_token.type == TokenType::DROP) {
            DropTableStmt stmt = parser.parseDropTable();
            executor.executeDropTable(stmt);
        
        } else if (first_token.type == TokenType::USE) {
            UseDbStmt stmt = parser.parseUse();
            executor.executeUse(stmt);
        
        } else {
            std::cout << "[ОШИБКА] Неизвестная или неподдерживаемая команда.\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "[ОШИБКА]: " << e.what() << "\n";
    }
}

// Пакетный режим: выполнение команд из текстового файла
void runBatchMode(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[ОШИБКА]: Не удалось открыть файл скрипта: " << filename << "\n";
        return;
    }

    std::string line;
    std::string full_query;
    
    while (std::getline(file, line)) {
        full_query += line + " ";
        // Если нашли точку с запятой, значит команда закончилась — выполняем
        if (line.find(';') != std::string::npos) {
            std::cout << "SQL> " << full_query << "\n";
            executeCommand(full_query);
            full_query.clear();
        }
    }
}

// Интерактивный режим: ввод команд прямо в консоли
void runInteractiveMode() {
    std::string query;
    std::cout << "Добро пожаловать в консоль СУБД\n";
    std::cout << "Введите команду или 'EXIT' для выхода.\n";

    while (true) {
        std::cout << "DBMS> ";
        if (!std::getline(std::cin, query)) break;

        // Команда выхода
        if (query == "EXIT" || query == "exit") {
            std::cout << "Выход...\n";
            break;
        }

        if (query.empty()) continue;

        executeCommand(query);
    }
}

int main(int argc, char* argv[]) {
    // Если передан аргумент командной строки — запускаем пакетный режим обработки файла
    if (argc > 1) {
        std::string script_file = argv[1];
        std::cout << "Запуск в пакетном режиме с файлом: " << script_file << "\n";
        runBatchMode(script_file);
    } else {
        // Иначе открываем интерактивную консоль
        runInteractiveMode();
    }

    return 0;
}