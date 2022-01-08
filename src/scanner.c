#include <stdio.h>
#include <string.h>

#include "scanner.h"

#include "common.h"

typedef struct {
    const char* start;
    const char* current;
    int line;
} _scanner_t;

_scanner_t _scanner;

void l_init_scanner(const char* source) {
    _scanner.start = source;
    _scanner.current = _scanner.start;
    _scanner.line = 1;
}

static bool _is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool _is_alpha(char c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
          c == '_';
}

static bool _is_at_end() {
    return *_scanner.current == '\0';
}

static char _advance() {
    _scanner.current++;
    return _scanner.current[-1];
}

static char _peek() {
    return *_scanner.current;
}

static char _peek_next() {
    if (_is_at_end()) 
        return '\0';
    return _scanner.current[1];
}

static bool _match(char expected) {
    if (_is_at_end()) 
        return false;

    if (*_scanner.current != expected) 
        return false;

    _scanner.current++;
    return true;
}

static token_t _make_token(TokenType type) {
    token_t token;
    token.type = type;
    token.start = _scanner.start;
    token.length = (int)(_scanner.current - _scanner.start);
    token.line = _scanner.line;
    return token;
}

static token_t _error_token(const char* message) {
    token_t token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = _scanner.line;
    return token;
}

static void _skip_whitespace() {
    for (;;) {
        char c = _peek();
        switch (c) {
            // handle within line whitespace
            case ' ':
            case '\r':
            case '\t':
                _advance();
                break;
            // handle newlines
            case '\n':
                _scanner.line++;
                _advance();
                break;

            // handle comments
            case '/': {
                if (_peek_next() == '/') {
                    // A comment goes until the end of the line.
                    while (_peek() != '\n' && !_is_at_end()) 
                        _advance();
                } 
                else {
                    return;
                }
                break;
            }

            default:
                return;
        }
    }
}

static TokenType _check_keyword(int start, int length, const char* rest, TokenType type) {
    if (_scanner.current - _scanner.start == start + length &&
        memcmp(_scanner.start + start, rest, length) == 0) {
        return type;
    }

  return TOKEN_IDENTIFIER;
}

static TokenType _identifier_type() {
    switch (_scanner.start[0]) {
        case 'a': return _check_keyword(1, 2, "nd", TOKEN_AND);
        case 'b': return _check_keyword(1, 4, "reak", TOKEN_BREAK);
        case 'c':
            if (_scanner.current - _scanner.start > 1) {
                switch (_scanner.start[1]) {
                    case 'l': return _check_keyword(2, 3, "ass", TOKEN_CLASS);
                    case 'a': return _check_keyword(2, 2, "se", TOKEN_CASE);
                }
            }
            break;
        case 'd':
            if (_scanner.current - _scanner.start > 1) {
                switch (_scanner.start[3]) {
                    case 'e': return _check_keyword(4, 1, "r", TOKEN_DEFER);
                    case 'a': return _check_keyword(4, 3, "ult", TOKEN_DEFAULT);
                }
            }
        case 'e': return _check_keyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (_scanner.current - _scanner.start > 1) {
                switch (_scanner.start[1]) {
                    case 'a': return _check_keyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return _check_keyword(2, 1, "r", TOKEN_FOR);
                    case 'n': return _check_keyword(2, 0, "", TOKEN_FUN);
                }
            }
            break;
        case 'i': return _check_keyword(1, 1, "f", TOKEN_IF);
        case 'n': return _check_keyword(1, 2, "il", TOKEN_NIL);
        case 'o': return _check_keyword(1, 1, "r", TOKEN_OR);
        case 'p': return _check_keyword(1, 4, "rint", TOKEN_PRINT);
        case 'r': return _check_keyword(1, 5, "eturn", TOKEN_RETURN);
        case 's': 
            if (_scanner.current - _scanner.start > 1) {
                switch (_scanner.start[1]) {
                    case 'u': return _check_keyword(2, 3, "per", TOKEN_SUPER);
                    case 'w': return _check_keyword(2, 4, "itch", TOKEN_SWITCH);
                }
            }
            break;
        case 't':
            if (_scanner.current - _scanner.start > 1) {
                switch (_scanner.start[1]) {
                    case 'h': return _check_keyword(2, 2, "is", TOKEN_THIS);
                    case 'r': return _check_keyword(2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;
        case 'v': return _check_keyword(1, 2, "ar", TOKEN_VAR);
        case 'w': return _check_keyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static token_t _identifier() {
    while (_is_alpha(_peek()) || _is_digit(_peek())) 
        _advance();
    return _make_token(_identifier_type());
}

static token_t _string() {
    while (_peek() != '"' && !_is_at_end()) {
        if (_peek() == '\n') 
            _scanner.line++;
        _advance();
    }

    if (_is_at_end()) 
        return _error_token("Unterminated string.");

    // The closing quote.
    _advance();
    return _make_token(TOKEN_STRING);
}

static token_t _number() {
  while (_is_digit(_peek())) 
    _advance();

  // Look for a fractional part.
  if (_peek() == '.' && _is_digit(_peek_next())) {
    // Consume the ".".
    _advance();

    while (_is_digit(_peek())) 
        _advance();
  }

  return _make_token(TOKEN_NUMBER);
}

token_t l_scan_token() {
    _skip_whitespace();
    _scanner.start = _scanner.current;

    if (_is_at_end()) 
        return _make_token(TOKEN_EOF);

    char c = _advance();

    if (_is_alpha(c)) 
        return _identifier();

    if (_is_digit(c)) 
        return _number();

    switch (c) {
        case '(': return _make_token(TOKEN_LEFT_PAREN);
        case ')': return _make_token(TOKEN_RIGHT_PAREN);
        case '{': return _make_token(TOKEN_LEFT_BRACE);
        case '}': return _make_token(TOKEN_RIGHT_BRACE);
        case ';': return _make_token(TOKEN_SEMICOLON);
        case ',': return _make_token(TOKEN_COMMA);
        case '.': return _make_token(TOKEN_DOT);
        case '-': return _make_token(TOKEN_MINUS);
        case '+': return _make_token(TOKEN_PLUS);
        case '/': return _make_token(TOKEN_SLASH);
        case '*': return _make_token(TOKEN_STAR);
        case '!': {
            return _make_token(
                _match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG
            );
        }
        case '=': {
            return _make_token(
                _match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL
            );
        }
        case '<': {
            return _make_token(
                _match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS
            );
        }
        case '>': {
            return _make_token(
                _match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER
            );
        }
        case '"': return _string();
    }

    return _error_token("Unexpected character.");
}

