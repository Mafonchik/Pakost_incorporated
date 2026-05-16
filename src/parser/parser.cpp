#include "parser.h"
#include <iostream>
#include <unordered_set> // Добавлено для проверки дубликатов колонок

CreateTableStmt Parser::parseCreateTable() {
    CreateTableStmt stmt;
    std::unordered_set<std::string> seen_columns; // Множество для контроля уникальности имен

    // 1. Ожидаем ключевые слова CREATE и TABLE
    consume(TokenType::CREATE, "Ожидалось ключевое слово 'CREATE'.");
    consume(TokenType::TABLE, "Ожидалось ключевое слово 'TABLE'.");

    // 2. Имя таблицы
    Token name_token = consume(TokenType::IDENTIFIER, "Ожидалось имя таблицы.");
    stmt.table_name = name_token.value;

    // 3. Открывающая круглая скобка
    consume(TokenType::PAREN_LEFT, "Ожидалась '(' после имени таблицы.");

    // 4. Парсинг колонок (как минимум одна должна быть)
    do {
        ColumnDef col;
        
        // Имя колонки
        Token col_name = consume(TokenType::IDENTIFIER, "Ожидалось имя колонки.");
        col.name = col_name.value;

        // Проверяем, встречалась ли уже такая колонка в текущей таблице
        if (seen_columns.find(col.name) != seen_columns.end()) {
            throw std::runtime_error("Ошибка семантики: дублирование имени колонки '" + col.name + "' в таблице '" + stmt.table_name + "'.");
        }
        seen_columns.insert(col.name);

        // Тип колонки (INT или STRING)
        Token col_type = advance();
        if (col_type.type == TokenType::TYPE_INT || col_type.type == TokenType::TYPE_STRING) {
            col.type = col_type.type;
        } else {
            throw std::runtime_error("Неизвестный тип данных для колонки '" + col.name + "'. Ожидался INT или STRING.");
        }

        // Модификаторы (NOT_NULL, INDEXED). Они опциональны.
        if (peek().type == TokenType::NOT_NULL) {
            col.is_not_null = true;
            advance();
        } else if (peek().type == TokenType::INDEXED) {
            col.is_indexed = true;
            // По ТЗ INDEXED уникально и не может быть NULL
            col.is_not_null = true; 
            advance();
        }

        stmt.columns.push_back(col);

        // Если следующий токен — запятая, значит будет еще одна колонка.
    } while (peek().type == TokenType::COMMA && advance().type == TokenType::COMMA);

    // 5. Закрывающая круглая скобка
    consume(TokenType::PAREN_RIGHT, "Ожидалась ')' в конце определения колонок.");

    // 6. Точка с запятой (согласно ТЗ все команды завершаются ';')
    consume(TokenType::SEMICOLON, "Ожидалась ';' в конце команды.");

    return stmt;
}

InsertStmt Parser::parseInsert() {
    InsertStmt stmt;

    // 1. Ожидаем 'INSERT', 'INTO' и имя таблицы
    consume(TokenType::INSERT, "Ожидалось ключевое слово 'INSERT'.");
    consume(TokenType::INTO, "Ожидалось ключевое слово 'INTO'.");

    Token name_token = consume(TokenType::IDENTIFIER, "Ожидалось имя таблицы.");
    stmt.table_name = name_token.value;

    // 2. Ожидаем ключевое слово 'VALUES' и открывающую скобку
    consume(TokenType::VALUES, "Ожидалось ключевое слово 'VALUES'.");
    consume(TokenType::PAREN_LEFT, "Ожидалась '(' после VALUES.");

    // 3. Считываем переданные значения
    do {
        Token val_token = advance();
        
        // Значение должно быть либо числом, либо строкой в кавычках
        if (val_token.type == TokenType::NUMBER || val_token.type == TokenType::STRING_LITERAL) {
            stmt.values.push_back({val_token.type, val_token.value});
        } else {
            throw std::runtime_error("Синтаксическая ошибка: ожидалось число или строка внутри VALUES, получено '" + val_token.value + "'.");
        }

        // Проверяем, есть ли запятая. Если да — идем на следующий круг цикла.
    } while (peek().type == TokenType::COMMA && advance().type == TokenType::COMMA);

    // 4. Закрывающая скобка и точка с запятой
    consume(TokenType::PAREN_RIGHT, "Ожидалась ')' в конце списка значений.");
    consume(TokenType::SEMICOLON, "Ожидалась ';' в конце команды.");

    return stmt;
}

SelectStmt Parser::parseSelect() {
    consume(TokenType::SELECT, "Ожидалось ключевое слово SELECT");
    
    if (peek().type == TokenType::ASTERISK) {
        advance();
    } // (Позже здесь добавим парсинг списка колонок)

    consume(TokenType::FROM, "Ожидалось ключевое слово FROM");
    
    Token table_token = consume(TokenType::IDENTIFIER, "Ожидалось имя таблицы");
    std::string table_name = table_token.value;

    consume(TokenType::WHERE, "Ожидалось ключевое слово WHERE");
    
    Token col_token = consume(TokenType::IDENTIFIER, "Ожидалось имя колонки для поиска");
    std::string column_name = col_token.value;

    // Ловим любой оператор сравнения из тех, что умеет лексер
    Token op_token = peek();
    if (op_token.type == TokenType::OP_EQUAL || 
        op_token.type == TokenType::OP_NOT_EQUAL || 
        op_token.type == TokenType::OP_LESS || 
        op_token.type == TokenType::OP_GREATER || 
        op_token.type == TokenType::OP_LESS_EQ || 
        op_token.type == TokenType::OP_GREATER_EQ) {
        advance();
    } else {
        throw std::runtime_error("Синтаксическая ошибка: ожидался оператор сравнения (=, !=, <, >, <=, >=)");
    }

    Token val_token = consume(TokenType::NUMBER, "Ожидалось числовое значение");
    int value = std::stoi(val_token.value);

    if (peek().type == TokenType::SEMICOLON) {
        advance();
    }

    return SelectStmt{table_name, column_name, op_token.type, value};
}

DropTableStmt Parser::parseDropTable() {
    // Ожидаем: DROP TABLE table_name;
    
    consume(TokenType::DROP, "Ожидалось ключевое слово DROP");
    consume(TokenType::TABLE, "Ожидалось ключевое слово TABLE");
    
    Token table_token = consume(TokenType::IDENTIFIER, "Ожидалось имя таблицы");
    std::string table_name = table_token.value;

    if (peek().type == TokenType::SEMICOLON) {
        advance();
    }

    return DropTableStmt{table_name};
}

UseDbStmt Parser::parseUse() {
    consume(TokenType::USE, "Ожидалось ключевое слово USE");
    
    Token db_token = consume(TokenType::IDENTIFIER, "Ожидалось имя базы данных");
    std::string db_name = db_token.value;

    if (peek().type == TokenType::SEMICOLON) {
        advance();
    }

    return UseDbStmt{db_name};
}