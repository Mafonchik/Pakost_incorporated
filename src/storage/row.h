#pragma once

#include <optional>
#include <string>
#include <vector>

// Значение поля: nullopt = NULL (отличается от пустой строки ""). 
// INT хранится в каноническом текстовом виде.
using Field = std::optional<std::string>;
using Row = std::vector<Field>;
