/**
 * @file string_utils.hpp
 * @brief String utility functions for CLI
 */

#pragma once

#include <string>
#include <algorithm>
#include <cctype>
#include <vector>
#include <sstream>

namespace nexusd::cli::utils {

/**
 * @brief Convert string to uppercase (for command normalization)
 * @param str Input string
 * @return Uppercase version of the string
 */
inline std::string to_upper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

/**
 * @brief Convert string to lowercase
 * @param str Input string
 * @return Lowercase version of the string
 */
inline std::string to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

/**
 * @brief Trim whitespace from both ends of a string
 * @param str Input string
 * @return Trimmed string
 */
inline std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

/**
 * @brief Split string by delimiter
 * @param str Input string
 * @param delimiter Character to split on
 * @return Vector of tokens
 */
inline std::vector<std::string> split(const std::string& str, char delimiter = ' ') {
    std::vector<std::string> tokens;
    std::istringstream stream(str);
    std::string token;
    while (std::getline(stream, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

/**
 * @brief Tokenize command line (respects quoted strings)
 * @param input Command line input
 * @return Vector of tokens
 */
inline std::vector<std::string> tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;
    char quote_char = '\0';
    
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        
        if (!in_quotes && (c == '"' || c == '\'')) {
            in_quotes = true;
            quote_char = c;
        } else if (in_quotes && c == quote_char) {
            in_quotes = false;
            quote_char = '\0';
        } else if (!in_quotes && std::isspace(c)) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    
    if (!current.empty()) {
        tokens.push_back(current);
    }
    
    return tokens;
}

/**
 * @brief Check if string starts with prefix
 * @param str Input string
 * @param prefix Prefix to check
 * @return true if str starts with prefix
 */
inline bool starts_with(const std::string& str, const std::string& prefix) {
    if (prefix.size() > str.size()) return false;
    return str.compare(0, prefix.size(), prefix) == 0;
}

/**
 * @brief Join strings with delimiter
 * @param parts Vector of strings
 * @param delimiter Delimiter string
 * @return Joined string
 */
inline std::string join(const std::vector<std::string>& parts, const std::string& delimiter = " ") {
    if (parts.empty()) return "";
    std::string result = parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
        result += delimiter + parts[i];
    }
    return result;
}

} // namespace nexusd::cli::utils
