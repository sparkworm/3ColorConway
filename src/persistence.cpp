#include "persistence.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <utility>

namespace {
constexpr char ConfigurationFormat[] = "3colorconway-rules";
constexpr std::array<std::uint8_t, 8> BoardMagic{
    '3', 'C', 'C', 'B', 'O', 'A', 'R', 'D'};
constexpr std::uint16_t FileVersion = 1;
// Header: magic (8), version (2), dimensions (8), then three 11-byte rules.
constexpr std::size_t BoardHeaderSize = 51;
// Cell bits are packed row-major, least-significant bit first within each byte.
constexpr std::size_t MaxConfigurationSize = 64 * 1024;
constexpr std::size_t MaxBoardSize = 128 * 1024 * 1024;

struct JsonValue {
    enum class Type { Object, Array, Number, String, Other };
    Type type = Type::Other;
    int number = 0;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;
};

class JsonParser {
public:
    // Creates a parser that reads JSON tokens from input without taking ownership.
    // @param input Complete JSON text to parse; it must outlive this parser.
    // @return No value; constructs the parser.
    explicit JsonParser(const std::string& input) : input_(input) {}

    // Parses one complete JSON value and rejects any non-whitespace trailing text.
    // @param value Receives the parsed tree only when parsing succeeds.
    // @return true for valid complete JSON, otherwise false.
    bool parse(JsonValue& value) {
        skipWhitespace();
        if (!parseValue(value, 0)) {
            return false;
        }
        skipWhitespace();
        return position_ == input_.size();
    }

private:
    // Parses a value based on its leading token, enforcing the nesting limit.
    // @param value Receives the parsed object, array, string, or integer.
    // @param depth Current nesting depth used to bound recursive parsing.
    // @return true when one value is valid, otherwise false.
    bool parseValue(JsonValue& value, unsigned int depth) {
        // Bound nesting so a malformed hand-edited file cannot recurse indefinitely.
        if (depth > 16 || position_ >= input_.size()) {
            return false;
        }
        switch (input_[position_]) {
        case '{':
            return parseObject(value, depth + 1);
        case '[':
            return parseArray(value, depth + 1);
        case '"':
            value.type = JsonValue::Type::String;
            return parseString(value.string);
        case 't':
            return parseLiteral("true");
        case 'f':
            return parseLiteral("false");
        case 'n':
            return parseLiteral("null");
        default:
            return parseNumber(value);
        }
    }

    // Parses a JSON object, rejecting missing separators and duplicate keys.
    // @param value Receives the object and its parsed members.
    // @param depth Nesting depth to pass on to child values.
    // @return true when the object is syntactically complete, otherwise false.
    bool parseObject(JsonValue& value, unsigned int depth) {
        ++position_;
        value.type = JsonValue::Type::Object;
        skipWhitespace();
        if (consume('}')) {
            return true;
        }
        while (position_ < input_.size()) {
            std::string key;
            if (input_[position_] != '"' || !parseString(key)) {
                return false;
            }
            skipWhitespace();
            if (!consume(':')) {
                return false;
            }
            skipWhitespace();
            JsonValue member;
            if (!parseValue(member, depth)) {
                return false;
            }
            if (!value.object.emplace(std::move(key), std::move(member)).second) {
                return false;
            }
            skipWhitespace();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
            skipWhitespace();
        }
        return false;
    }

    // Parses a JSON array, preserving the order of its elements.
    // @param value Receives the array and its parsed elements.
    // @param depth Nesting depth to pass on to child values.
    // @return true when the array is syntactically complete, otherwise false.
    bool parseArray(JsonValue& value, unsigned int depth) {
        ++position_;
        value.type = JsonValue::Type::Array;
        skipWhitespace();
        if (consume(']')) {
            return true;
        }
        while (position_ < input_.size()) {
            JsonValue element;
            if (!parseValue(element, depth)) {
                return false;
            }
            value.array.push_back(std::move(element));
            skipWhitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
            skipWhitespace();
        }
        return false;
    }

