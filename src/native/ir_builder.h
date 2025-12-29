#ifndef SOX_IR_BUILDER_H
#define SOX_IR_BUILDER_H

#include "ir.h"
#include "../chunk.h"
#include "../object.h"

// IR builder context
typedef struct {
    ir_module_t* module;
    ir_function_t* current_function;
    ir_block_t* current_block;

    // Stack simulation for SSA construction
    ir_value_t* stack;
    int stack_count;
    int stack_capacity;

    // Local variable to register mapping
    ir_value_t* locals;
    int local_count;
    int local_capacity;

    // Mapping of compiled obj_function_t -> ir_function_t
    obj_function_t** function_keys;
    ir_function_t** function_values;
    int function_map_count;
    int function_map_capacity;

    // Global name -> function mapping for direct calls
    obj_string_t** global_function_names;
    ir_function_t** global_function_values;
    int global_function_count;
    int global_function_capacity;
} ir_builder_t;

// Create IR builder
ir_builder_t* ir_builder_new(void);
void ir_builder_free(ir_builder_t* builder);

// Build IR from bytecode
ir_module_t* ir_builder_build_module(obj_closure_t* entry_closure);

// Build IR for a single function
ir_function_t* ir_builder_build_function(ir_builder_t* builder, obj_function_t* function);

// Stack operations
void ir_builder_push(ir_builder_t* builder, ir_value_t value);
ir_value_t ir_builder_pop(ir_builder_t* builder);
ir_value_t ir_builder_peek(ir_builder_t* builder, int distance);

// Instruction emission helpers
ir_instruction_t* ir_builder_emit(ir_builder_t* builder, ir_op_t op);
ir_instruction_t* ir_builder_emit_unary(ir_builder_t* builder, ir_op_t op, ir_value_t dest, ir_value_t operand);
ir_instruction_t* ir_builder_emit_binary(ir_builder_t* builder, ir_op_t op, ir_value_t dest, ir_value_t left, ir_value_t right);

#endif
