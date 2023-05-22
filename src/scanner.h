#ifndef SOX_SCANNER_H
#define SOX_SCANNER_H

typedef enum TokenType {
    // Single-character tokens.
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR, TOKEN_COLON, TOKEN_UNDERSCORE,
    // One or two character tokens.
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    // Literals.
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,
    // Keywords.
    TOKEN_AND, TOKEN_CLASS, TOKEN_ELSE, TOKEN_FALSE,
    TOKEN_FOR, TOKEN_FOREACH, TOKEN_FUN, TOKEN_IF, TOKEN_NIL, TOKEN_OR,
    TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS,
    TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE, TOKEN_DEFER, TOKEN_IN, TOKEN_INDEX, TOKEN_VALUE,
    
    TOKEN_SWITCH, TOKEN_CASE, TOKEN_DEFAULT, TOKEN_BREAK, TOKEN_CONTINUE,

    TOKEN_ERROR, TOKEN_EOF
} TokenType;

const char * l_token_type_to_string(TokenType type);

typedef struct token_t token_t;
typedef struct token_t{
    TokenType type;
    const char* start;
    int length;
    int line;
    token_t *previous;
} token_t;

void      l_init_scanner(const char* source);
token_t * l_scan_token();

#endif