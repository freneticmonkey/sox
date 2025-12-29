#include "ir.h"
#include "../lib/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// IR module functions
ir_module_t* ir_module_new(void) {
    ir_module_t* module = (ir_module_t*)l_mem_alloc(sizeof(ir_module_t));
    module->functions = NULL;
    module->function_count = 0;
    module->function_capacity = 0;
    module->source_file = NULL;
    return module;
}

void ir_module_free(ir_module_t* module) {
    if (!module) return;

    for (int i = 0; i < module->function_count; i++) {
        ir_function_free(module->functions[i]);
    }
    l_mem_free(module->functions, sizeof(ir_function_t*) * module->function_capacity);

    if (module->source_file) {
        l_mem_free(module->source_file, strlen(module->source_file) + 1);
    }

    l_mem_free(module, sizeof(ir_module_t));
}

void ir_module_add_function(ir_module_t* module, ir_function_t* func) {
    if (module->function_capacity < module->function_count + 1) {
        int old_capacity = module->function_capacity;
        module->function_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        module->functions = (ir_function_t**)l_mem_realloc(
            module->functions,
            sizeof(ir_function_t*) * old_capacity,
            sizeof(ir_function_t*) * module->function_capacity
        );
    }
    module->functions[module->function_count++] = func;
}

// IR function functions
ir_function_t* ir_function_new(const char* name, int arity) {
    ir_function_t* func = (ir_function_t*)l_mem_alloc(sizeof(ir_function_t));
    func->name = (char*)l_mem_alloc(strlen(name) + 1);
    strcpy(func->name, name);
    func->arity = arity;
    func->entry = NULL;
    func->blocks = NULL;
    func->block_count = 0;
    func->block_capacity = 0;
    func->next_register = 0;
    func->next_label = 0;
    func->local_count = 0;
    func->upvalue_count = 0;
    func->code_offset = 0;
    return func;
}

void ir_function_free(ir_function_t* func) {
    if (!func) return;

    for (int i = 0; i < func->block_count; i++) {
        ir_block_free(func->blocks[i]);
    }
    l_mem_free(func->blocks, sizeof(ir_block_t*) * func->block_capacity);
    l_mem_free(func->name, strlen(func->name) + 1);
    l_mem_free(func, sizeof(ir_function_t));
}

ir_block_t* ir_function_new_block(ir_function_t* func) {
    int label = ir_function_alloc_label(func);
    ir_block_t* block = ir_block_new(label);

    if (func->block_capacity < func->block_count + 1) {
        int old_capacity = func->block_capacity;
        func->block_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        func->blocks = (ir_block_t**)l_mem_realloc(
            func->blocks,
            sizeof(ir_block_t*) * old_capacity,
            sizeof(ir_block_t*) * func->block_capacity
        );
    }
    func->blocks[func->block_count++] = block;

    if (!func->entry) {
        func->entry = block;
    }

    return block;
}

int ir_function_alloc_register(ir_function_t* func) {
    return func->next_register++;
}

int ir_function_alloc_label(ir_function_t* func) {
    return func->next_label++;
}

// IR block functions
ir_block_t* ir_block_new(int label) {
    ir_block_t* block = (ir_block_t*)l_mem_alloc(sizeof(ir_block_t));
    block->label = label;
    block->first = NULL;
    block->last = NULL;
    block->successors = NULL;
    block->successor_count = 0;
    block->successor_capacity = 0;
    block->predecessors = NULL;
    block->predecessor_count = 0;
    block->predecessor_capacity = 0;
    block->next = NULL;
    return block;
}

void ir_block_free(ir_block_t* block) {
    if (!block) return;

    ir_instruction_t* instr = block->first;
    while (instr) {
        ir_instruction_t* next = instr->next;
        ir_instruction_free(instr);
        instr = next;
    }

    l_mem_free(block->successors, sizeof(ir_block_t*) * block->successor_capacity);
    l_mem_free(block->predecessors, sizeof(ir_block_t*) * block->predecessor_capacity);
    l_mem_free(block, sizeof(ir_block_t));
}

