#include "table.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

bool parseInt(const std::string& s, long long& out) {
    try {
        size_t pos = 0;
        out = std::stoll(s, &pos);
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

uint64_t fileSize(const std::string& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return 0;
    auto sz = fs::file_size(path, ec);
    return ec ? 0 : static_cast<uint64_t>(sz);
}

dbms::Key makeKey(const ColumnDefinition& col, const std::string& value) {
    if (col.type == DataType::INT) {
        return dbms::Key(static_cast<long long>(std::stoll(value)));
    }
    return dbms::Key(value);
}

std::string serializeRow(const Row& row) {
    std::string buf;
    uint32_t n = static_cast<uint32_t>(row.size());
    buf.append(reinterpret_cast<const char*>(&n), sizeof(n));
    for (const auto& f : row) {
        buf.push_back(f ? '\0' : '\1');
        if (f) {
            uint32_t len = static_cast<uint32_t>(f->size());
            buf.append(reinterpret_cast<const char*>(&len), sizeof(len));
            buf.append(*f);
        }
    }
    return buf;
}

bool readRow(std::istream& in, Row& row) {
    uint32_t n = 0;
    if (!in.read(reinterpret_cast<char*>(&n), sizeof(n))) return false;
    if (n > 4096) return false;   // защита от мусора
    row.assign(n, std::nullopt);
    for (uint32_t i = 0; i < n; ++i) {
        char is_null = 0;
        if (!in.get(is_null)) return false;
        if (is_null) continue;
        uint32_t len = 0;
        if (!in.read(reinterpret_cast<char*>(&len), sizeof(len))) return false;
        if (len > (1u << 30)) return false;
        std::string val(len, '\0');
        if (len > 0 && !in.read(&val[0], len)) return false;
        row[i] = std::move(val);
    }
    return true;
}

template <class F>
void scanFile(const std::string& path, F&& fn) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    Row row;
    while (true) {
        std::streamoff off = in.tellg();
        if (!readRow(in, row)) break;
        fn(static_cast<uint64_t>(off), row);
    }
}

void writeStr(std::ostream& out, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    out.write(reinterpret_cast<const char*>(&len), sizeof(len));
    out.write(s.data(), len);
}

bool readStr(std::istream& in, std::string& s) {
    uint32_t len = 0;
    if (!in.read(reinterpret_cast<char*>(&len), sizeof(len)) || len > 4096) return false;
    s.assign(len, '\0');
    return len == 0 || static_cast<bool>(in.read(&s[0], len));
}

int compareValues(const std::string& x, const std::string& y, DataType t) {
    if (t == DataType::INT) {
        long long a = std::stoll(x), b = std::stoll(y);
        return a < b ? -1 : (a > b ? 1 : 0);
    }
    return x.compare(y) < 0 ? -1 : (x == y ? 0 : 1);   // лексикографически
}

CompOp flipOp(CompOp op) {
    switch (op) {
        case CompOp::LT: return CompOp::GT;
        case CompOp::GT: return CompOp::LT;
        case CompOp::LE: return CompOp::GE;
        case CompOp::GE: return CompOp::LE;
        default: return op;
    }
}

}

bool Predicate::eval(const Row& row) const {
    const Field& x = a.get(row);
    const Field& y = b.get(row);

    if (op == CompOp::LIKE) {
        if (!x || !y) return false;
        if (regex) return std::regex_match(*x, *regex);
        try {
            return std::regex_match(*x, std::regex(*y));
        } catch (const std::regex_error&) {
            return false;
        }
    }

    if (op == CompOp::BETWEEN) {
        const Field& z = c.get(row);
        if (!x || !y || !z) return false;
        return compareValues(*x, *y, type) >= 0 && compareValues(*x, *z, type) < 0;
    }

    if (!x || !y) {
        if (op == CompOp::EQ) return !x && !y;
        if (op == CompOp::NE) return static_cast<bool>(x) != static_cast<bool>(y);
        return false;
    }

    int r = compareValues(*x, *y, type);
    switch (op) {
        case CompOp::EQ: return r == 0;
        case CompOp::NE: return r != 0;
        case CompOp::LT: return r < 0;
        case CompOp::GT: return r > 0;
        case CompOp::LE: return r <= 0;
        case CompOp::GE: return r >= 0;
        default: return false;
    }
}

