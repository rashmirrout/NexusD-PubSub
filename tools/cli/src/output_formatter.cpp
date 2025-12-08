/**
 * @file output_formatter.cpp
 * @brief Output formatting implementation
 */

#include "output_formatter.hpp"
#include <iostream>
#include <algorithm>

namespace nexusd::cli {

OutputFormatter::OutputFormatter(bool json_mode)
    : json_mode_(json_mode) {}

void OutputFormatter::set_json_mode(bool enabled) {
    json_mode_ = enabled;
}

void OutputFormatter::print_ok() {
    if (json_mode_) {
        std::cout << R"({"status":"OK"})" << "\n";
    } else {
        std::cout << "OK\n";
    }
}

void OutputFormatter::print_ok(const std::string& message) {
    if (json_mode_) {
        std::cout << R"({"status":"OK","message":")" << escape_json_string(message) << "\"}\n";
    } else {
        std::cout << "OK: " << message << "\n";
    }
}

void OutputFormatter::print_error(const std::string& message) {
    if (json_mode_) {
        std::cout << R"({"error":")" << escape_json_string(message) << "\"}\n";
    } else {
        std::cerr << "(error) " << message << "\n";
    }
}

void OutputFormatter::print_nil() {
    if (json_mode_) {
        std::cout << "null\n";
    } else {
        std::cout << "(nil)\n";
    }
}

void OutputFormatter::print_integer(int64_t value) {
    if (json_mode_) {
        std::cout << value << "\n";
    } else {
        std::cout << "(integer) " << value << "\n";
    }
}

void OutputFormatter::print_string(const std::string& value) {
    if (json_mode_) {
        std::cout << "\"" << escape_json_string(value) << "\"\n";
    } else {
        std::cout << "\"" << value << "\"\n";
    }
}

void OutputFormatter::print_bulk_string(const std::string& value) {
    if (json_mode_) {
        std::cout << "\"" << escape_json_string(value) << "\"\n";
    } else {
        std::cout << value << "\n";
    }
}

void OutputFormatter::print_array(const std::vector<std::string>& items) {
    if (json_mode_) {
        std::cout << "[";
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) std::cout << ",";
            std::cout << "\"" << escape_json_string(items[i]) << "\"";
        }
        std::cout << "]\n";
    } else {
        if (items.empty()) {
            std::cout << "(empty array)\n";
            return;
        }
        for (size_t i = 0; i < items.size(); ++i) {
            std::cout << std::setw(3) << (i + 1) << ") " << items[i] << "\n";
        }
    }
}

void OutputFormatter::print_key_value(const std::string& key, const std::string& value) {
    if (json_mode_) {
        std::cout << "{\"" << escape_json_string(key) << "\":\"" 
                  << escape_json_string(value) << "\"}\n";
    } else {
        std::cout << key << ": " << value << "\n";
    }
}

void OutputFormatter::print_key_values(const std::vector<std::pair<std::string, std::string>>& pairs) {
    if (json_mode_) {
        std::cout << "{";
        for (size_t i = 0; i < pairs.size(); ++i) {
            if (i > 0) std::cout << ",";
            std::cout << "\"" << escape_json_string(pairs[i].first) << "\":\""
                      << escape_json_string(pairs[i].second) << "\"";
        }
        std::cout << "}\n";
    } else {
        size_t max_key_len = 0;
        for (const auto& [key, _] : pairs) {
            max_key_len = std::max(max_key_len, key.size());
        }
        for (const auto& [key, value] : pairs) {
            std::cout << std::left << std::setw(max_key_len + 1) << (key + ":") 
                      << " " << value << "\n";
        }
    }
}

