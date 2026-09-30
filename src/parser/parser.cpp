#include "parser.h"
#include <unordered_set>

std::string Parser::parseQualifiedName() {
    std::string result = consume(TokenType::IDENTIFIER, "Ожидалось имя таблицы.").value;
    if (peek().type == TokenType::DOT) {
        advance();
        result += "." + consume(TokenType::IDENTIFIER, "Ожидалось имя таблицы после '.'.").value;
    }
    return result;
}

ParsedValue Parser::parseValue() {
    Token token = peek();
    if (token.type == TokenType::NUMBER ||
        token.type == TokenType::STRING_LITERAL ||
        token.type == TokenType::NULL_VAL) {
        advance();
        ParsedValue value;
        value.type = token.type;
        value.value = token.value;
        return value;
    }
    std::string got = token.type == TokenType::END_OF_FILE ? "конец команды" : "'" + token.value + "'";
    throw std::runtime_error("Синтаксическая ошибка: ожидалось значение (число, строка в кавычках или NULL), получено " + got + ".");
}

ParsedValue Parser::parseOperand() {
    if (peek().type == TokenType::IDENTIFIER) {
        ParsedValue value;
        value.type = TokenType::IDENTIFIER;
        value.value = advance().value;
        return value;
    }
    return parseValue();
}

Condition Parser::parseCondition() {
    Condition cond;
    cond.left = parseOperand();

    Token op = peek();
    switch (op.type) {
        case TokenType::OP_EQUAL:
        case TokenType::ASSIGN:        cond.op = CompOp::EQ; break;   // '=' и '==' эквивалентны
        case TokenType::OP_NOT_EQUAL:  cond.op = CompOp::NE; break;
        case TokenType::OP_LESS:       cond.op = CompOp::LT; break;
        case TokenType::OP_GREATER:    cond.op = CompOp::GT; break;
        case TokenType::OP_LESS_EQ:    cond.op = CompOp::LE; break;
        case TokenType::OP_GREATER_EQ: cond.op = CompOp::GE; break;
        case TokenType::BETWEEN:       cond.op = CompOp::BETWEEN; break;
        case TokenType::LIKE:          cond.op = CompOp::LIKE; break;
        default:
            throw std::runtime_error("Синтаксическая ошибка: ожидался оператор (==, !=, <, >, <=, >=, BETWEEN, LIKE).");
    }
    advance();

    cond.right = parseOperand();
    if (cond.op == CompOp::BETWEEN) {
        consume(TokenType::AND, "Ожидалось ключевое слово 'AND' после нижней границы.");
        cond.upper = parseOperand();
    }
    return cond;
}

CreateDatabaseStmt Parser::parseCreateDatabase() {
    consume(TokenType::CREATE, "Ожидалось 'CREATE'.");
    consume(TokenType::DATABASE, "Ожидалось 'DATABASE'.");
    CreateDatabaseStmt stmt;
    stmt.db_name = consume(TokenType::IDENTIFIER, "Ожидалось имя базы данных.").value;
    consumeEnd();
    return stmt;
}

DropDatabaseStmt Parser::parseDropDatabase() {
    consume(TokenType::DROP, "Ожидалось 'DROP'.");
    consume(TokenType::DATABASE, "Ожидалось 'DATABASE'.");
    DropDatabaseStmt stmt;
    stmt.db_name = consume(TokenType::IDENTIFIER, "Ожидалось имя базы данных.").value;
    consumeEnd();
    return stmt;
}

UseDbStmt Parser::parseUse() {
    consume(TokenType::USE, "Ожидалось 'USE'.");
    UseDbStmt stmt;
    stmt.db_name = consume(TokenType::IDENTIFIER, "Ожидалось имя базы данных.").value;
    consumeEnd();
    return stmt;
}

CreateTableStmt Parser::parseCreateTable() {
    CreateTableStmt stmt;
    std::unordered_set<std::string> seen_columns;

    consume(TokenType::CREATE, "Ожидалось 'CREATE'.");
    consume(TokenType::TABLE, "Ожидалось 'TABLE'.");
    stmt.table_name = parseQualifiedName();
    consume(TokenType::PAREN_LEFT, "Ожидалась '(' после имени таблицы.");

    do {
        ColumnDef column;
        column.name = consume(TokenType::IDENTIFIER, "Ожидалось имя колонки.").value;
        if (!seen_columns.insert(column.name).second) {
            throw std::runtime_error("Ошибка семантики: дублирование имени колонки '" + column.name + "'.");
        }

        Token type_token = peek();
        if (type_token.type != TokenType::TYPE_INT && type_token.type != TokenType::TYPE_STRING) {
            throw std::runtime_error("Синтаксическая ошибка: неизвестный тип данных у колонки '" + column.name + "' (допустимы INT и STRING).");
        }
        advance();
        column.type = type_token.type;

        while (peek().type == TokenType::NOT_NULL || peek().type == TokenType::INDEXED) {
            Token modifier = advance();
            if (modifier.type == TokenType::NOT_NULL) {
                column.is_not_null = true;
            } else {
                column.is_indexed = true;
                column.is_not_null = true;   // INDEXED => уникально и не NULL
            }
        }

        stmt.columns.push_back(column);

        if (peek().type == TokenType::COMMA) {
            advance();
            continue;
        }
        break;
    } while (true);

    consume(TokenType::PAREN_RIGHT, "Ожидалась ')' в конце определения колонок.");
    consumeEnd();
    return stmt;
}