Table::Table(const std::string& name, const CreateTableStmt& stmt, const std::string& db_dir)
    : name_(name), dir_(db_dir),
      data_path_(db_dir + "/" + name + ".db"),
      meta_path_(db_dir + "/" + name + ".meta") {
    meta_.table_name = name;
    for (const auto& c : stmt.columns) {
        ColumnDefinition col;
        col.name = c.name;
        col.type = (c.type == TokenType::TYPE_INT) ? DataType::INT : DataType::STRING;
        col.is_not_null = c.is_not_null;
        col.is_indexed = c.is_indexed;
        meta_.columns.push_back(col);
    }

    try {
        std::ofstream(data_path_, std::ios::binary | std::ios::trunc).close();   // пустой файл данных
        indexes_ = makeEmptyIndexes();
        saveIndexes();
        saveMeta();   // meta пишем последним: её наличие означает «таблица создана»
    } catch (...) {
        for (const auto& p : filePaths()) {
            std::error_code ec;
            fs::remove(p, ec);
        }
        throw;
    }
}

Table::Table(const std::string& name, const std::string& db_dir)
    : name_(name), dir_(db_dir),
      data_path_(db_dir + "/" + name + ".db"),
      meta_path_(db_dir + "/" + name + ".meta") {
    meta_.table_name = name;
    loadMeta();
    openIndexes();
}

Table::~Table() {
    try {
        saveIndexes();
    } catch (...) {
        // деструктор не должен бросать; при следующем открытии индекс перестроится
    }
}

void Table::saveMeta() const {
    std::string tmp = meta_path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Не удалось записать файл схемы таблицы: " + tmp);
        uint32_t n = static_cast<uint32_t>(meta_.columns.size());
        out.write(reinterpret_cast<const char*>(&n), sizeof(n));
        for (const auto& col : meta_.columns) {
            writeStr(out, col.name);
            out.put(col.type == DataType::INT ? 0 : 1);
            out.put(col.is_not_null ? 1 : 0);
            out.put(col.is_indexed ? 1 : 0);
        }
        out.flush();
        if (!out) throw std::runtime_error("Ошибка записи файла схемы таблицы: " + tmp);
    }
    fs::rename(tmp, meta_path_);
}


void Table::loadMeta() {
    std::ifstream in(meta_path_, std::ios::binary);
    if (!in) throw std::runtime_error("Не найден файл схемы таблицы '" + name_ + "'.");
    uint32_t n = 0;
    if (!in.read(reinterpret_cast<char*>(&n), sizeof(n)) || n == 0 || n > 4096) {
        throw std::runtime_error("Файл схемы таблицы '" + name_ + "' повреждён.");
    }
    for (uint32_t i = 0; i < n; ++i) {
        ColumnDefinition col;
        char t = 0, nn = 0, idx = 0;
        if (!readStr(in, col.name) || !in.get(t) || !in.get(nn) || !in.get(idx)) {
            throw std::runtime_error("Файл схемы таблицы '" + name_ + "' повреждён.");
        }
        col.type = t == 0 ? DataType::INT : DataType::STRING;
        col.is_not_null = nn != 0;
        col.is_indexed = idx != 0;
        meta_.columns.push_back(col);
    }
}

std::vector<std::string> Table::filePaths() const {
    std::vector<std::string> paths = {data_path_, meta_path_, data_path_ + ".tmp", meta_path_ + ".tmp"};
    for (const auto& col : meta_.columns) {
        if (col.is_indexed) {
            paths.push_back(indexPath(col.name));
            paths.push_back(indexPath(col.name) + ".tmp");
        }
    }
    return paths;
}

// ---------- индексы ----------

// Имена столбцов не содержат '.', поэтому «таблица.столбец.idx» не может совпасть у разных таблиц
std::string Table::indexPath(const std::string& column) const {
    return dir_ + "/" + name_ + "." + column + ".idx";
}

Table::IndexMap Table::makeEmptyIndexes() const {
    IndexMap map;
    for (const auto& col : meta_.columns) {
        if (col.is_indexed) {
            map[col.name] = std::make_unique<dbms::IndexManager>(indexPath(col.name), col.type == DataType::INT);
        }
    }
    return map;
}

void Table::addToIndexes(IndexMap& map, const Row& row, uint64_t offset) const {
    for (size_t i = 0; i < meta_.columns.size() && i < row.size(); ++i) {
        const auto& col = meta_.columns[i];
        if (col.is_indexed && row[i]) {
            map.at(col.name)->insert(makeKey(col, *row[i]), offset);
        }
    }
}

