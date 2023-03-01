#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"

#include "common.h"
#include "scanner.h"
#include "lib/memory.h"

#ifdef DEBUG_PRINT_CODE
#include "lib/debug.h"
#endif

#define GEN_VAR_MAX 1024 * 1024

#define MAX_BRANCHES 32

typedef struct {
    token_t current;
    token_t previous;
    bool    had_error;
    bool    panic_mode;
} _parser_t;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*parse_func)(bool canAssign);

typedef struct {
    parse_func prefix;
    parse_func infix;
    Precedence precedence;
} parse_rule_t;

typedef struct {
    token_t name;
    int     depth;
    bool    is_captured;
} local_t;

typedef struct {
  uint8_t index;
  bool is_local;
} upvalue_t;

typedef enum FunctionType {
    TYPE_FUNCTION,
    TYPE_DEFER,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_SCRIPT
} FunctionType;

// bringing loop tracking across from Wren for break and continue functionality
typedef struct loop_t loop_t;

typedef struct loop_t {
    // The loop enclosing this one or NULL if the outermost loop
    loop_t *enclosing;

    // Index of the instruction that the loop should jump back to.
    int start;

    // Index of the argument for the CODE_JUMP_IF instruction used to exit the
    // loop. Stored so we can patch it once we know where the loop ends.
    int exit_jump;

    // Index of the first instruction of the body of the loop.
    int body;

    // Depth of the scope(s) that need to be exited if a break is hit inside the
    // loop.
    int scope_depth;

} loop_t;

typedef struct switch_t switch_t;

typedef struct switch_t {
    switch_t *enclosing;
    int      start;
    int      default_jump;
    uint8_t  global_variable;
} switch_t;

typedef struct compiler_t compiler_t;
typedef struct compiler_t {
    compiler_t*     enclosing;
    obj_function_t* function;
    FunctionType    type;

    local_t         locals[UINT8_COUNT];
    int             local_count;
    upvalue_t       upvalues[UINT8_COUNT];
    int             scope_depth;

    uint8_t         deferred_functions[UINT8_COUNT];
    int             defer_count;

    // current loop being compiled or NULL if no loop
    loop_t         *loop;
    
    // current switch being compiled or NULL if no switch
    switch_t       *_switch;

    int             main_function;
} compiler_t;

typedef struct class_compiler_t class_compiler_t;
typedef struct class_compiler_t {
    class_compiler_t* enclosing;
    bool              has_superclass;
} class_compiler_t;

char _generated_variables_block[GEN_VAR_MAX];
int  _gen_var_offset = 0;

_parser_t         _parser;
compiler_t*       _current = NULL;
class_compiler_t* _current_class;
chunk_t*          _compiling_chunk;

static chunk_t* _current_chunk() {
    return &_current->function->chunk;
}

static void _error_at(token_t* token, const char* message) {
    if (_parser.panic_mode)
        return;
    
    _parser.panic_mode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } 
    else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    _parser.had_error = true;
}

static void _error(const char* message) {
    _error_at(&_parser.previous, message);
}

static void _error_at_current(const char* message) {
    _error_at(&_parser.current, message);
}

static void _advance() {
    _parser.previous = _parser.current;
    
    for (;;) {
        _parser.current = l_scan_token();
        if (_parser.current.type != TOKEN_ERROR) 
            break;

        _error_at_current(_parser.current.start);
    }

#ifdef DEBUG_PRINT_TOKENS
    fprintf(stdout, " `%.*s` ", _parser.current.length, _parser.current.start);
    fflush(stdout);
#endif
}

static void _optional_consume(TokenType type, const char* message) {
    if (_parser.current.type == type) {
        _advance();
        return;
    }
    // else don't do anything?
}

static void _consume(TokenType type, const char* message) {
    if (_parser.current.type == type) {
        _advance();
        return;
    }

    _error_at_current(message);
}

static bool _check(TokenType type) {
    return _parser.current.type == type;
}

static bool _match(TokenType type) {
    if ( !_check(type) )
        return false;

    _advance();
    return true;
}

static void _emit_byte(uint8_t byte) {
    l_write_chunk(_current_chunk(), byte, _parser.previous.line);
}

static void _emit_bytes(uint8_t byte1, uint8_t byte2) {
    _emit_byte(byte1);
    _emit_byte(byte2);
}

static void _emit_loop(int loopStart) {
    _emit_byte(OP_LOOP);

    int offset = _current_chunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) 
        _error("Loop body too large.");

    _emit_byte((offset >> 8) & 0xff);
    _emit_byte(offset & 0xff);
}