DropTableStmt Parser::parseDropTable() {
    consume(TokenType::DROP, "Ожидалось 'DROP'.");
    consume(TokenType::TABLE, "Ожидалось 'TABLE'.");
    DropTableStmt stmt;
    stmt.table_name = parseQualifiedName();
    consumeEnd();
    return stmt;
}

InsertStmt Parser::parseInsert() {
    InsertStmt stmt;
    consume(TokenType::INSERT, "Ожидалось 'INSERT'.");
    consume(TokenType::INTO, "Ожидалось 'INTO'.");
    stmt.table_name = parseQualifiedName();

    if (peek().type == TokenType::PAREN_LEFT) {
        advance();
        do {
            stmt.columns.push_back(consume(TokenType::IDENTIFIER, "Ожидалось имя колонки.").value);
            if (peek().type == TokenType::COMMA) {
                advance();
                continue;
            }
            break;
        } while (true);
        consume(TokenType::PAREN_RIGHT, "Ожидалась ')' после списка колонок.");
    }

    if (peek().type == TokenType::VALUE || peek().type == TokenType::VALUES) {
        advance();
    } else {
        throw std::runtime_error("Синтаксическая ошибка: ожидалось 'VALUE' или 'VALUES'.");
    }

    do {
        consume(TokenType::PAREN_LEFT, "Ожидалась '(' перед значениями строки.");
        std::vector<ParsedValue> row;
        do {
            row.push_back(parseValue());
            if (peek().type == TokenType::COMMA) {
                advance();
                continue;
            }
            break;
        } while (true);
        consume(TokenType::PAREN_RIGHT, "Ожидалась ')' в конце значений строки.");
        stmt.rows.push_back(std::move(row));

        if (peek().type == TokenType::COMMA) {
            advance();
            continue;   // следующая строка — снова потребуется '('
        }
        break;
    } while (true);

    consumeEnd();
    return stmt;
}

SelectStmt Parser::parseSelect() {
    SelectStmt stmt;
    consume(TokenType::SELECT, "Ожидалось 'SELECT'.");

    if (peek().type == TokenType::ASTERISK) {
        advance();
        stmt.select_all = true;
    } else {
        do {
            stmt.columns.push_back(consume(TokenType::IDENTIFIER, "Ожидалось имя колонки.").value);
            if (peek().type == TokenType::AS) {
                advance();
                stmt.aliases.push_back(consume(TokenType::IDENTIFIER, "Ожидалось имя алиаса после AS.").value);
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

    consume(TokenType::FROM, "Ожидалось 'FROM'.");
    stmt.table_name = parseQualifiedName();

    if (peek().type == TokenType::WHERE) {
        advance();
        stmt.where = parseCondition();
        stmt.has_where = true;
    }

    consumeEnd();
    return stmt;
}

UpdateStmt Parser::parseUpdate() {
    UpdateStmt stmt;
    consume(TokenType::UPDATE, "Ожидалось 'UPDATE'.");
    stmt.table_name = parseQualifiedName();
    consume(TokenType::SET, "Ожидалось 'SET'.");

    do {
        std::string column_name = consume(TokenType::IDENTIFIER, "Ожидалось имя колонки.").value;
        consume(TokenType::ASSIGN, "Ожидалось '=' в выражении SET.");
        stmt.assignments.emplace_back(column_name, parseValue());
        if (peek().type == TokenType::COMMA) {
            advance();
            continue;
        }
        break;
    } while (true);

    consume(TokenType::WHERE, "Ожидалось 'WHERE' (UPDATE без условия не поддерживается).");
    stmt.where = parseCondition();
    consumeEnd();
    return stmt;
}

DeleteStmt Parser::parseDelete() {
    DeleteStmt stmt;
    consume(TokenType::DELETE, "Ожидалось 'DELETE'.");
    consume(TokenType::FROM, "Ожидалось 'FROM'.");
    stmt.table_name = parseQualifiedName();
    consume(TokenType::WHERE, "Ожидалось 'WHERE' (DELETE без условия не поддерживается).");
    stmt.where = parseCondition();
    consumeEnd();
    return stmt;
}