void Table::openIndexes() {
    IndexMap loaded = makeEmptyIndexes();
    uint64_t size = fileSize(data_path_);
    bool ok = true;
    for (auto& entry : loaded) {
        try {
            if (!entry.second->load(size)) ok = false;
        } catch (...) {
            ok = false;
        }
    }
    if (ok) {
        indexes_ = std::move(loaded);
    } else {
        rebuildIndexes();   // индекс отсутствует/устарел/повреждён — строим заново из данных
    }
}

void Table::rebuildIndexes() {
    IndexMap fresh = makeEmptyIndexes();
    if (!fresh.empty()) {
        scanFile(data_path_, [&](uint64_t off, Row& row) { addToIndexes(fresh, row, off); });
    }
    indexes_ = std::move(fresh);
    saveIndexes();
}

void Table::saveIndexes() const {
    uint64_t size = fileSize(data_path_);
    for (const auto& entry : indexes_) {
        entry.second->save(size);
    }
}

// ---------- условия ----------

Operand Table::makeOperand(const ParsedValue& pv) const {
    Operand op;
    if (pv.isColumn()) {
        int idx = meta_.columnIndex(pv.value);
        if (idx < 0) {
            throw std::runtime_error("Ошибка семантики: столбец '" + pv.value + "' не найден в таблице '" + name_ + "'.");
        }
        op.is_column = true;
        op.col_idx = static_cast<size_t>(idx);
        op.type = meta_.columns[idx].type;
    } else if (pv.isNull()) {
        op.is_null_const = true;
    } else if (pv.type == TokenType::NUMBER) {
        long long v = 0;
        if (!parseInt(pv.value, v)) {
            throw std::runtime_error("Ошибка семантики: число '" + pv.value + "' выходит за допустимый диапазон.");
        }
        op.type = DataType::INT;
        op.constant = std::to_string(v);
    } else {
        op.type = DataType::STRING;
        op.constant = pv.value;
    }
    return op;
}

Predicate Table::compile(const Condition& cond) const {
    Predicate p;
    p.op = cond.op;
    p.a = makeOperand(cond.left);
    p.b = makeOperand(cond.right);
    if (cond.op == CompOp::BETWEEN) p.c = makeOperand(cond.upper);

    std::vector<const Operand*> ops = {&p.a, &p.b};
    if (cond.op == CompOp::BETWEEN) ops.push_back(&p.c);

    if (cond.op == CompOp::LIKE) {
        for (const Operand* o : ops) {
            if (o->hasType() && o->type != DataType::STRING) {
                throw std::runtime_error("Ошибка семантики: LIKE применим только к строковым значениям.");
            }
        }
        if (!p.b.is_column && p.b.constant) {
            try {
                p.regex = std::make_shared<std::regex>(*p.b.constant);
            } catch (const std::regex_error& e) {
                throw std::runtime_error("Ошибка семантики: некорректное регулярное выражение '" + *p.b.constant + "': " + e.what());
            }
        }
    } else {
        bool have = false;
        for (const Operand* o : ops) {
            if (!o->hasType()) continue;
            if (!have) {
                p.type = o->type;
                have = true;
            } else if (o->type != p.type) {
                throw std::runtime_error("Ошибка семантики: в условии сравниваются значения разных типов (INT и STRING).");
            }
        }
    }
    return p;
}

bool Table::planIndex(const Predicate& p, std::vector<FileOffset>& offsets) const {
    if (p.op == CompOp::LIKE || p.op == CompOp::NE) return false;

    const Operand* col = nullptr;
    const Operand* k1 = nullptr;
    const Operand* k2 = nullptr;
    CompOp op = p.op;

    if (op == CompOp::BETWEEN) {
        if (!p.a.is_column || p.b.is_column || p.c.is_column) return false;
        if (p.b.is_null_const || p.c.is_null_const) return false;
        col = &p.a; k1 = &p.b; k2 = &p.c;
    } else {
        if (p.a.is_column && !p.b.is_column) {
            col = &p.a; k1 = &p.b;
        } else if (!p.a.is_column && p.b.is_column) {
            col = &p.b; k1 = &p.a; op = flipOp(op);   // 5 < id  ==>  id > 5
        } else {
            return false;
        }
        if (k1->is_null_const) return false;
    }

    const ColumnDefinition& cd = meta_.columns[col->col_idx];
    auto it = indexes_.find(cd.name);
    if (it == indexes_.end()) return false;
    const dbms::IndexManager& idx = *it->second;

    auto key = [&](const Operand& o) { return makeKey(cd, *o.constant); };

    switch (op) {
        case CompOp::EQ: {
            FileOffset off = 0;
            if (idx.find(key(*k1), off)) offsets.push_back(off);
            return true;
        }
        case CompOp::LT:
        case CompOp::LE:
            offsets = idx.rangeSearch(std::nullopt, key(*k1));
            return true;
        case CompOp::GT:
        case CompOp::GE:
            offsets = idx.rangeSearch(key(*k1), std::nullopt);
            return true;
        case CompOp::BETWEEN:
            offsets = idx.rangeSearch(key(*k1), key(*k2));
            return true;
        default:
            return false;
    }
}

