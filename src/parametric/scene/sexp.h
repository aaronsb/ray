#pragma once

// S-expression parser for scene description language
// Minimal implementation: tokenize + recursive descent

#include <string>
#include <vector>
#include <variant>
#include <stdexcept>
#include <cctype>
#include <sstream>

namespace parametric {

// S-expression: either an atom (symbol/number/string) or a list
struct SExp;
using SExpList = std::vector<SExp>;

struct SExp {
    std::variant<std::string, double, SExpList> value;

    // Convenience constructors
    SExp() : value(SExpList{}) {}
    SExp(const std::string& s) : value(s) {}
    SExp(double d) : value(d) {}
    SExp(SExpList list) : value(std::move(list)) {}

    // Type checks
    bool isSymbol() const { return std::holds_alternative<std::string>(value); }
    bool isNumber() const { return std::holds_alternative<double>(value); }
    bool isList() const { return std::holds_alternative<SExpList>(value); }

    // Accessors (throw if wrong type)
    const std::string& asSymbol() const { return std::get<std::string>(value); }
    double asNumber() const { return std::get<double>(value); }
    const SExpList& asList() const { return std::get<SExpList>(value); }
    SExpList& asList() { return std::get<SExpList>(value); }

    // List helpers
    size_t size() const { return isList() ? asList().size() : 0; }
    const SExp& operator[](size_t i) const { return asList()[i]; }

    // Get first element as symbol (common pattern for (op args...))
    const std::string& head() const {
        if (!isList() || asList().empty() || !asList()[0].isSymbol()) {
            throw std::runtime_error("Expected (symbol ...)");
        }
        return asList()[0].asSymbol();
    }
};

// Tokenizer
enum class TokenType { LParen, RParen, Symbol, Number, String, End };

struct Token {
    TokenType type;
    std::string text;
    double number = 0;
};

class Tokenizer {
public:
    explicit Tokenizer(const std::string& input) : input_(input), pos_(0) {}

    Token next() {
        skipWhitespaceAndComments();

        if (pos_ >= input_.size()) {
            return {TokenType::End, "", 0};
        }

        char c = input_[pos_];

        if (c == '(') {
            pos_++;
            return {TokenType::LParen, "(", 0};
        }
        if (c == ')') {
            pos_++;
            return {TokenType::RParen, ")", 0};
        }
        if (c == '"') {
            return readString();
        }
        if (isdigit(c) || (c == '-' && pos_ + 1 < input_.size() &&
                           (isdigit(input_[pos_ + 1]) || input_[pos_ + 1] == '.'))) {
            return readNumber();
        }
        return readSymbol();
    }

    Token peek() {
        size_t saved = pos_;
        Token t = next();
        pos_ = saved;
        return t;
    }

private:
    void skipWhitespaceAndComments() {
        while (pos_ < input_.size()) {
            char c = input_[pos_];
            if (isspace(c)) {
                pos_++;
            } else if (c == ';') {
                // Line comment - skip to newline
                while (pos_ < input_.size() && input_[pos_] != '\n') {
                    pos_++;
                }
            } else {
                break;
            }
        }
    }

    Token readString() {
        pos_++; // skip opening quote
        std::string s;
        while (pos_ < input_.size() && input_[pos_] != '"') {
            if (input_[pos_] == '\\' && pos_ + 1 < input_.size()) {
                pos_++;
                char esc = input_[pos_];
                if (esc == 'n') s += '\n';
                else if (esc == 't') s += '\t';
                else s += esc;
            } else {
                s += input_[pos_];
            }
            pos_++;
        }
        if (pos_ < input_.size()) pos_++; // skip closing quote
        return {TokenType::String, s, 0};
    }

    Token readNumber() {
        size_t start = pos_;
        if (input_[pos_] == '-') pos_++;
        while (pos_ < input_.size() && (isdigit(input_[pos_]) || input_[pos_] == '.')) {
            pos_++;
        }
        std::string text = input_.substr(start, pos_ - start);
        double num = std::stod(text);
        return {TokenType::Number, text, num};
    }

    Token readSymbol() {
        size_t start = pos_;
        while (pos_ < input_.size()) {
            char c = input_[pos_];
            if (isspace(c) || c == '(' || c == ')' || c == '"' || c == ';') {
                break;
            }
            pos_++;
        }
        return {TokenType::Symbol, input_.substr(start, pos_ - start), 0};
    }

    std::string input_;
    size_t pos_;
};

// Parser
class SExpParser {
public:
    explicit SExpParser(const std::string& input) : tokenizer_(input) {}

    SExp parse() {
        return parseExpr();
    }

    // Parse multiple top-level expressions
    std::vector<SExp> parseAll() {
        std::vector<SExp> result;
        while (tokenizer_.peek().type != TokenType::End) {
            result.push_back(parseExpr());
        }
        return result;
    }

private:
    SExp parseExpr() {
        Token t = tokenizer_.next();

        switch (t.type) {
            case TokenType::LParen: {
                SExpList list;
                while (tokenizer_.peek().type != TokenType::RParen) {
                    if (tokenizer_.peek().type == TokenType::End) {
                        throw std::runtime_error("Unexpected end of input, expected )");
                    }
                    list.push_back(parseExpr());
                }
                tokenizer_.next(); // consume ')'
                return SExp(std::move(list));
            }
            case TokenType::Number:
                return SExp(t.number);
            case TokenType::String:
            case TokenType::Symbol:
                return SExp(t.text);
            case TokenType::RParen:
                throw std::runtime_error("Unexpected )");
            case TokenType::End:
                throw std::runtime_error("Unexpected end of input");
        }
        return SExp();
    }

    Tokenizer tokenizer_;
};

// Convenience function
inline std::vector<SExp> parseSExp(const std::string& input) {
    return SExpParser(input).parseAll();
}

} // namespace parametric
