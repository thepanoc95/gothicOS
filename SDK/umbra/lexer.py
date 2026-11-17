"""Lexer for the UmbraTS source language."""

import re

KEYWORDS = {
    "import", "from", "export", "class", "static", "function", "let", "const",
    "if", "else", "while", "for", "return", "break", "continue", "true",
    "false", "null", "undefined", "void", "boolean", "number", "string",
    "int32", "uint32", "int64", "uint64", "float32", "float64", "uint8",
    "never", "unknown", "type", "interface", "enum", "abstract", "async",
    "await", "yield", "try", "catch", "finally", "throw", "new", "this",
}

OPERATORS = (
    "===", "!==", "==", "!=", "<=", ">=", "&&", "||", "++", "--",
    "+=", "-=", "*=", "/=", "=", "+", "-", "*", "/", "%", "!", "<", ">",
    "(", ")", "{", "}", "[", "]", ";", ",", ":", ".", "@", "?",
)

_IDENTIFIER = re.compile(r"[A-Za-z_$][A-Za-z0-9_$]*")
_NUMBER = re.compile(r"(?:0[xX][0-9a-fA-F]+|[0-9]+\.[0-9]+|[0-9]+)")


class LexError(Exception):
    def __init__(self, message, line, column):
        self.line = line
        self.column = column
        Exception.__init__(self, "%s at %d:%d" % (message, line, column))


class Token:
    __slots__ = ("kind", "value", "line", "column")

    def __init__(self, kind, value, line, column):
        self.kind = kind
        self.value = value
        self.line = line
        self.column = column

    def is_value(self, value):
        return self.value == value

    def __repr__(self):
        return "Token(%r, %r, %d, %d)" % (self.kind, self.value, self.line, self.column)


def tokenize(source):
    """Convert source text into a list of :class:`Token` objects."""
    tokens = []
    index = 0
    line = 1
    column = 1
    length = len(source)

    def advance(text):
        nonlocal index, line, column
        for character in text:
            if character == "\n":
                line += 1
                column = 1
            else:
                column += 1
        index += len(text)

    while index < length:
        character = source[index]
        if character in " \t\r\n":
            advance(character)
            continue
        if source.startswith("//", index):
            end = source.find("\n", index)
            advance(source[index:end if end >= 0 else length])
            continue
        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            if end < 0:
                raise LexError("unterminated block comment", line, column)
            advance(source[index:end + 2])
            continue

        match = _IDENTIFIER.match(source, index)
        if match:
            word = match.group(0)
            kind = "KEYWORD" if word in KEYWORDS else "IDENT"
            tokens.append(Token(kind, word, line, column))
            advance(word)
            continue

        match = _NUMBER.match(source, index)
        if match:
            text = match.group(0)
            tokens.append(Token("NUMBER", text, line, column))
            advance(text)
            continue

        if character in "\"'":
            quote = character
            cursor = index + 1
            value = []
            while cursor < length and source[cursor] != quote:
                if source[cursor] == "\\":
                    cursor += 1
                    if cursor >= length:
                        break
                    escapes = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", "\"": "\"", "'": "'"}
                    value.append(escapes.get(source[cursor], source[cursor]))
                else:
                    value.append(source[cursor])
                cursor += 1
            if cursor >= length:
                raise LexError("unterminated string", line, column)
            text = source[index:cursor + 1]
            tokens.append(Token("STRING", "".join(value), line, column))
            advance(text)
            continue

        operator = next((op for op in OPERATORS if source.startswith(op, index)), None)
        if operator is None:
            raise LexError("unexpected character %r" % character, line, column)
        normalized = {"===": "==", "!==": "!="}.get(operator, operator)
        tokens.append(Token("OP", normalized, line, column))
        advance(operator)

    tokens.append(Token("EOF", None, line, column))
    return tokens