    // Parses a quoted JSON string, including supported escapes and ASCII Unicode.
    // @param value Receives the decoded string contents.
    // @return true when a closing quote and valid escapes are found, otherwise false.
    bool parseString(std::string& value) {
        if (!consume('"')) {
            return false;
        }
        while (position_ < input_.size()) {
            const unsigned char character =
                static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                return true;
            }
            if (character < 0x20) {
                return false;
            }
            if (character != '\\') {
                value.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) {
                return false;
            }
            const char escaped = input_[position_++];
            // Decode JSON escapes; non-ASCII Unicode escapes are not needed by
            // the rule-file schema and are rejected rather than misrepresented.
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                value.push_back(escaped);
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case 'u': {
                unsigned int codePoint = 0;
                for (int digit = 0; digit < 4; ++digit) {
                    if (position_ >= input_.size()) {
                        return false;
                    }
                    const char hex = input_[position_++];
                    codePoint <<= 4;
                    if (hex >= '0' && hex <= '9') {
                        codePoint += static_cast<unsigned int>(hex - '0');
                    } else if (hex >= 'a' && hex <= 'f') {
                        codePoint += static_cast<unsigned int>(hex - 'a' + 10);
                    } else if (hex >= 'A' && hex <= 'F') {
                        codePoint += static_cast<unsigned int>(hex - 'A' + 10);
                    } else {
                        return false;
                    }
                }
                if (codePoint > 0x7f) {
                    return false;
                }
                value.push_back(static_cast<char>(codePoint));
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    // Parses a signed decimal integer; fractional and exponent forms are rejected.
    // @param value Receives the parsed integer.
    // @return true for an in-range integer token, otherwise false.
    bool parseNumber(JsonValue& value) {
        const std::size_t start = position_;
        if (position_ < input_.size() && input_[position_] == '-') {
            ++position_;
        }
        if (position_ >= input_.size() || input_[position_] < '0' ||
            input_[position_] > '9') {
            return false;
        }
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && input_[position_] >= '0' &&
                input_[position_] <= '9') {
                return false;
            }
        } else {
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') {
                ++position_;
            }
        }
        if (position_ < input_.size() &&
            (input_[position_] == '.' || input_[position_] == 'e' ||
             input_[position_] == 'E')) {
            return false;
        }
        const char* numberStart = input_.data() + start;
        const char* numberEnd = input_.data() + position_;
        const auto conversion = std::from_chars(numberStart, numberEnd,
                                                 value.number);
        if (conversion.ec != std::errc{} || conversion.ptr != numberEnd) {
            return false;
        }
        value.type = JsonValue::Type::Number;
        return true;
    }

    // Matches and consumes a JSON keyword such as true, false, or null.
    // @param literal Exact keyword expected at the current input position.
    // @return true and advances past the keyword on a match; otherwise false.
    bool parseLiteral(const char* literal) {
        const std::string expected(literal);
        if (input_.compare(position_, expected.size(), expected) != 0) {
            return false;
        }
        position_ += expected.size();
        return true;
    }

    // Advances over JSON whitespace at the current input position.
    // @return No value; updates the parser position.
    void skipWhitespace() {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\t' ||
                input_[position_] == '\n' || input_[position_] == '\r')) {
            ++position_;
        }
    }

    // Consumes one expected character if it is next in the input.
    // @param expected Character to match and consume.
    // @return true if consumed, otherwise false without advancing.
    bool consume(char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    const std::string& input_;
    std::size_t position_ = 0;
};

// Checks that all rule weights and score thresholds fit the simulation limits.
// @param rules Rule set to validate.
// @return true when every stored value is supported, otherwise false.
bool isValidRules(const RuleSet& rules) {
    for (const ChannelRule& rule : rules) {
        for (const int weight : rule.weights) {
            if (weight < MinWeight || weight > MaxWeight) {
                return false;
            }
        }
        if (rule.birthMin < -MaxRuleScore || rule.birthMin > MaxRuleScore ||
            rule.birthMax < -MaxRuleScore || rule.birthMax > MaxRuleScore ||
            rule.survivalMin < -MaxRuleScore ||
            rule.survivalMin > MaxRuleScore ||
            rule.survivalMax < -MaxRuleScore ||
            rule.survivalMax > MaxRuleScore) {
            return false;
        }
    }
    return true;
}