void ir_block_add_instruction(ir_block_t* block, ir_instruction_t* instr) {
    if (!block->first) {
        block->first = instr;
        block->last = instr;
    } else {
        block->last->next = instr;
        block->last = instr;
    }
    instr->next = NULL;
}

void ir_block_add_successor(ir_block_t* block, ir_block_t* successor) {
    // Add to successors
    if (block->successor_capacity < block->successor_count + 1) {
        int old_capacity = block->successor_capacity;
        block->successor_capacity = (old_capacity < 4) ? 4 : old_capacity * 2;
        block->successors = (ir_block_t**)l_mem_realloc(
            block->successors,
            sizeof(ir_block_t*) * old_capacity,
            sizeof(ir_block_t*) * block->successor_capacity
        );
    }
    block->successors[block->successor_count++] = successor;

    // Add to predecessor's predecessors
    if (successor->predecessor_capacity < successor->predecessor_count + 1) {
        int old_capacity = successor->predecessor_capacity;
        successor->predecessor_capacity = (old_capacity < 4) ? 4 : old_capacity * 2;
        successor->predecessors = (ir_block_t**)l_mem_realloc(
            successor->predecessors,
            sizeof(ir_block_t*) * old_capacity,
            sizeof(ir_block_t*) * successor->predecessor_capacity
        );
    }
    successor->predecessors[successor->predecessor_count++] = block;
}

// IR instruction functions
ir_instruction_t* ir_instruction_new(ir_op_t op) {
    ir_instruction_t* instr = (ir_instruction_t*)l_mem_alloc(sizeof(ir_instruction_t));
    instr->op = op;
    instr->dest = (ir_value_t){0};
    instr->operand1 = (ir_value_t){0};
    instr->operand2 = (ir_value_t){0};
    instr->operand3 = (ir_value_t){0};
    instr->call_args = NULL;
    instr->call_arg_count = 0;
    instr->call_target = NULL;
    instr->call_function = NULL;
    instr->line = 0;
    instr->next = NULL;
    return instr;
}

void ir_instruction_free(ir_instruction_t* instr) {
    if (!instr) return;
    if (instr->call_args) {
        l_mem_free(instr->call_args, sizeof(ir_value_t) * instr->call_arg_count);
    }
    if (instr->string_data) {
        l_mem_free((void*)instr->string_data, instr->string_length + 1);
    }
    l_mem_free(instr, sizeof(ir_instruction_t));
}

// IR value construction
ir_value_t ir_value_register(int reg) {
    ir_value_t val;
    val.type = IR_VAL_REGISTER;
    val.size = IR_SIZE_16BYTE;  // Virtual registers hold value_t (16 bytes)
    val.as.reg = reg;
    return val;
}

ir_value_t ir_value_constant(value_t val) {
    ir_value_t irval;
    irval.type = IR_VAL_CONSTANT;
    irval.size = IR_SIZE_16BYTE;  // Constants are value_t (16 bytes)
    irval.as.constant = val;
    return irval;
}

ir_value_t ir_value_label(int label) {
    ir_value_t val;
    val.type = IR_VAL_LABEL;
    val.size = IR_SIZE_8BYTE;  // Labels are just block identifiers
    val.as.label = label;
    return val;
}

ir_value_t ir_value_function(ir_function_t* func) {
    ir_value_t val;
    val.type = IR_VAL_FUNCTION;
    val.size = IR_SIZE_16BYTE;  // Function references are wrapped in value_t (16 bytes)
    val.as.func = func;
    return val;
}