void OutputFormatter::print_table(const std::vector<std::string>& headers,
                                   const std::vector<TableRow>& rows) {
    if (json_mode_) {
        std::cout << "[";
        for (size_t r = 0; r < rows.size(); ++r) {
            if (r > 0) std::cout << ",";
            std::cout << "{";
            for (size_t c = 0; c < headers.size() && c < rows[r].cells.size(); ++c) {
                if (c > 0) std::cout << ",";
                std::cout << "\"" << escape_json_string(headers[c]) << "\":\""
                          << escape_json_string(rows[r].cells[c]) << "\"";
            }
            std::cout << "}";
        }
        std::cout << "]\n";
    } else {
        if (rows.empty()) {
            std::cout << "(empty list)\n";
            return;
        }
        
        auto widths = calculate_column_widths(headers, rows);
        
        // Print header
        for (size_t i = 0; i < headers.size(); ++i) {
            std::cout << std::left << std::setw(widths[i] + 2) << headers[i];
        }
        std::cout << "\n";
        
        // Print separator
        for (size_t i = 0; i < headers.size(); ++i) {
            std::cout << std::string(widths[i], '-') << "  ";
        }
        std::cout << "\n";
        
        // Print rows
        for (const auto& row : rows) {
            for (size_t i = 0; i < row.cells.size() && i < widths.size(); ++i) {
                std::cout << std::left << std::setw(widths[i] + 2) << row.cells[i];
            }
            std::cout << "\n";
        }
    }
}

void OutputFormatter::print_json(const JsonObject& obj) {
    std::cout << json_stringify(obj, 0) << "\n";
}

void OutputFormatter::print_section(const std::string& title) {
    if (!json_mode_) {
        std::cout << "\n# " << title << "\n";
    }
}

void OutputFormatter::print_separator() {
    if (!json_mode_) {
        std::cout << "\n";
    }
}

void OutputFormatter::print_raw(const std::string& text) {
    std::cout << text;
}

void OutputFormatter::print_line(const std::string& text) {
    std::cout << text << "\n";
}

void OutputFormatter::flush() {
    std::cout.flush();
}

std::string OutputFormatter::escape_json_string(const std::string& s) const {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

std::vector<size_t> OutputFormatter::calculate_column_widths(
    const std::vector<std::string>& headers,
    const std::vector<TableRow>& rows) const {
    
    std::vector<size_t> widths(headers.size());
    
    for (size_t i = 0; i < headers.size(); ++i) {
        widths[i] = headers[i].size();
    }
    
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.cells.size() && i < widths.size(); ++i) {
            widths[i] = std::max(widths[i], row.cells[i].size());
        }
    }
    
    return widths;
}

std::string OutputFormatter::json_stringify(const JsonObject& obj, int indent) const {
    return std::visit([this, indent](const auto& val) -> std::string {
        using T = std::decay_t<decltype(val)>;
        
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            return val ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(val);
        } else if constexpr (std::is_same_v<T, double>) {
            std::ostringstream oss;
            oss << std::setprecision(15) << val;
            return oss.str();
        } else if constexpr (std::is_same_v<T, std::string>) {
            return "\"" + escape_json_string(val) + "\"";
        } else if constexpr (std::is_same_v<T, std::vector<JsonObject>>) {
            if (val.empty()) return "[]";
            std::string result = "[";
            for (size_t i = 0; i < val.size(); ++i) {
                if (i > 0) result += ",";
                result += json_stringify(val[i], indent + 2);
            }
            result += "]";
            return result;
        } else if constexpr (std::is_same_v<T, std::map<std::string, JsonObject>>) {
            if (val.empty()) return "{}";
            std::string result = "{";
            bool first = true;
            for (const auto& [k, v] : val) {
                if (!first) result += ",";
                first = false;
                result += "\"" + escape_json_string(k) + "\":" + json_stringify(v, indent + 2);
            }
            result += "}";
            return result;
        }
        return "null";
    }, obj.value);
}

// OutputBuffer implementation
OutputBuffer::OutputBuffer() {
    old_cout_ = std::cout.rdbuf(buffer_.rdbuf());
}

OutputBuffer::~OutputBuffer() {
    std::cout.rdbuf(old_cout_);
}

std::string OutputBuffer::str() const {
    return buffer_.str();
}

} // namespace nexusd::cli