// Finds a named member of a parsed JSON object.
// @param object JSON value that must contain an object.
// @param key Member name to find.
// @param value Receives a pointer to the member when found.
// @return true when the object contains key, otherwise false.
bool getMember(const JsonValue& object, const char* key,
               const JsonValue*& value) {
    if (object.type != JsonValue::Type::Object) {
        return false;
    }
    const auto member = object.object.find(key);
    if (member == object.object.end()) {
        return false;
    }
    value = &member->second;
    return true;
}

// Reads a named JSON object member only when it is an integer.
// @param object JSON object containing the requested member.
// @param key Name of the integer member to read.
// @param number Receives the member's integer value.
// @return true when the member exists and is an integer, otherwise false.
bool getInteger(const JsonValue& object, const char* key, int& number) {
    const JsonValue* value = nullptr;
    if (!getMember(object, key, value) ||
        value->type != JsonValue::Type::Number) {
        return false;
    }
    number = value->number;
    return true;
}

// Appends a 16-bit integer in little-endian byte order.
// @param bytes Destination byte sequence.
// @param value Integer to append.
// @return No value; appends two bytes to bytes.
void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

// Appends a 32-bit integer in little-endian byte order.
// @param bytes Destination byte sequence.
// @param value Integer to append.
// @return No value; appends four bytes to bytes.
void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

// Reads a little-endian 16-bit integer at offset.
// @param bytes Source byte sequence.
// @param offset Read position; advances by two only on success.
// @param value Receives the decoded integer.
// @return true when two bytes are available, otherwise false.
bool readU16(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
             std::uint16_t& value) {
    if (offset + 2 > bytes.size()) {
        return false;
    }
    value = static_cast<std::uint16_t>(bytes[offset]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
    offset += 2;
    return true;
}

// Reads a little-endian 32-bit integer at offset.
// @param bytes Source byte sequence.
// @param offset Read position; advances by four only on success.
// @param value Receives the decoded integer.
// @return true when four bytes are available, otherwise false.
bool readU32(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
             std::uint32_t& value) {
    if (offset + 4 > bytes.size()) {
        return false;
    }
    value = 0;
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
    }
    return true;
}

// Encodes a supported signed rule value in its 16-bit two's-complement form.
// @param value Signed integer to encode.
// @return The value's 16-bit representation.
std::uint16_t encodeSigned16(int value) {
    return static_cast<std::uint16_t>(static_cast<std::int16_t>(value));
}

// Converts a 16-bit two's-complement value back to a signed integer.
// @param value Encoded 16-bit value.
// @return The corresponding signed integer.
int decodeSigned16(std::uint16_t value) {
    return value <= 0x7fffU ? static_cast<int>(value)
                           : static_cast<int>(value) - 0x10000;
}