static int _emit_jump(uint8_t instruction) {
    _emit_byte(instruction);
    _emit_byte(0xff);
    _emit_byte(0xff);
    return _current_chunk()->count - 2;
}

static void _emit_jump_location(int location) {
    _emit_byte(OP_JUMP);
    uint8_t a = (location>>8) & 0xff;
    uint8_t b =  location & 0xff;
    _emit_bytes( a, b );
}

static void _emit_return() {
    if (_current->type == TYPE_INITIALIZER) {
        _emit_bytes(OP_GET_LOCAL, 0);
    } else {
        _emit_byte(OP_NIL);
    }
    _emit_byte(OP_RETURN);
}

static uint8_t _make_constant(value_t value) {
    int constant = l_add_constant(_current_chunk(), value);
    if (constant > UINT8_MAX) {
        _error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void _emit_constant(value_t value) {
    _emit_bytes(OP_CONSTANT, _make_constant(value));
}

static void _patch_jump(int offset) {
    // -2 to adjust for the bytecode for the jump offset itself.
    int jump = _current_chunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        _error("Too much code to jump over.");
    }

    _current_chunk()->code[offset] = (jump >> 8) & 0xff;
    _current_chunk()->code[offset + 1] = jump & 0xff;
}

static void l_init_compiler(compiler_t* compiler, FunctionType type) {

    // if this is the first time a compiler object has been setup, then
    // initialise global state
    if ( compiler->enclosing == NULL ) {
        memset(&_generated_variables_block, 0, GEN_VAR_MAX);

#ifdef DEBUG_PRINT_CODE
        printf(" == COMPILER START == ");
#endif

    }    

    // set the enclosing compiler if one exists
    compiler->enclosing = ( _current != NULL ) ? _current : NULL;

    compiler->function = NULL;
    compiler->type = type;

    compiler->loop = NULL;
    compiler->_switch = NULL;

    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->defer_count = 0;

    compiler->function = l_new_function();

    _current = compiler;

    if (type != TYPE_SCRIPT) {
        _current->function->name = l_copy_string(_parser.previous.start,
                                                 _parser.previous.length);
    }

    local_t* local = &_current->locals[_current->local_count++];
    local->depth = 0;
    local->is_captured = false;

    if (type != TYPE_FUNCTION) {
        local->name.start = "this";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }

    compiler->main_function = -1;
}

static obj_function_t* _end_compiler() {

    // Call any deferred functions before returning
    if ( _current->defer_count > 0 ) {
        for (int i = 0; i < _current->defer_count ; i++ ) {
            // obj_function_t defer_func = _current->deferred_functions[i];
            // if ( defer_func->type != TYPE_DEFER )
            //     _error("Can't read local variable in its own initializer.");
            
            _emit_bytes(OP_GET_LOCAL, _current->deferred_functions[i]);
            _emit_bytes(OP_CALL, 0);
        }
    }
    _emit_return();
    obj_function_t* function = _current->function;
#ifdef DEBUG_PRINT_CODE
    if (!_parser.had_error) {
        l_dissassemble_chunk(
            _current_chunk(), 
            function->name != NULL ? function->name->chars : "<script>"
        );
    }
#endif
    _current = _current->enclosing;
    return function;
}

static void _begin_scope() {
    _current->scope_depth++;
}

static void _end_scope() {
    _current->scope_depth--;

    while (_current->local_count > 0 &&
           _current->locals[_current->local_count - 1].depth > _current->scope_depth) {
        if (_current->locals[_current->local_count - 1].is_captured) {
            _emit_byte(OP_CLOSE_UPVALUE);
        } else {
            _emit_byte(OP_POP);
        }
        _current->local_count--;
    }
}

static void          _expression();
static void          _statement();
static void          _declaration();
static parse_rule_t* _get_rule(TokenType type);
static void          _parse_precedence(Precedence precedence);

static uint8_t 
_identifier_constant(token_t* name) {
    return _make_constant(OBJ_VAL(l_copy_string(name->start,
                                                name->length)));
}

static bool _identifiers_equal(token_t* a, token_t* b) {
    if (a->length != b->length) 
        return false;
        
    return memcmp(a->start, b->start, a->length) == 0;
}

static int _resolve_local(compiler_t* compiler, token_t* name) {
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        local_t* local = &compiler->locals[i];
        if (_identifiers_equal(name, &local->name)) {
            if (local->depth == -1) {
                _error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

static int _add_upvalue(compiler_t* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalue_count;

    for (int i = 0; i < upvalueCount; i++) {
        upvalue_t* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->is_local == isLocal) {
            return i;
        }

        if (upvalueCount == UINT8_COUNT) {
            _error("Too many closure variables in function.");
            return 0;
        }
    }

    compiler->upvalues[upvalueCount].is_local = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalue_count++;
}

static int _resolve_upvalue(compiler_t* compiler, token_t* name) {
    if (compiler->enclosing == NULL) 
        return -1;

    int local = _resolve_local(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].is_captured = true;
        return _add_upvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = _resolve_upvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return _add_upvalue(compiler, (uint8_t)upvalue, false);
    }
        
    return -1;
}

static void _add_local(token_t name) {
    if ( _current->local_count > UINT8_COUNT ) {
        _error("Too many local variables in function");
        return;
    }
    local_t* local = &_current->locals[_current->local_count++];
    local->name = name;
    local->depth = -1;
    local->is_captured = false;
}

static uint8_t _generate_variable(TokenType type, const char *hint) {
    if (_current->scope_depth == 0) 
        return 0;

    // generate a unique name for this variable

    if ( (strlen(hint) + _gen_var_offset) >= GEN_VAR_MAX )
        _error("Too many generated values in script (defer).");

    char* temp_name =  &_generated_variables_block[_gen_var_offset];
    sprintf(temp_name, "%s_%d", hint, _current->local_count);
    int length = strlen(temp_name);
    _gen_var_offset += length;
    
    token_t name = {
        .type = type,
        .start = temp_name,
        .length = length,
        .line = 0,
    };

    for (int i = _current->local_count - 1; i >= 0; i--) {
        local_t* local = &_current->locals[i];
        if (local->depth != -1 && local->depth < _current->scope_depth) {
            break; 
        }

        if (_identifiers_equal(&name, &local->name)) {
            _error("Already a variable with this name in this scope.");
        }
    }
    _add_local(name);

    return _identifier_constant(&name);
}

static void _declare_variable() {
    if (_current->scope_depth == 0) 
        return;

    token_t* name = &_parser.previous;
    for (int i = _current->local_count - 1; i >= 0; i--) {
        local_t* local = &_current->locals[i];
        if (local->depth != -1 && local->depth < _current->scope_depth) {
            break; 
        }

        if (_identifiers_equal(name, &local->name)) {
            _error("Already a variable with this name in this scope.");
        }
    }
    _add_local(*name);
}

static uint8_t _parse_variable(const char* errorMessage) {
    _consume(TOKEN_IDENTIFIER, errorMessage);

    _declare_variable();
    if ( _current->scope_depth > 0 )
        return 0;

    return _identifier_constant(&_parser.previous);
}

static void _mark_initialized() {
    if (_current->scope_depth == 0)
        return;
     _current->locals[_current->local_count - 1].depth = _current->scope_depth;
}

static void _define_variable(uint8_t global) {
    if ( _current->scope_depth > 0 ) {
        _mark_initialized();
        return;
    }
    _emit_bytes(OP_DEFINE_GLOBAL, global);
}

static uint8_t _argument_list() {
    uint8_t argCount = 0;
    if (!_check(TOKEN_RIGHT_PAREN)) {
        do {
            _expression();
            if (argCount == 255) {
                _error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (_match(TOKEN_COMMA));
    }
    _consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

static void _and_(bool canAssign) {
    int endJump = _emit_jump(OP_JUMP_IF_FALSE);

    _emit_byte(OP_POP);
    _parse_precedence(PREC_AND);

    _patch_jump(endJump);
}

static void _binary(bool canAssign) {
    TokenType operatorType = _parser.previous.type;
    parse_rule_t* rule = _get_rule(operatorType);
    _parse_precedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_BANG_EQUAL:    _emit_bytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL:   _emit_byte(OP_EQUAL); break;
        case TOKEN_GREATER:       _emit_byte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: _emit_bytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS:          _emit_byte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:    _emit_bytes(OP_GREATER, OP_NOT); break;
        case TOKEN_PLUS:          _emit_byte(OP_ADD); break;
        case TOKEN_MINUS:         _emit_byte(OP_SUBTRACT); break;
        case TOKEN_STAR:          _emit_byte(OP_MULTIPLY); break;
        case TOKEN_SLASH:         _emit_byte(OP_DIVIDE); break;
        default: 
            return; // Unreachable.
    }
}

static void _call(bool canAssign) {
    uint8_t argCount = _argument_list();
    _emit_bytes(OP_CALL, argCount);
}

static void _dot(bool canAssign) {
    _consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = _identifier_constant(&_parser.previous);

    if (canAssign && _match(TOKEN_EQUAL)) {
        _expression();
        _emit_bytes(OP_SET_PROPERTY, name);
    } else if ( _match(TOKEN_LEFT_PAREN) ) {
        uint8_t argCount = _argument_list();
        _emit_bytes(OP_INVOKE, name);
        _emit_byte(argCount);
    } else {
        _emit_bytes(OP_GET_PROPERTY, name);
    }
}

static void _literal(bool canAssign) {
    switch (_parser.previous.type) {
        case TOKEN_FALSE: _emit_byte(OP_FALSE); break;
        case TOKEN_NIL:   _emit_byte(OP_NIL); break;
        case TOKEN_TRUE:  _emit_byte(OP_TRUE); break;
        default: 
            return; // Unreachable.
    }
}

static void _grouping(bool canAssign) {
    _expression();
    _consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void _number(bool canAssign) {
    double number = strtod(_parser.previous.start, NULL);
    _emit_constant(NUMBER_VAL(number));
}

static void _or_(bool canAssign) {
    int elseJump = _emit_jump(OP_JUMP_IF_FALSE);
    int endJump = _emit_jump(OP_JUMP);

    _patch_jump(elseJump);
    _emit_byte(OP_POP);

    _parse_precedence(PREC_OR);
    _patch_jump(endJump);
}

static void _string(bool canAssign) {
    _emit_constant(
        OBJ_VAL(
            l_copy_string(_parser.previous.start  + 1,
                          _parser.previous.length - 2)
        )
    );
}

static void _named_variable(token_t name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = _resolve_local(_current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = _resolve_upvalue(_current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = _identifier_constant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign && _match(TOKEN_EQUAL)) {
        _expression();
        _emit_bytes(setOp, (uint8_t)arg);
    } else {
        _emit_bytes(getOp, (uint8_t)arg);
    }
}

static void _variable(bool canAssign) {
    _named_variable(_parser.previous, canAssign);
}

static token_t _synthetic_token(const char* text) {
    token_t token;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}

static void _super_(bool canAssign) {
    if (_current_class == NULL) {
        _error("Can't use 'super' outside of a class.");
    } else if (!_current_class->has_superclass) {
        _error("Can't use 'super' in a class with no superclass.");
    }

    _consume(TOKEN_DOT, "Expect '.' after 'super'.");
    _consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    uint8_t name = _identifier_constant(&_parser.previous);

    _named_variable(_synthetic_token("this"), false);
    if (_match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = _argument_list();
        _named_variable(_synthetic_token("super"), false);
        _emit_bytes(OP_SUPER_INVOKE, name);
        _emit_byte(argCount);
    } else {
        _named_variable(_synthetic_token("super"), false);
        _emit_bytes(OP_GET_SUPER, name);
    }
}

static void _this_(bool canAssign) {
    if (_current_class == NULL) {
        _error("Can't use 'this' outside of a class.");
        return;
    }
    _variable(false);
}

static void _index(bool canAssign) {
    _expression();
    _consume(TOKEN_RIGHT_BRACKET, "Expect ']' after indexing expression.");

    if (canAssign && _match(TOKEN_EQUAL)) {
        _expression();
        _emit_byte(OP_SET_INDEX);
    } else {
        _emit_byte(OP_GET_INDEX);
    }
}

static void _unary(bool canAssign) {
    TokenType operatorType = _parser.previous.type;

    // Compile the operand.
    _parse_precedence(PREC_UNARY);

    // Emit the operator instruction.
    switch (operatorType) {
        case TOKEN_BANG:  _emit_byte(OP_NOT); break;
        case TOKEN_MINUS: _emit_byte(OP_NEGATE); break;
        default: 
            return; // Unreachable.
    }
}

parse_rule_t rules[] = {
    [TOKEN_LEFT_PAREN]    = {_grouping, _call,    PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,     PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,     PREC_NONE}, 
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,     PREC_NONE},
    [TOKEN_LEFT_BRACKET]  = {NULL,     _index,   PREC_CALL},
    [TOKEN_RIGHT_BRACKET] = {NULL,     NULL,     PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,     PREC_NONE},
    [TOKEN_DOT]           = {NULL,     _dot,     PREC_CALL},
    [TOKEN_MINUS]         = {_unary,   _binary,  PREC_TERM},
    [TOKEN_PLUS]          = {NULL,     _binary,  PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,     PREC_NONE},
    [TOKEN_SLASH]         = {NULL,     _binary,  PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,     _binary,  PREC_FACTOR},
    [TOKEN_BANG]          = {_unary,   NULL,     PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     _binary,  PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,     NULL,     PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     _binary,  PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,     _binary,  PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     _binary,  PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     _binary,  PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     _binary,  PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {_variable,NULL,     PREC_NONE},
    [TOKEN_STRING]        = {_string,  NULL,     PREC_NONE},
    [TOKEN_NUMBER]        = {_number,  NULL,     PREC_NONE},
    [TOKEN_AND]           = {NULL,     _and_,    PREC_AND},
    [TOKEN_CLASS]         = {NULL,     NULL,     PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,     PREC_NONE},
    [TOKEN_FALSE]         = {_literal, NULL,     PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,     PREC_NONE},
    [TOKEN_FUN]           = {NULL,     NULL,     PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,     PREC_NONE},
    [TOKEN_NIL]           = {_literal, NULL,     PREC_NONE},
    [TOKEN_OR]            = {NULL,     _or_,     PREC_OR},
    [TOKEN_PRINT]         = {NULL,     NULL,     PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,     PREC_NONE},
    [TOKEN_SUPER]         = {_super_,  NULL,     PREC_NONE},
    [TOKEN_THIS]          = {_this_,   NULL,     PREC_NONE},
    [TOKEN_TRUE]          = {_literal, NULL,     PREC_NONE},
    [TOKEN_VAR]           = {NULL,     NULL,     PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,     PREC_NONE},
    [TOKEN_DEFER]         = {NULL,     NULL,     PREC_NONE},
    [TOKEN_SWITCH]        = {NULL,     NULL,     PREC_NONE},
    [TOKEN_CASE]          = {NULL,     _binary,  PREC_NONE},
    [TOKEN_DEFAULT]       = {NULL,     NULL,     PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,     PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,     PREC_NONE},
};

static void _parse_precedence(Precedence precedence) {
    _advance();

    parse_func prefixRule = _get_rule(_parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        _error("Expect expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= _get_rule(_parser.current.type)->precedence) {
        _advance();
        parse_func infixRule = _get_rule(_parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && _match(TOKEN_EQUAL)) {
        _error("Invalid assignment target.");
    }
}

static parse_rule_t* _get_rule(TokenType type) {
  return &rules[type];
}

// Patch all placeholder ops in the current code to jumps to the current location
static void _patch_jumps(int from_location, OpCode patch_code) {
    int i = from_location;
    int skip_size = 0;
    chunk_t *chunk = &_current->function->chunk;

    while ( i < chunk->count) {

        // if a break op code is found
        if ( chunk->code[i] == patch_code ) {
            // update it to a jump
            chunk->code[i] = OP_JUMP;
            // and patch the current location
            _patch_jump(i+1);
            i += 3;

        } else {
            // otherwise skip forward in the chunk using the operation type appropriate size
            skip_size = l_op_get_arg_size_bytes(chunk, i);

            // if the bytecode wasn't able to be correctly read, display an error and return
            // this is not ideal as it will just result in an error and corrupted bytecode
            // should really be stopping execution entirely
            if (skip_size == -1) {
                _error("Invalid chunk data detected while patching jumps.");
                return;
            }

            i += 1 + skip_size;
        }
    }
}

static void _loop_start(loop_t *loop) {
    loop->enclosing = _current->loop;
    loop->start = _current_chunk()->count;
    loop->scope_depth = _current->scope_depth;
    loop->exit_jump = -1;
    _current->loop = loop;
}

static void _loop_update_start(int location) {
    _current->loop->start = location;
}

static void _loop_jump() {
    _patch_jumps(_current->loop->start, OP_CONTINUE);
    _emit_loop(_current->loop->start);
}

static void _loop_test_exit() {
    _current->loop->exit_jump = _emit_jump(OP_JUMP_IF_FALSE);
    // pop the condition check result from the stack
    _emit_byte(OP_POP);
}

static void _loop_body(int body_offset) {
    _current->loop->body = body_offset;
}

static void _loop_end() {
    _emit_loop(_current->loop->start);
    if ( _current->loop->exit_jump != -1 ) {
        _patch_jump(_current->loop->exit_jump);
        // pop the condition check result from the stack
        _emit_byte(OP_POP);
    }

    _patch_jumps(_current->loop->body, OP_BREAK);

    _current->loop = _current->loop->enclosing;
}

static void _switch_start(switch_t *_switch, uint8_t global) {

    _switch->enclosing = _current->_switch;
    _switch->start = _current_chunk()->count;
    _switch->default_jump = -1;
    _switch->global_variable = global;

    _current->_switch = _switch;
}

static bool _switch_found_default() {
    return _current->_switch->default_jump != -1;
}

static void _switch_get_variable() {
    _emit_bytes(OP_GET_GLOBAL, _current->_switch->global_variable);
}

static void _switch_set_default_location() {
    _current->_switch->default_jump = _current_chunk()->count;
}

static void _switch_jump_default() {
    _emit_loop(_current->_switch->default_jump);
}

static void _switch_end() {

    _patch_jumps(_current->_switch->start, OP_BREAK);

    _current->_switch = _current->_switch->enclosing;
}

static void _expression() {
    _parse_precedence(PREC_ASSIGNMENT);
}

static void _block() {
    while (!_check(TOKEN_RIGHT_BRACE) && !_check(TOKEN_EOF)) {
        _declaration();
    }

    _consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void _function(FunctionType type) {
    compiler_t compiler;
    l_init_compiler(&compiler, type);
    _begin_scope();

    if ( type == TYPE_DEFER ) {
        _consume(TOKEN_LEFT_PAREN, "Expect '(' after function declaration.");
    } else {
        _consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    }

    if ( !_check(TOKEN_RIGHT_PAREN) ) {
        do {
            _current->function->arity++;
            if (_current->function->arity > 255) {
                _error_at_current("Can't have more than 255 parameters.");
            }
            uint8_t constant = _parse_variable("Expect parameter name.");
            _define_variable(constant);
        } while (_match(TOKEN_COMMA));
    }

    _consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    _consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    _block();

    obj_function_t* function = _end_compiler();
    _emit_bytes(OP_CLOSURE, _make_constant(OBJ_VAL(function)));

    for (int i = 0; i < function->upvalue_count; i++) {
        _emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
        _emit_byte(compiler.upvalues[i].index);
    }
}

static void _method() {
    _consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t constant = _identifier_constant(&_parser.previous);

    FunctionType type = TYPE_METHOD;
    if (_parser.previous.length == 4 &&
        memcmp(_parser.previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }
    _function(type);

    _emit_bytes(OP_METHOD, constant);
}

static void _class_declaration() {
    _consume(TOKEN_IDENTIFIER, "Expect class name.");
    token_t className = _parser.previous;
    uint8_t nameConstant = _identifier_constant(&_parser.previous);
    _declare_variable();

    _emit_bytes(OP_CLASS, nameConstant);
    _define_variable(nameConstant);

    class_compiler_t classCompiler;
    classCompiler.enclosing = _current_class;
    classCompiler.has_superclass = false;
    _current_class = &classCompiler;

    if (_match(TOKEN_LESS)) {
        _consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        _variable(false);
        if (_identifiers_equal(&className, &_parser.previous)) {
           _error("A class can't inherit from itself.");
        }
        
        _begin_scope();
        _add_local(_synthetic_token("super"));
        _define_variable(0);

        _named_variable(className, false);
        _emit_byte(OP_INHERIT);
        classCompiler.has_superclass = true;
    }

    _named_variable(className, false);

    _consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");

    while (!_check(TOKEN_RIGHT_BRACE) && !_check(TOKEN_EOF)) {
        _method();
    }

    _consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
    _emit_byte(OP_POP);

    if (classCompiler.has_superclass) {
        _end_scope();
    }

    _current_class = _current_class->enclosing;
}

static void _defer_declaration() {

    uint8_t global = _generate_variable(TYPE_DEFER, "defer");
    _mark_initialized();
    _function(TYPE_DEFER);
    _define_variable(global);

    _current->deferred_functions[_current->defer_count++] = _current->local_count - 1;
}

static void _fun_declaration() {
    uint8_t global = _parse_variable("Expect function name.");
    
    if (_current->enclosing == NULL) {
        if ( strncmp(_parser.previous.start, "main", 4) == 0 ) {
            if (_current->main_function != -1) {
                _error_at_current("Cannot have more than one main()");
            }
            _current->main_function = global;
        }
    }
    
    _mark_initialized();
    _function(TYPE_FUNCTION);
    _define_variable(global);

}

static void _var_declaration() {
    uint8_t global = _parse_variable("Expect variable name.");

    if (_match(TOKEN_EQUAL)) {
        _expression();
    } else {
        _emit_byte(OP_NIL);
    }
    _optional_consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    _define_variable(global);
}

static void _expression_statement() {
    _expression();
    _optional_consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    _emit_byte(OP_POP);
}

static void _for_statement() {
    _begin_scope();

    // Process initializer + expression
    if (_match(TOKEN_SEMICOLON)) {
        // No initializer.
    } else if (_match(TOKEN_VAR)) {
        _var_declaration();
    } else {
        _expression_statement();
    }
    
    loop_t loop;
    _loop_start(&loop);
    
    // Process the for loop exit condition
    if (!_match(TOKEN_SEMICOLON)) {
        _expression();
        _consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // Jump out of the loop if the condition is false.
        _loop_test_exit();
    }

    // process the increment

    if (!_match(TOKEN_LEFT_BRACE)) {
        // set a jump location to skip to the loop body
        int bodyJump = _emit_jump(OP_JUMP);

        // store the increment location
        int incrementStart = _current_chunk()->count;
        
        // increment
        _expression();
        _emit_byte(OP_POP);
        _check(TOKEN_LEFT_BRACE);
        
        // now set a jump to location for the loop end condition check
        _loop_jump();

        // this will only be executed on entering the for loop
        // the jumps being setup will ensure that it is jumped over on
        // every subsequent iteration

        // reset the loop start to the beginning of the increment.
        _loop_update_start(incrementStart);

        // patch the pre-increment jump to the body location
        // this will skip the increment code after the loop end condition has been checked
        _patch_jump(bodyJump);
    }
    
    _statement();

    // jump to either the increment code, or if no increment the loop end condition check
    _loop_jump();

    _loop_end();

    _end_scope();
}

static void _if_statement() {

    // if

    // process the logical expression
    _expression();

    // {

    // run the 'then' statement
    int thenJump = _emit_jump(OP_JUMP_IF_FALSE);
    _emit_byte(OP_POP);
    _statement();

    // finshed then - jump over the 'else' statement
    int elseJump = _emit_jump(OP_JUMP);

    // }

    // else {
    // otherwise run the 'else' statement
    _patch_jump(thenJump);
    _emit_byte(OP_POP);

    if (_match(TOKEN_ELSE))
        _statement();
    
    // }

    // set the post if statement jump point
    _patch_jump(elseJump);
}

// static void _case_statement(int *endJumps, int *branchCount, int *defaultLocation, uint8_t switch_var) {
static void _case_statement() {

    if (_match(TOKEN_CASE)) {
        // Push the switched variable onto the stack
        _switch_get_variable();

        // read the case variable
        _expression();

        // compare
        _emit_byte(OP_EQUAL);

        // run the 'case' statement
        int thenJump = _emit_jump(OP_JUMP_IF_FALSE);
        _emit_byte(OP_POP);

        // check if there is a statement block for this case
        if ( _check(TOKEN_LEFT_BRACE) ) {
            // patch any fall through statements to this block
            _patch_jumps(_current->_switch->start, OP_CASE_FALLTHROUGH);

            // then run the statement
            _statement();

            // finshed now jump to the end of the switch statement
            _emit_jump(OP_BREAK);

        } else {
            // insert a jump marker to jump to the next defined block
            _emit_jump(OP_CASE_FALLTHROUGH);
        }
        
        _patch_jump(thenJump);
        // pop the equal result
        _emit_byte(OP_POP);
    }

    // check for a switch default if one hasn't already been found
    if ( !_switch_found_default() && _match(TOKEN_DEFAULT)) {
        // create a jump point to skip the default statement by errr default
        // this will ensure that the default statement will be run after all 
        // of the case statements have been checked
        int skipJump = _emit_jump(OP_JUMP);
        
        // Get the location of the beginning of the default statement
        _switch_set_default_location();
        // *defaultLocation = _current_chunk()->count;

        _statement();

        // jump to the end of the switch statement
        _emit_jump(OP_BREAK);
        
        // patch the jump for the skip jump
        _patch_jump(skipJump);
    }

    // check if there are more case statements
    if (_check(TOKEN_CASE)) {
        _case_statement();
    }
}

static void _switch_statement() {
    
    // setup a global for the switched value
    uint8_t global = _generate_variable(TOKEN_SWITCH, "switch_var");
    _mark_initialized();

    // read the value to be switched on
    _expression();
    
    // store it in the global
    _define_variable(global);

    switch_t _switch;
    _switch_start(&_switch, global);
    
    _consume(TOKEN_LEFT_BRACE, "Expect '{' before switch body.");
    _begin_scope();

    if (_check(TOKEN_CASE) || _check(TOKEN_DEFAULT) ) {
        _case_statement();
    }

    // Insert a jump (loop = backwards jump) to the default case location.
    // This will be skipped to the jump patches below if a case statement is run
    _switch_jump_default();

    // set the return jump points for all case and default branches
    // for ( int i = 0; i < branchCount; i++) {
    //     _patch_jump(endJumps[i]);
    // }

    // patch all instances of OP_BREAK
    _switch_end();

    _end_scope();

    _consume(TOKEN_RIGHT_BRACE, "Expect '}' after switch body.");
}

static void _print_statement() {
    _expression();
    _optional_consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    _emit_byte(OP_PRINT);
}

static void _break_statement() {
    if ( _current->loop == NULL && _current->_switch == NULL ) {
        _error("Can't use break outside of a loop or switch statement.");
        return;
    }
    _optional_consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    _emit_jump(OP_BREAK);
}

static void _continue_statement() {
    if ( _current->loop == NULL) {
        _error("Can't use continue outside of a loop statement.");
        return;
    }
    _optional_consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    _emit_jump(OP_CONTINUE);
}

static void _return_statement() {
    if (_current->type == TYPE_SCRIPT) {
        _error("Can't return from top-level code.");
    }

    if (_match(TOKEN_SEMICOLON)) {
        _emit_return();
    } else {
        if (_current->type == TYPE_INITIALIZER) {
            _error("Can't return a value from an initializer.");
        }
        _expression();
        _optional_consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        _emit_byte(OP_RETURN);
    }
}

static void _while_statement() {
    loop_t loop;
    _loop_start(&loop);

    _expression();

    _loop_test_exit();

    _statement();
    
    _loop_jump();

    _loop_end();
}

static void _synchronize() {
    _parser.panic_mode = false;

    while (_parser.current.type != TOKEN_EOF) {
        if (_parser.previous.type == TOKEN_SEMICOLON) 
            return;
        
        switch (_parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_SWITCH:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;

            default:
                ; // Do nothing.
        }

        _advance();
    }
}

static void _declaration() {

    if ( _match(TOKEN_CLASS) ) {
        _class_declaration();
    } else if (_match(TOKEN_DEFER)) {
        _defer_declaration();
    } else if ( _match(TOKEN_FUN) ) {
        _fun_declaration();
    } else if ( _match(TOKEN_VAR) ) {
        _var_declaration();
    } else {
        _statement();
    }

    if ( _parser.panic_mode )
        _synchronize();
}

static void _statement() {
    if ( _match(TOKEN_BREAK) ) {
        _break_statement();
    } else if ( _match(TOKEN_CONTINUE) ) {
        _continue_statement();
    } else if ( _match(TOKEN_PRINT) ) {
        _print_statement();
    } else if ( _match(TOKEN_IF) ) {
        _if_statement();
    } else if ( _match(TOKEN_SWITCH) ) {
        _switch_statement();
    } else if ( _match(TOKEN_RETURN) ) {
        _return_statement();
    } else if ( _match(TOKEN_WHILE) ) {
        _while_statement();
    } else if ( _match(TOKEN_FOR) ) {
        _for_statement();
    } else if ( _match(TOKEN_LEFT_BRACE) ) {
        _begin_scope();
        _block();
        _end_scope();
    } else {
        _expression_statement();
    }
}

obj_function_t* l_compile(const char* source) {
    l_init_scanner(source);
    compiler_t compiler;

    l_init_compiler(&compiler, TYPE_SCRIPT);

    _parser.had_error = false;
    _parser.panic_mode = false;

    _advance();

    while (!_match(TOKEN_EOF)) {
        _declaration();
    }

    // if the main function is defined, call it
    if ( compiler.main_function != -1 ) {

        // Create argc + argv parameters
        
        // uint8_t global = _generate_variable(TOKEN_IDENTIFIER, "argc");
        // _mark_initialized();

        // _named_variable(_synthetic_token("argc"), false);

        // // store it in the global
        // _define_variable(global);


        _emit_bytes(OP_GET_GLOBAL, compiler.main_function);

        _emit_constant((value_t){
            .type = VAL_NUMBER,
            .as.number = 1,
        });

        _emit_constant((value_t){
            .type = VAL_OBJ,
            .as.obj = (obj_t*)l_copy_string("params", 6),
        });

        _emit_bytes(OP_CALL, 2);
    }

    obj_function_t* function = _end_compiler();

    return _parser.had_error ? NULL : function;    
}

void l_mark_compiler_roots() {
    compiler_t* compiler = _current;
    while (compiler != NULL) {
        l_mark_object((obj_t*)compiler->function);
        compiler = compiler->enclosing;
    }
}