// IR printing/debugging
static const char* ir_op_name(ir_op_t op) {
    switch (op) {
        case IR_CONST_INT: return "const_int";
        case IR_CONST_FLOAT: return "const_float";
        case IR_CONST_NIL: return "const_nil";
        case IR_CONST_BOOL: return "const_bool";
        case IR_ADD: return "add";
        case IR_SUB: return "sub";
        case IR_MUL: return "mul";
        case IR_DIV: return "div";
        case IR_NEG: return "neg";
        case IR_EQ: return "eq";
        case IR_NE: return "ne";
        case IR_LT: return "lt";
        case IR_LE: return "le";
        case IR_GT: return "gt";
        case IR_GE: return "ge";
        case IR_NOT: return "not";
        case IR_AND: return "and";
        case IR_OR: return "or";
        case IR_LOAD_LOCAL: return "load_local";
        case IR_STORE_LOCAL: return "store_local";
        case IR_LOAD_GLOBAL: return "load_global";
        case IR_STORE_GLOBAL: return "store_global";
        case IR_LOAD_UPVALUE: return "load_upvalue";
        case IR_STORE_UPVALUE: return "store_upvalue";
        case IR_GET_PROPERTY: return "get_property";
        case IR_SET_PROPERTY: return "set_property";
        case IR_GET_INDEX: return "get_index";
        case IR_SET_INDEX: return "set_index";
        case IR_JUMP: return "jump";
        case IR_BRANCH: return "branch";
        case IR_CALL: return "call";
        case IR_RETURN: return "return";
        case IR_RUNTIME_CALL: return "runtime_call";
        case IR_PRINT: return "print";
        case IR_PUSH: return "push";
        case IR_POP: return "pop";
        case IR_TYPE_CHECK: return "type_check";
        case IR_CAST: return "cast";
        case IR_NEW_ARRAY: return "new_array";
        case IR_NEW_TABLE: return "new_table";
        case IR_NEW_CLOSURE: return "new_closure";
        case IR_NEW_CLASS: return "new_class";
        case IR_NEW_INSTANCE: return "new_instance";
        case IR_PHI: return "phi";
        case IR_MOVE: return "move";
        default: return "unknown";
    }
}

static void ir_value_print(ir_value_t val) {
    switch (val.type) {
        case IR_VAL_REGISTER:
            printf("%%r%d", val.as.reg);
            break;
        case IR_VAL_CONSTANT:
            printf("$");
            l_print_value(val.as.constant);
            break;
        case IR_VAL_LABEL:
            printf("L%d", val.as.label);
            break;
        case IR_VAL_FUNCTION:
            printf("@%s", val.as.func->name);
            break;
    }
}

void ir_instruction_print(ir_instruction_t* instr) {
    printf("  ");

    // Print destination if present
    if (instr->dest.type != 0) {
        ir_value_print(instr->dest);
        printf(" = ");
    }

    printf("%s", ir_op_name(instr->op));

    // Print operands
    if (instr->operand1.type != 0) {
        printf(" ");
        ir_value_print(instr->operand1);
    }
    if (instr->operand2.type != 0) {
        printf(", ");
        ir_value_print(instr->operand2);
    }
    if (instr->operand3.type != 0) {
        printf(", ");
        ir_value_print(instr->operand3);
    }

    printf("\n");
}

void ir_block_print(ir_block_t* block) {
    printf("L%d:\n", block->label);

    ir_instruction_t* instr = block->first;
    while (instr) {
        ir_instruction_print(instr);
        instr = instr->next;
    }
}

void ir_function_print(ir_function_t* func) {
    printf("function %s(arity=%d) {\n", func->name, func->arity);
    printf("  locals: %d, upvalues: %d, registers: %d\n",
           func->local_count, func->upvalue_count, func->next_register);

    for (int i = 0; i < func->block_count; i++) {
        ir_block_print(func->blocks[i]);
    }

    printf("}\n\n");
}

void ir_module_print(ir_module_t* module) {
    printf("IR Module");
    if (module->source_file) {
        printf(" (%s)", module->source_file);
    }
    printf(":\n\n");

    for (int i = 0; i < module->function_count; i++) {
        ir_function_print(module->functions[i]);
    }
}