// ---------- преобразование значений ----------

Field Table::convertValue(const ColumnDefinition& col, const ParsedValue& pv) const {
    if (pv.isNull()) return std::nullopt;
    if (pv.isColumn()) {
        throw std::runtime_error("Ошибка семантики: вместо значения для столбца '" + col.name + "' указано имя '" + pv.value + "'.");
    }
    if (col.type == DataType::INT) {
        if (pv.type != TokenType::NUMBER) {
            throw std::runtime_error("Ошибка типа: столбец '" + col.name + "' имеет тип INT, а получена строка \"" + pv.value + "\".");
        }
        long long v = 0;
        if (!parseInt(pv.value, v)) {
            throw std::runtime_error("Ошибка типа: число '" + pv.value + "' вне допустимого диапазона.");
        }
        return std::to_string(v);
    }
    if (pv.type != TokenType::STRING_LITERAL) {
        throw std::runtime_error("Ошибка типа: столбец '" + col.name + "' имеет тип STRING, а получено число " + pv.value + " (строки пишутся в двойных кавычках).");
    }
    return pv.value;
}

// ---------- INSERT ----------

size_t Table::insert(const std::vector<std::string>& columns,
                     const std::vector<std::vector<ParsedValue>>& rows) {
    std::vector<size_t> pos;
    if (columns.empty()) {
        for (size_t i = 0; i < meta_.columns.size(); ++i) pos.push_back(i);
    } else {
        std::unordered_set<std::string> seen;
        for (const auto& name : columns) {
            int idx = meta_.columnIndex(name);
            if (idx < 0) {
                throw std::runtime_error("Ошибка семантики: столбец '" + name + "' не найден в таблице '" + name_ + "'.");
            }
            if (!seen.insert(name).second) {
                throw std::runtime_error("Ошибка семантики: столбец '" + name + "' указан дважды.");
            }
            pos.push_back(static_cast<size_t>(idx));
        }
    }

    std::vector<Row> prepared;
    prepared.reserve(rows.size());
    std::vector<std::unordered_set<std::string>> batch_keys(meta_.columns.size());

    for (size_t r = 0; r < rows.size(); ++r) {
        const auto& vals = rows[r];
        std::string ctx = "Строка " + std::to_string(r + 1) + ": ";
        if (vals.size() != pos.size()) {
            throw std::runtime_error(ctx + "ожидалось значений: " + std::to_string(pos.size()) +
                                     ", получено: " + std::to_string(vals.size()) + ".");
        }

        Row row(meta_.columns.size());
        for (size_t k = 0; k < pos.size(); ++k) {
            try {
                row[pos[k]] = convertValue(meta_.columns[pos[k]], vals[k]);
            } catch (const std::runtime_error& e) {
                throw std::runtime_error(ctx + e.what());
            }
        }

        for (size_t i = 0; i < meta_.columns.size(); ++i) {
            const auto& col = meta_.columns[i];
            if (!row[i]) {
                if (col.is_not_null || col.is_indexed) {
                    throw std::runtime_error(ctx + "столбец '" + col.name + "' не может быть NULL.");
                }
                continue;
            }
            if (col.is_indexed) {
                FileOffset dummy = 0;
                if (!batch_keys[i].insert(*row[i]).second ||
                    indexes_.at(col.name)->find(makeKey(col, *row[i]), dummy)) {
                    throw std::runtime_error(ctx + "нарушено ограничение уникальности столбца '" + col.name +
                                             "' (значение " + *row[i] + " уже существует).");
                }
            }
        }
        prepared.push_back(std::move(row));
    }

    uint64_t base = fileSize(data_path_);
    std::string buf;
    std::vector<uint64_t> offsets;
    offsets.reserve(prepared.size());
    for (const auto& row : prepared) {
        offsets.push_back(base + buf.size());
        buf += serializeRow(row);
    }
    {
        std::ofstream out(data_path_, std::ios::binary | std::ios::app);
        if (!out) throw std::runtime_error("Не удалось открыть файл данных для записи: " + data_path_);
        out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
        out.flush();
        if (!out) throw std::runtime_error("Ошибка записи в файл данных: " + data_path_);
    }

    for (size_t r = 0; r < prepared.size(); ++r) {
        addToIndexes(indexes_, prepared[r], offsets[r]);
    }
    return prepared.size();
}

