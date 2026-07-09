/*
 * Copyright (c) 2026, roit
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <cctype>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace toml {

struct ParseError : std::runtime_error {
    size_t line;
    ParseError(const std::string &msg, size_t ln) : std::runtime_error("string " + std::to_string(ln) + ": " + msg), line(ln) {
    }
};

enum class Type {
    String,
    Array,
    Bool,
    Int
};

struct Value {
    Type type = Type::String;
    std::string str;
    std::vector<std::string> arr;
    bool boolean = false;
    long long integer = 0;

    std::string asString() const {
        switch (type) {
        case Type::String:
            return str;
        case Type::Bool:
            return boolean ? "true" : "false";
        case Type::Int:
            return std::to_string(integer);
        case Type::Array: {
            std::string out;
            for (size_t i = 0; i < arr.size(); ++i) {
                if (i)
                    out += " ";
                out += arr[i];
            }
            return out;
        }
        }
        return "";
    }

    std::vector<std::string> asStringArray() const {
        if (type == Type::Array)
            return arr;
        if (type == Type::String && !str.empty())
            return {str};
        return {};
    }
};

using Table = std::map<std::string, Value>;

struct Document {
    Table root;
    std::vector<std::string> sectionOrder;
    std::map<std::string, Table> sections;
};

namespace detail {

class Cursor {
public:
    explicit Cursor(const std::string &text) : s(text) {
    }

    bool eof() const {
        return pos >= s.size();
    }

    char peek(size_t off = 0) const {
        return (pos + off < s.size()) ? s[pos + off] : '\0';
    }

    char get() {
        char c = s[pos++];
        if (c == '\n')
            line++;
        return c;
    }

    void skipWhitespaceAndComments(bool crossLines) {
        for (;;) {
            while (!eof() && (peek() == ' ' || peek() == '\t' || peek() == '\r' || (crossLines && peek() == '\n')))
                get();

            if (!eof() && peek() == '#') {
                while (!eof() && peek() != '\n')
                    get();
                continue;
            }
            break;
        }
    }

    size_t line = 1;

private:
    const std::string &s;
    size_t pos = 0;

public:
    size_t position() const {
        return pos;
    }
};

inline std::string parseBareKey(Cursor &c) {
    std::string key;
    while (!c.eof() && (std::isalnum(static_cast<unsigned char>(c.peek())) || c.peek() == '_' || c.peek() == '-'))
        key += c.get();
    if (key.empty())
        throw ParseError("Wait key", c.line);
    return key;
}

inline std::string parseBasicString(Cursor &c) {
    size_t startLine = c.line;
    if (c.peek(1) == '"' && c.peek(2) == '"') {
        c.get();
        c.get();
        c.get();

        if (c.peek() == '\n')
            c.get();
        std::string out;
        while (true) {
            if (c.eof())
                throw ParseError("unterminated multi-line string \"\"\"", startLine);
            if (c.peek() == '"' && c.peek(1) == '"' && c.peek(2) == '"') {
                c.get();
                c.get();
                c.get();
                break;
            }
            out += c.get();
        }
        return out;
    }

    c.get();
    std::string out;
    while (true) {
        if (c.eof() || c.peek() == '\n')
            throw ParseError("unterminated string \"...\"", startLine);
        char ch = c.get();
        if (ch == '"')
            break;
        if (ch == '\\') {
            if (c.eof())
                throw ParseError("unclosed escape character", c.line);
            char esc = c.get();
            switch (esc) {
            case 'n':
                out += '\n';
                break;
            case 't':
                out += '\t';
                break;
            case 'r':
                out += '\r';
                break;
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            default:
                out += esc;
                break;
            }
        }
        else
        {
            out += ch;
        }
    }
    return out;
}

inline std::string parseLiteralString(Cursor &c) {
    size_t startLine = c.line;
    if (c.peek(1) == '\'' && c.peek(2) == '\'') {
        c.get();
        c.get();
        c.get();
        if (c.peek() == '\n')
            c.get();
        std::string out;
        while (true) {
            if (c.eof())
                throw ParseError("unterminated multi-line string '''", startLine);
            if (c.peek() == '\'' && c.peek(1) == '\'' && c.peek(2) == '\'') {
                c.get();
                c.get();
                c.get();
                break;
            }
            out += c.get();
        }
        return out;
    }

    c.get();
    std::string out;
    while (true) {
        if (c.eof() || c.peek() == '\n')
            throw ParseError("unterminated string '...'", startLine);
        char ch = c.get();
        if (ch == '\'')
            break;
        out += ch;
    }
    return out;
}

Value parseValue(Cursor &c);

inline Value parseArray(Cursor &c) {
    Value v;
    v.type = Type::Array;
    c.get();
    c.skipWhitespaceAndComments(true);
    while (!c.eof() && c.peek() != ']') {
        Value item = parseValue(c);
        v.arr.push_back(item.asString());
        c.skipWhitespaceAndComments(true);
        if (!c.eof() && c.peek() == ',') {
            c.get();
            c.skipWhitespaceAndComments(true);
        }
    }
    if (c.eof())
        throw ParseError("unclosed array '['", c.line);
    c.get();
    return v;
}

inline Value parseValue(Cursor &c) {
    c.skipWhitespaceAndComments(true);
    if (c.eof())
        throw ParseError("expected value", c.line);

    char ch = c.peek();
    if (ch == '"') {
        Value v;
        v.type = Type::String;
        v.str = parseBasicString(c);
        return v;
    }
    if (ch == '\'') {
        Value v;
        v.type = Type::String;
        v.str = parseLiteralString(c);
        return v;
    }
    if (ch == '[')
        return parseArray(c);

    std::string tok;
    while (!c.eof() && c.peek() != '\n' && c.peek() != '#' && c.peek() != ',' && c.peek() != ']' && c.peek() != ' ' && c.peek() != '\t' && c.peek() != '\r')
        tok += c.get();

    if (tok == "true" || tok == "false") {
        Value v;
        v.type = Type::Bool;
        v.boolean = (tok == "true");
        return v;
    }

    bool isInt = !tok.empty();
    size_t startDigits = (tok[0] == '-' || tok[0] == '+') ? 1 : 0;
    if (startDigits >= tok.size())
        isInt = false;
    for (size_t i = startDigits; i < tok.size() && isInt; ++i)
        if (!std::isdigit(static_cast<unsigned char>(tok[i])))
            isInt = false;

    if (isInt) {
        Value v;
        v.type = Type::Int;
        v.integer = std::stoll(tok);
        return v;
    }

    throw ParseError("invalid value: '" + tok + "'", c.line);
}

}

inline Document parse(const std::string &text) {
    using namespace detail;
    Document doc;
    Cursor c(text);
    std::string currentSection;
    bool inRoot = true;

    while (true) {
        c.skipWhitespaceAndComments(true);
        if (c.eof())
            break;

        if (c.peek() == '[') {
            c.get();
            c.skipWhitespaceAndComments(false);
            std::string name;
            if (c.peek() == '"')
                name = parseBasicString(c);
            else
                name = parseBareKey(c);
            c.skipWhitespaceAndComments(false);
            if (c.eof() || c.peek() != ']')
                throw ParseError("']' expected after section name", c.line);
            c.get();
            if (doc.sections.find(name) == doc.sections.end())
                doc.sectionOrder.push_back(name);
            currentSection = name;
            inRoot = false;
            continue;
        }

        std::string key;
        if (c.peek() == '"')
            key = parseBasicString(c);
        else
            key = parseBareKey(c);

        c.skipWhitespaceAndComments(false);
        if (c.eof() || c.peek() != '=')
            throw ParseError("'=' expected after key '" + key + "'", c.line);
        c.get();

        Value v = parseValue(c);

        if (inRoot)
            doc.root[key] = v;
        else
            doc.sections[currentSection][key] = v;

        c.skipWhitespaceAndComments(false);
        if (!c.eof() && (c.peek() == '\n' || c.peek() == '\r' || c.peek() == '#'))
            continue;
        if (!c.eof() && c.peek() != '\0') {
        }
    }

    return doc;
}

}
