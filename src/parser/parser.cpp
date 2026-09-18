#include "parser.h"
#include <unordered_set>
#include <vector>

CreateDatabaseStmt Parser::parseCreateDatabase() {
    consume(TokenType::CREATE, "Ожидалось ключевое слово 'CREATE'.");
    consume(TokenType::DATABASE, "Ожидалось ключевое слово 'DATABASE'.");
    CreateDatabaseStmt stmt;
    stmt.db_name = parseQualifiedName();
    consume(TokenType::SEMICOLON, "Ожидалась ';' в конце команды.");
    return stmt;
}

Condition Parser::parseCondition() {
    Condition condition;
    condition.column = consume(TokenType::IDENTIFIER, "Ожидалось имя столбца в условии.").value;

    Token op = peek();
    if (op.type == TokenType::OP_EQUAL || op.type == TokenType::ASSIGN || 
        op.type == TokenType::OP_NOT_EQUAL ||
        op.type == TokenType::OP_LESS || op.type == TokenType::OP_GREATER ||
        op.type == TokenType::OP_LESS_EQ || op.type == TokenType::OP_GREATER_EQ) {
        advance();
        condition.op = (op.type == TokenType::ASSIGN) ? "=" : op.value; // Нормализуем в "=" если пришел ASSIGN
        condition.value = parseValue();
        condition.has_where = true;
        return condition;
    }

    if (op.type == TokenType::BETWEEN) {
        advance();
        condition.op = "BETWEEN";
        condition.lower = parseValue();
        consume(TokenType::AND, "Ожидалось ключевое слово 'AND' после нижней границы.");
        condition.upper = parseValue();
        condition.is_between = true;
        condition.has_where = true;
        return condition;
    }

    if (op.type == TokenType::LIKE) {
        advance();
        condition.op = "LIKE";
        condition.value = parseValue();
        condition.has_where = true;
        return condition;
    }

    throw std::runtime_error("Синтаксическая ошибка: ожидался оператор сравнения, BETWEEN или LIKE.");
}

CreateTableStmt Parser::parseCreateTable() {
    CreateTableStmt stmt;
    std::unordered_set<std::string> seen_columns;

    consume(TokenType::CREATE, "Ожидалось ключевое слово 'CREATE'.");
    consume(TokenType::TABLE, "Ожидалось ключевое слово 'TABLE'.");
    stmt.table_name = parseQualifiedName();
    consume(TokenType::PAREN_LEFT, "Ожидалась '(' после имени таблицы.");

    // Читаем колонки в цикле
    do {
        ColumnDef column;
        column.name = consume(TokenType::IDENTIFIER, "Ожидалось имя колонки.").value;
        if (seen_columns.find(column.name) != seen_columns.end()) {
            throw std::runtime_error("Ошибка семантики: дублирование имени колонки '" + column.name + "'.");
        }
        seen_columns.insert(column.name);

        Token type_token = peek();
        if (type_token.type == TokenType::TYPE_INT || type_token.type == TokenType::TYPE_STRING) {
            advance();
            column.type = type_token.type;
        } else {
            throw std::runtime_error("Неизвестный тип данных для колонки '" + column.name + "'.");
        }

        while (peek().type == TokenType::NOT_NULL || peek().type == TokenType::INDEXED || peek().type == TokenType::DEFAULT) {
            Token modifier = advance();
            if (modifier.type == TokenType::NOT_NULL) {
                column.is_not_null = true;
            } else if (modifier.type == TokenType::INDEXED) {
                column.is_indexed = true;
                column.is_not_null = true;
            } else if (modifier.type == TokenType::DEFAULT) {
                column.has_default = true;
                column.default_value = parseValue();
            }
        }

        stmt.columns.push_back(column);

        // Если дальше идет запятая, значит, есть еще колонки
        if (peek().type == TokenType::COMMA) {
            advance();
            continue;
        } 
        break; // Если запятой нет, выходим из цикла колонок
    } while (true);

    consume(TokenType::PAREN_RIGHT, "Ожидалась ')' в конце определения колонок.");
    consume(TokenType::SEMICOLON, "Ожидалась ';' в конце команды.");
    return stmt;
}

DropDatabaseStmt Parser::parseDropDatabase() {
    consume(TokenType::DROP, "Ожидалось ключевое слово 'DROP'.");
    consume(TokenType::DATABASE, "Ожидалось ключевое слово 'DATABASE'.");
    DropDatabaseStmt stmt;
    stmt.db_name = consume(TokenType::IDENTIFIER, "Ожидалось имя базы данных.").value;
    consume(TokenType::SEMICOLON, "Ожидалась ';' в конце команды.");
    return stmt;
}