// ---------- SELECT ----------

std::vector<Row> Table::select(const Condition* where) const {
    std::vector<Row> result;

    if (!where) {
        scanFile(data_path_, [&](uint64_t, Row& row) { result.push_back(row); });
        return result;
    }

    Predicate p = compile(*where);

    std::vector<FileOffset> offsets;
    if (planIndex(p, offsets)) {
        std::ifstream in(data_path_, std::ios::binary);
        if (!in && !offsets.empty()) {
            throw std::runtime_error("Не удалось открыть файл данных: " + data_path_);
        }
        for (FileOffset off : offsets) {
            in.clear();
            in.seekg(static_cast<std::streamoff>(off));
            Row row;
            if (readRow(in, row) && p.eval(row)) result.push_back(std::move(row));
        }
        return result;
    }

    scanFile(data_path_, [&](uint64_t, Row& row) {
        if (p.eval(row)) result.push_back(row);
    });
    return result;
}

// ---------- DELETE / UPDATE ----------

size_t Table::rewrite(const std::function<RowAction(Row&)>& visitor) {
    std::ifstream in(data_path_, std::ios::binary);
    if (!in) return 0;

    std::string tmp = data_path_ + ".tmp";
    IndexMap fresh = makeEmptyIndexes();
    size_t affected = 0;

    try {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Не удалось создать временный файл: " + tmp);

        uint64_t offset = 0;
        Row row;
        while (readRow(in, row)) {
            RowAction act = visitor(row);
            if (act == RowAction::DELETE_ROW) {
                ++affected;
                continue;
            }
            if (act == RowAction::MODIFIED) ++affected;

            std::string buf = serializeRow(row);
            out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
            addToIndexes(fresh, row, offset);
            offset += buf.size();
        }
        out.flush();
        if (!out) throw std::runtime_error("Ошибка записи временного файла: " + tmp);
    } catch (const dbms::IndexError&) {
        std::error_code ec;
        fs::remove(tmp, ec);
        throw std::runtime_error("Нарушено ограничение уникальности индексированного столбца. Изменения отменены.");
    } catch (...) {
        std::error_code ec;
        fs::remove(tmp, ec);
        throw;
    }
    in.close();

    std::error_code ec;
    if (affected == 0) {
        fs::remove(tmp, ec);
        return 0;
    }

    for (const auto& entry : indexes_) {
        fs::remove(entry.second->path(), ec);
    }
    fs::rename(tmp, data_path_);
    indexes_ = std::move(fresh);
    saveIndexes();
    return affected;
}

size_t Table::remove(const Condition& where) {
    Predicate p = compile(where);
    return rewrite([&](Row& row) {
        return p.eval(row) ? RowAction::DELETE_ROW : RowAction::KEEP;
    });
}

size_t Table::update(const std::vector<std::pair<std::string, ParsedValue>>& assignments,
                     const Condition& where) {
    Predicate p = compile(where);

    std::vector<std::pair<size_t, Field>> changes;
    std::unordered_set<std::string> seen;
    for (const auto& [name, pv] : assignments) {
        int idx = meta_.columnIndex(name);
        if (idx < 0) {
            throw std::runtime_error("Ошибка семантики: столбец '" + name + "' не найден в таблице '" + name_ + "'.");
        }
        if (!seen.insert(name).second) {
            throw std::runtime_error("Ошибка семантики: столбец '" + name + "' присваивается дважды.");
        }
        const auto& col = meta_.columns[idx];
        Field v = convertValue(col, pv);
        if (!v && (col.is_not_null || col.is_indexed)) {
            throw std::runtime_error("Ошибка семантики: столбец '" + name + "' не может быть NULL.");
        }
        changes.emplace_back(static_cast<size_t>(idx), std::move(v));
    }

    return rewrite([&](Row& row) {
        if (!p.eval(row)) return RowAction::KEEP;
        for (const auto& [i, v] : changes) row[i] = v;
        return RowAction::MODIFIED;
    });
}