// Formats rules as the versioned, human-readable JSON configuration schema.
// @param rules Valid rules to serialize.
// @return JSON text including a trailing newline.
std::string formatConfiguration(const RuleSet& rules) {
    std::ostringstream output;
    output << "{\n  \"format\": \"" << ConfigurationFormat
           << "\",\n  \"version\": " << FileVersion
           << ",\n  \"channels\": [\n";
    for (std::size_t channel = 0; channel < rules.size(); ++channel) {
        const ChannelRule& rule = rules[channel];
        output << "    {\"weights\": [" << rule.weights[0] << ", "
               << rule.weights[1] << ", " << rule.weights[2]
               << "], \"birth_min\": " << rule.birthMin
               << ", \"birth_max\": " << rule.birthMax
               << ", \"survival_min\": " << rule.survivalMin
               << ", \"survival_max\": " << rule.survivalMax << "}";
        output << (channel + 1 == rules.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    return output.str();
}

// Writes bytes to a temporary sibling file and renames it into place on success.
// @param path Destination file name.
// @param bytes Data to write.
// @param size Number of bytes to write.
// @param error Receives an explanation if an operation fails; cleared on success.
// @return true when the destination is replaced successfully, otherwise false.
bool writeAtomically(const std::string& path, const std::uint8_t* bytes,
                     std::size_t size, std::string& error) {
    if (path.empty()) {
        error = "File path is empty.";
        return false;
    }
    const std::filesystem::path target(path);
    std::filesystem::path temporary = target;
    temporary += ".tmp";

    // Finish writing a sibling temporary before replacing the destination.
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not open temporary file for writing.";
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes),
                 static_cast<std::streamsize>(size));
    output.flush();
    if (!output) {
        error = "Could not write the complete file.";
        output.close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    output.close();
    if (!output) {
        error = "Could not finish writing the file.";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }

    std::error_code renameError;
    std::filesystem::rename(temporary, target, renameError);
    if (renameError) {
        error = "Could not replace the destination file: " +
                renameError.message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    error.clear();
    return true;
}

// Reads a bounded file into memory, rejecting missing, oversized, or partial input.
// @param path File name to read.
// @param maximumSize Largest permitted file in bytes.
// @param bytes Receives the complete file contents.
// @param error Receives an explanation if reading fails; cleared on success.
// @return true when the entire file was read, otherwise false.
bool readFile(const std::string& path, std::size_t maximumSize,
              std::vector<std::uint8_t>& bytes, std::string& error) {
    if (path.empty()) {
        error = "File path is empty.";
        return false;
    }
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
    if (sizeError) {
        error = "Could not read file size: " + sizeError.message();
        return false;
    }
    if (size > maximumSize) {
        error = "File is larger than the supported format.";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not open file for reading.";
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    if (input.bad() || bytes.size() != size) {
        error = "Could not read the complete file.";
        return false;
    }
    error.clear();
    return true;
}

// Extracts and validates the versioned channel-rule schema from parsed JSON.
// @param value Parsed root JSON value.
// @param rules Receives the rules only after the schema and ranges validate.
// @return true for a complete supported rule configuration, otherwise false.
bool parseRules(const JsonValue& value, RuleSet& rules) {
    // Require the complete known schema; extra or missing fields indicate an
    // unsupported version rather than something to silently ignore.
    if (value.type != JsonValue::Type::Object || value.object.size() != 3) {
        return false;
    }
    const JsonValue* channels = nullptr;
    const JsonValue* format = nullptr;
    const JsonValue* version = nullptr;
    if (!getMember(value, "format", format) ||
        format->type != JsonValue::Type::String ||
        format->string != ConfigurationFormat ||
        !getMember(value, "version", version) ||
        version->type != JsonValue::Type::Number ||
        version->number != FileVersion ||
        !getMember(value, "channels", channels) ||
        channels->type != JsonValue::Type::Array ||
        channels->array.size() != rules.size()) {
        return false;
    }
    for (std::size_t channel = 0; channel < rules.size(); ++channel) {
        const JsonValue& channelValue = channels->array[channel];
        const JsonValue* weights = nullptr;
        if (channelValue.type != JsonValue::Type::Object ||
            channelValue.object.size() != 5 ||
            !getMember(channelValue, "weights", weights) ||
            weights->type != JsonValue::Type::Array ||
            weights->array.size() != 3) {
            return false;
        }
        for (std::size_t source = 0; source < 3; ++source) {
            if (weights->array[source].type != JsonValue::Type::Number) {
                return false;
            }
            rules[channel].weights[source] = weights->array[source].number;
        }
        if (!getInteger(channelValue, "birth_min", rules[channel].birthMin) ||
            !getInteger(channelValue, "birth_max", rules[channel].birthMax) ||
            !getInteger(channelValue, "survival_min",
                        rules[channel].survivalMin) ||
            !getInteger(channelValue, "survival_max",
                        rules[channel].survivalMax)) {
            return false;
        }
    }
    return isValidRules(rules);
}
}

// Saves a validated rule set as versioned JSON.
// @param path Destination JSON file name.
// @param rules Rule set to serialize.
// @param error Receives the failure reason; cleared when saving succeeds.
// @return true when the configuration is written, otherwise false.
bool saveConfiguration(const std::string& path, const RuleSet& rules,
                       std::string& error) {
    if (!isValidRules(rules)) {
        error = "Current rules contain values outside the supported ranges.";
        return false;
    }
    const std::string contents = formatConfiguration(rules);
    return writeAtomically(path,
                           reinterpret_cast<const std::uint8_t*>(contents.data()),
                           contents.size(), error);
}

// Loads a JSON rule configuration without changing rules on failure.
// @param path Configuration file name to read.
// @param rules Receives the new rule set after complete validation.
// @param error Receives the failure reason; cleared when loading succeeds.
// @return true when valid rules were loaded, otherwise false.
bool loadConfiguration(const std::string& path, RuleSet& rules,
                       std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!readFile(path, MaxConfigurationSize, bytes, error)) {
        return false;
    }
    const std::string contents(bytes.begin(), bytes.end());
    JsonValue root;
    RuleSet loadedRules{};
    if (!JsonParser(contents).parse(root) || !parseRules(root, loadedRules)) {
        error = "Configuration JSON is malformed, unsupported, or has invalid rule values.";
        return false;
    }
    // Apply only after parsing and validation succeed, so failures preserve rules.
    rules = loadedRules;
    error.clear();
    return true;
}

// Saves grid cells and their associated rules in the compact board format.
// @param path Destination board file name.
// @param cells Row-major cell states, each represented by a three-bit RGB mask.
// @param rules Rule set to store with the board.
// @param error Receives the failure reason; cleared when saving succeeds.
// @return true when the complete board is written, otherwise false.
bool saveBoard(const std::string& path,
               const std::vector<unsigned int>& cells, const RuleSet& rules,
               std::string& error) {
    return saveBoard(path, cells, rules, GridColumns, GridRows, error);
}

bool saveBoard(const std::string& path,
               const std::vector<unsigned int>& cells, const RuleSet& rules,
               int width, int height, std::string& error) {
    if (width < 1 || height < 1 ||
        width > 16384 || height > 16384) {
        error = "Board dimensions are outside the supported range.";
        return false;
    }
    const std::size_t expectedCellCount =
        static_cast<std::size_t>(width) * height;
    if (cells.size() != expectedCellCount) {
        error = "Board dimensions do not match the current grid.";
        return false;
    }
    if (!isValidRules(rules)) {
        error = "Current rules contain values outside the supported ranges.";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    const std::size_t packedSize = (expectedCellCount * 3 + 7) / 8;
    const std::size_t boardFileSize = BoardHeaderSize + packedSize;
    if (boardFileSize > MaxBoardSize) {
        error = "Board dimensions are too large.";
        return false;
    }
    bytes.reserve(boardFileSize);
    bytes.insert(bytes.end(), BoardMagic.begin(), BoardMagic.end());
    // Multi-byte header fields are encoded little-endian for a portable format.
    appendU16(bytes, FileVersion);
    appendU32(bytes, static_cast<std::uint32_t>(width));
    appendU32(bytes, static_cast<std::uint32_t>(height));
    // Store each rule as three signed one-byte weights and four signed 16-bit limits.
    for (const ChannelRule& rule : rules) {
        for (const int weight : rule.weights) {
            bytes.push_back(static_cast<std::uint8_t>(
                static_cast<std::int8_t>(weight)));
        }
        appendU16(bytes, encodeSigned16(rule.birthMin));
        appendU16(bytes, encodeSigned16(rule.birthMax));
        appendU16(bytes, encodeSigned16(rule.survivalMin));
        appendU16(bytes, encodeSigned16(rule.survivalMax));
    }
    if (bytes.size() != BoardHeaderSize) {
        error = "Internal board header size mismatch.";
        return false;
    }
    bytes.resize(boardFileSize, 0);
    // Pack each row-major cell's three RGB bits consecutively into the payload.
    for (std::size_t cell = 0; cell < cells.size(); ++cell) {
        if (cells[cell] > 7U) {
            error = "Board contains a cell state outside the RGB bit-mask range.";
            return false;
        }
        for (unsigned int bit = 0; bit < 3; ++bit) {
            if ((cells[cell] & (1U << bit)) != 0U) {
                // Cell n occupies bits 3n through 3n+2, crossing bytes as needed.
                const std::size_t bitIndex = cell * 3 + bit;
                bytes[BoardHeaderSize + bitIndex / 8] |=
                    static_cast<std::uint8_t>(1U << (bitIndex % 8));
            }
        }
    }
    if (bytes.size() != boardFileSize) {
        error = "Internal board encoding size mismatch.";
        return false;
    }
    return writeAtomically(path, bytes.data(), bytes.size(), error);
}

// Loads a board and its rules, committing neither unless the entire file validates.
// @param path Board file name to read.
// @param cells Receives row-major decoded cell states on success.
// @param rules Receives the associated validated rules on success.
// @param error Receives the failure reason; cleared when loading succeeds.
// @return true when the complete board and rule set were loaded, otherwise false.
bool loadBoard(const std::string& path, std::vector<unsigned int>& cells,
               RuleSet& rules, std::string& error) {
    std::vector<unsigned int> loadedCells;
    RuleSet loadedRules{};
    int width = 0;
    int height = 0;
    if (!loadBoard(path, loadedCells, loadedRules, width, height, error)) {
        return false;
    }
    if (width != GridColumns || height != GridRows) {
        error = "Board dimensions do not match the current grid.";
        return false;
    }
    cells = std::move(loadedCells);
    rules = loadedRules;
    error.clear();
    return true;
}

bool loadBoard(const std::string& path, std::vector<unsigned int>& cells,
               RuleSet& rules, int& width, int& height, std::string& error) {
    std::error_code sizeError;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, sizeError);
    if (sizeError) {
        error = "Could not read file size: " + sizeError.message();
        return false;
    }
    if (fileSize < BoardHeaderSize || fileSize > MaxBoardSize) {
        error = "Board file is not a supported size.";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    if (!readFile(path, static_cast<std::size_t>(fileSize), bytes, error)) {
        return false;
    }
    if (bytes.size() != static_cast<std::size_t>(fileSize) ||
        !std::equal(BoardMagic.begin(), BoardMagic.end(), bytes.begin())) {
        error = "Board file has an invalid signature or size.";
        return false;
    }
    std::size_t offset = BoardMagic.size();
    std::uint16_t version = 0;
    std::uint32_t storedWidth = 0;
    std::uint32_t storedHeight = 0;
    if (!readU16(bytes, offset, version) || version != FileVersion ||
        !readU32(bytes, offset, storedWidth) ||
        !readU32(bytes, offset, storedHeight) ||
        storedWidth < 1 || storedHeight < 1 ||
        storedWidth > 16384 || storedHeight > 16384) {
        error = "Board file version or dimensions are not supported.";
        return false;
    }
    width = static_cast<int>(storedWidth);
    height = static_cast<int>(storedHeight);
    const std::size_t expectedCellCount =
        static_cast<std::size_t>(width) * height;
    const std::size_t packedSize = (expectedCellCount * 3 + 7) / 8;
    if (bytes.size() != BoardHeaderSize + packedSize) {
        error = "Board file has an invalid payload size.";
        return false;
    }

    // Decode the stored rule metadata before allocating and decoding cell data.
    RuleSet loadedRules{};
    for (ChannelRule& rule : loadedRules) {
        for (int& weight : rule.weights) {
            if (offset >= bytes.size()) {
                error = "Board file is truncated.";
                return false;
            }
            const std::uint8_t encoded = bytes[offset++];
            weight = encoded <= 0x7fU
                         ? static_cast<int>(encoded)
                         : static_cast<int>(encoded) - 0x100;
        }
        std::uint16_t value = 0;
        if (!readU16(bytes, offset, value)) {
            error = "Board file is truncated.";
            return false;
        }
        rule.birthMin = decodeSigned16(value);
        if (!readU16(bytes, offset, value)) {
            error = "Board file is truncated.";
            return false;
        }
        rule.birthMax = decodeSigned16(value);
        if (!readU16(bytes, offset, value)) {
            error = "Board file is truncated.";
            return false;
        }
        rule.survivalMin = decodeSigned16(value);
        if (!readU16(bytes, offset, value)) {
            error = "Board file is truncated.";
            return false;
        }
        rule.survivalMax = decodeSigned16(value);
    }
    if (offset != BoardHeaderSize || !isValidRules(loadedRules)) {
        error = "Board file contains invalid rule values.";
        return false;
    }

    // Decode into temporary state and commit both board and rules only when valid.
    // Expand each packed group of three bits into one cell state.
    std::vector<unsigned int> loadedCells(
        expectedCellCount, DeadState);
    for (std::size_t cell = 0; cell < loadedCells.size(); ++cell) {
        unsigned int state = 0;
        for (unsigned int bit = 0; bit < 3; ++bit) {
            const std::size_t bitIndex = cell * 3 + bit;
            if ((bytes[BoardHeaderSize + bitIndex / 8] &
                 static_cast<std::uint8_t>(1U << (bitIndex % 8))) != 0U) {
                state |= 1U << bit;
            }
        }
        loadedCells[cell] = state;
    }
    cells = std::move(loadedCells);
    rules = loadedRules;
    error.clear();
    return true;
}
