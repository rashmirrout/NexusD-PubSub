/**
 * @file output_formatter.hpp
 * @brief Output formatting for CLI (table and JSON)
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <sstream>
#include <iomanip>

namespace nexusd::cli {

/**
 * @brief JSON value types for output
 */
using JsonValue = std::variant<
    std::nullptr_t,
    bool,
    int64_t,
    double,
    std::string,
    std::vector<struct JsonObject>,
    std::map<std::string, struct JsonObject>
>;

struct JsonObject {
    JsonValue value;
    
    JsonObject() : value(nullptr) {}
    JsonObject(std::nullptr_t) : value(nullptr) {}
    JsonObject(bool b) : value(b) {}
    JsonObject(int i) : value(static_cast<int64_t>(i)) {}
    JsonObject(int64_t i) : value(i) {}
    JsonObject(double d) : value(d) {}
    JsonObject(const char* s) : value(std::string(s)) {}
    JsonObject(const std::string& s) : value(s) {}
    JsonObject(std::vector<JsonObject> arr) : value(std::move(arr)) {}
    JsonObject(std::map<std::string, JsonObject> obj) : value(std::move(obj)) {}
};

/**
 * @brief Table row for formatted output
 */
struct TableRow {
    std::vector<std::string> cells;
};

/**
 * @brief Output formatter supporting table and JSON formats
 */
class OutputFormatter {
public:
    explicit OutputFormatter(bool json_mode = false);
    
    /**
     * @brief Set JSON output mode
     */
    void set_json_mode(bool enabled);
    bool is_json_mode() const { return json_mode_; }
    
    // Simple value output
    void print_ok();
    void print_ok(const std::string& message);
    void print_error(const std::string& message);
    void print_nil();
    void print_integer(int64_t value);
    void print_string(const std::string& value);
    void print_bulk_string(const std::string& value);
    
    // Array output (Redis-style numbered list)
    void print_array(const std::vector<std::string>& items);
    
    // Key-value output
    void print_key_value(const std::string& key, const std::string& value);
    void print_key_values(const std::vector<std::pair<std::string, std::string>>& pairs);
    
    // Table output
    void print_table(const std::vector<std::string>& headers,
                     const std::vector<TableRow>& rows);
    
    // JSON object output
    void print_json(const JsonObject& obj);
    
    // Section headers (ignored in JSON mode)
    void print_section(const std::string& title);
    void print_separator();
    
    // Raw output (for streaming data like MONITOR)
    void print_raw(const std::string& text);
    void print_line(const std::string& text);
    
    // Flush output
    void flush();

private:
    bool json_mode_;
    
    std::string json_stringify(const JsonObject& obj, int indent = 0) const;
    std::string escape_json_string(const std::string& s) const;
    std::vector<size_t> calculate_column_widths(
        const std::vector<std::string>& headers,
        const std::vector<TableRow>& rows) const;
};

/**
 * @brief RAII helper for scoped output buffering
 */
class OutputBuffer {
public:
    OutputBuffer();
    ~OutputBuffer();
    
    std::string str() const;
    
private:
    std::stringstream buffer_;
    std::streambuf* old_cout_;
};

} // namespace nexusd::cli