InsertStmt Parser::parseInsert() {
    InsertStmt stmt;
    consume(TokenType::INSERT, "Ожидалось ключевое слово 'INSERT'.");
    consume(TokenType::INTO, "Ожидалось ключевое слово 'INTO'.");
    stmt.table_name = parseQualifiedName();

    if (peek().type == TokenType::PAREN_LEFT) {
        advance();
        do {
            stmt.columns.push_back(consume(TokenType::IDENTIFIER, "Ожидалось имя колонки.").value);
            if (peek().type == TokenType::COMMA) {
                advance();
            } else {
                break;
            }
        } while (true);
        consume(TokenType::PAREN_RIGHT, "Ожидалась ')' после списка колонок.");
    }

    if (peek().type == TokenType::VALUE || peek().type == TokenType::VALUES) {
        advance();
    } else {
        throw std::runtime_error("Ожидалось ключевое слово 'VALUE' или 'VALUES'.");
    }

    do {
        consume(TokenType::PAREN_LEFT, "Ожидалась '(' после VALUE.");
        std::vector<ParsedValue> row;
        do {
            row.push_back(parseValue());
            if (peek().type == TokenType::COMMA) {
                advance();
                if (peek().type == TokenType::PAREN_RIGHT) {
                    break;
                }
                continue;
            }
            break;
        } while (true);
        consume(TokenType::PAREN_RIGHT, "Ожидалась ')' в конце значения строки.");
        stmt.rows.push_back(row);
        if (peek().type == TokenType::COMMA) {
            advance();
            if (peek().type == TokenType::PAREN_LEFT) {
                continue;
            }
            throw std::runtime_error("Ожидался новый набор значений после запятой.");
        }
        break;
    } while (true);

    consume(TokenType::SEMICOLON, "Ожидалась ';' в конце команды.");
    return stmt;
}

SelectStmt Parser::parseSelect() {
    SelectStmt stmt;
    consume(TokenType::SELECT, "Ожидалось ключевое слово SELECT.");

    if (peek().type == TokenType::ASTERISK) {
        advance();
        stmt.select_all = true;
        stmt.columns.push_back("*");
    } else {
        do {
            std::string column_name = consume(TokenType::IDENTIFIER, "Ожидалось имя колонки.").value;
            stmt.columns.push_back(column_name);
            if (peek().type == TokenType::AS) {
                advance();
                stmt.aliases.push_back(consume(TokenType::IDENTIFIER, "Ожидалось имя алиаса.").value);
            } else {
                stmt.aliases.push_back("");
            }
            if (peek().type == TokenType::COMMA) {
                advance();
                continue;
            }
            break;
        } while (true);
    }

    consume(TokenType::FROM, "Ожидалось ключевое слово FROM.");
    stmt.table_name = parseQualifiedName();

    if (peek().type == TokenType::WHERE) {
        advance();
        stmt.where = parseCondition();
        stmt.has_where = true;
    }

    consume(TokenType::SEMICOLON, "Ожидалась ';' в конце команды.");
    return stmt;
}

UpdateStmt Parser::parseUpdate() {
    UpdateStmt stmt;
    consume(TokenType::UPDATE, "Ожидалось ключевое слово UPDATE.");
    stmt.table_name = parseQualifiedName();
    consume(TokenType::SET, "Ожидалось ключевое слово SET.");

    do {
        std::string column_name = consume(TokenType::IDENTIFIER, "Ожидалось имя колонки.").value;
        consume(TokenType::ASSIGN, "Ожидалось '=' в выражении SET.");
        ParsedValue value = parseValue();
        stmt.assignments.emplace_back(column_name, value);
        if (peek().type == TokenType::COMMA) {
            advance();
            continue;
        }
        break;
    } while (true);

    if (peek().type == TokenType::WHERE) {
        advance();
        stmt.where = parseCondition();
        stmt.has_where = true;
    }

    consume(TokenType::SEMICOLON, "Ожидалась ';' в конце команды.");
    return stmt;
}

DeleteStmt Parser::parseDelete() {
    DeleteStmt stmt;
    consume(TokenType::DELETE, "Ожидалось ключевое слово DELETE.");
    consume(TokenType::FROM, "Ожидалось ключевое слово FROM.");
    stmt.table_name = parseQualifiedName();

    if (peek().type == TokenType::WHERE) {
        advance();
        stmt.where = parseCondition();
        stmt.has_where = true;
    }

    consume(TokenType::SEMICOLON, "Ожидалась ';' в конце команды.");
    return stmt;
}

UseDbStmt Parser::parseUse() {
    consume(TokenType::USE, "Ожидалось ключевое слово 'USE'.");
    UseDbStmt stmt;
    stmt.db_name = parseQualifiedName();
    consume(TokenType::SEMICOLON, "Ожидалась ';' в конце команды.");
    return stmt;
}

DropTableStmt Parser::parseDropTable() {
    consume(TokenType::DROP, "Ожидалось ключевое слово 'DROP'.");
    consume(TokenType::TABLE, "Ожидалось ключевое слово 'TABLE'.");
    DropTableStmt stmt;
    stmt.table_name = parseQualifiedName();
    consume(TokenType::SEMICOLON, "Ожидалась ';' в конце команды.");
    return stmt;
}