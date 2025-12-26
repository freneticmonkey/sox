#ifndef SOX_IR_H
#define SOX_IR_H

#include "../common.h"
#include "../value.h"

// Forward declarations
typedef struct ir_function_t ir_function_t;
typedef struct ir_block_t ir_block_t;
typedef struct ir_instruction_t ir_instruction_t;

// IR instruction types
typedef enum {
    // Constants and immediate values
    IR_CONST_INT,      // Load integer constant
    IR_CONST_FLOAT,    // Load float constant
    IR_CONST_NIL,      // Load nil
    IR_CONST_BOOL,     // Load boolean
    IR_CONST_STRING,   // Load string constant (allocates at runtime)

    // Arithmetic operations
    IR_ADD,            // Add two values
    IR_SUB,            // Subtract two values
    IR_MUL,            // Multiply two values
    IR_DIV,            // Divide two values
    IR_NEG,            // Negate value

    // Comparison operations
    IR_EQ,             // Equal comparison
    IR_NE,             // Not equal comparison
    IR_LT,             // Less than
    IR_LE,             // Less than or equal
    IR_GT,             // Greater than
    IR_GE,             // Greater than or equal

    // Logical operations
    IR_NOT,            // Logical not
    IR_AND,            // Logical and
    IR_OR,             // Logical or

    // Memory operations
    IR_LOAD_LOCAL,     // Load local variable
    IR_STORE_LOCAL,    // Store local variable
    IR_LOAD_GLOBAL,    // Load global variable
    IR_STORE_GLOBAL,   // Store global variable
    IR_LOAD_UPVALUE,   // Load upvalue
    IR_STORE_UPVALUE,  // Store upvalue

    // Object operations
    IR_GET_PROPERTY,   // Get object property
    IR_SET_PROPERTY,   // Set object property
    IR_GET_INDEX,      // Array/table indexing (get)
    IR_SET_INDEX,      // Array/table indexing (set)

    // Control flow
    IR_JUMP,           // Unconditional jump
    IR_BRANCH,         // Conditional branch
    IR_CALL,           // Function call
    IR_RETURN,         // Return from function

    // Runtime operations
    IR_RUNTIME_CALL,   // Call runtime function
    IR_PRINT,          // Print value

    // Stack operations
    IR_PUSH,           // Push value to stack
    IR_POP,            // Pop value from stack

    // Type operations
    IR_TYPE_CHECK,     // Runtime type check
    IR_CAST,           // Type cast

    // Object creation
    IR_NEW_ARRAY,      // Create new array
    IR_NEW_TABLE,      // Create new table
    IR_NEW_CLOSURE,    // Create new closure
    IR_NEW_CLASS,      // Create new class
    IR_NEW_INSTANCE,   // Create new instance

    // Special
    IR_PHI,            // SSA phi node
    IR_MOVE,           // Move value between registers
} ir_op_t;

// IR value size classification
typedef enum {
    IR_SIZE_8BYTE,   // Scalar value: fits in single 64-bit register
    IR_SIZE_16BYTE,  // Composite value (value_t): requires register pair
} ir_value_size_t;

// IR value types
typedef enum {
    IR_VAL_REGISTER,   // Virtual register
    IR_VAL_CONSTANT,   // Compile-time constant
    IR_VAL_LABEL,      // Basic block label
    IR_VAL_FUNCTION,   // Function reference
} ir_value_type_t;

// IR value representation
typedef struct {
    ir_value_type_t type;
    ir_value_size_t size;  // Size classification (8-byte or 16-byte)
    union {
        int reg;           // Virtual register number
        value_t constant;  // Constant value
        int label;         // Block label
        ir_function_t* func;  // Function pointer
    } as;
} ir_value_t;

// IR instruction
struct ir_instruction_t {
    ir_op_t op;
    ir_value_t dest;       // Destination (usually a register)
    ir_value_t operand1;   // First operand
    ir_value_t operand2;   // Second operand
    ir_value_t operand3;   // Third operand (for some instructions)

    // Call-specific data (for IR_CALL, IR_RUNTIME_CALL)
    ir_value_t* call_args; // Array of call arguments
    int call_arg_count;    // Number of call arguments
    const char* call_target; // Target function name (for external calls)

    // String constant data (for IR_CONST_STRING)
    const char* string_data;  // String literal characters
    size_t string_length;     // String length

    int line;              // Source line number for debugging

    ir_instruction_t* next;
};

// Basic block
struct ir_block_t {
    int label;                    // Block identifier
    ir_instruction_t* first;      // First instruction
    ir_instruction_t* last;       // Last instruction

    ir_block_t** successors;      // Successor blocks
    int successor_count;
    int successor_capacity;

    ir_block_t** predecessors;    // Predecessor blocks
    int predecessor_count;
    int predecessor_capacity;

    ir_block_t* next;             // Next block in function
};

// IR function
struct ir_function_t {
    char* name;                   // Function name
    int arity;                    // Number of parameters

    ir_block_t* entry;            // Entry basic block
    ir_block_t** blocks;          // All basic blocks
    int block_count;
    int block_capacity;

    int next_register;            // Next virtual register number
    int next_label;               // Next block label

    // Metadata
    int local_count;              // Number of local variables
    int upvalue_count;            // Number of upvalues
};

// IR module (collection of functions)
typedef struct {
    ir_function_t** functions;
    int function_count;
    int function_capacity;

    char* source_file;            // Original source file
} ir_module_t;

// IR construction functions
ir_module_t* ir_module_new(void);
void ir_module_free(ir_module_t* module);
void ir_module_add_function(ir_module_t* module, ir_function_t* func);

ir_function_t* ir_function_new(const char* name, int arity);
void ir_function_free(ir_function_t* func);
ir_block_t* ir_function_new_block(ir_function_t* func);

ir_block_t* ir_block_new(int label);
void ir_block_free(ir_block_t* block);
void ir_block_add_instruction(ir_block_t* block, ir_instruction_t* instr);
void ir_block_add_successor(ir_block_t* block, ir_block_t* successor);

ir_instruction_t* ir_instruction_new(ir_op_t op);
void ir_instruction_free(ir_instruction_t* instr);

// IR value construction
ir_value_t ir_value_register(int reg);
ir_value_t ir_value_constant(value_t val);
ir_value_t ir_value_label(int label);
ir_value_t ir_value_function(ir_function_t* func);

// IR register allocation
int ir_function_alloc_register(ir_function_t* func);
int ir_function_alloc_label(ir_function_t* func);

// IR printing/debugging
void ir_module_print(ir_module_t* module);
void ir_function_print(ir_function_t* func);
void ir_block_print(ir_block_t* block);
void ir_instruction_print(ir_instruction_t* instr);

#endif
