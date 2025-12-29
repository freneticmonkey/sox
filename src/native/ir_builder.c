#include "ir_builder.h"
#include "../lib/memory.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ir_builder_t* ir_builder_new(void) {
    ir_builder_t* builder = (ir_builder_t*)l_mem_alloc(sizeof(ir_builder_t));
    builder->module = NULL;
    builder->current_function = NULL;
    builder->current_block = NULL;
    builder->stack = NULL;
    builder->stack_count = 0;
    builder->stack_capacity = 0;
    builder->locals = NULL;
    builder->local_count = 0;
    builder->local_capacity = 0;
    builder->function_keys = NULL;
    builder->function_values = NULL;
    builder->function_map_count = 0;
    builder->function_map_capacity = 0;
    builder->global_function_names = NULL;
    builder->global_function_values = NULL;
    builder->global_function_count = 0;
    builder->global_function_capacity = 0;
    return builder;
}

void ir_builder_free(ir_builder_t* builder) {
    if (!builder) return;

    if (builder->stack) {
        l_mem_free(builder->stack, sizeof(ir_value_t) * builder->stack_capacity);
    }
    if (builder->locals) {
        l_mem_free(builder->locals, sizeof(ir_value_t) * builder->local_capacity);
    }
    if (builder->function_keys) {
        l_mem_free(builder->function_keys,
                   sizeof(obj_function_t*) * builder->function_map_capacity);
    }
    if (builder->function_values) {
        l_mem_free(builder->function_values,
                   sizeof(ir_function_t*) * builder->function_map_capacity);
    }
    if (builder->global_function_names) {
        l_mem_free(builder->global_function_names,
                   sizeof(obj_string_t*) * builder->global_function_capacity);
    }
    if (builder->global_function_values) {
        l_mem_free(builder->global_function_values,
                   sizeof(ir_function_t*) * builder->global_function_capacity);
    }

    l_mem_free(builder, sizeof(ir_builder_t));
}

void ir_builder_push(ir_builder_t* builder, ir_value_t value) {
    if (builder->stack_capacity < builder->stack_count + 1) {
        int old_capacity = builder->stack_capacity;
        builder->stack_capacity = (old_capacity < 8) ? 8 : old_capacity * 2;
        builder->stack = (ir_value_t*)l_mem_realloc(
            builder->stack,
            sizeof(ir_value_t) * old_capacity,
            sizeof(ir_value_t) * builder->stack_capacity
        );
    }
    builder->stack[builder->stack_count++] = value;
}

ir_value_t ir_builder_pop(ir_builder_t* builder) {
    if (builder->stack_count > 0) {
        return builder->stack[--builder->stack_count];
    }
    // Return a nil register as default
    return ir_value_register(-1);
}

ir_value_t ir_builder_peek(ir_builder_t* builder, int distance) {
    if (builder->stack_count > distance) {
        return builder->stack[builder->stack_count - 1 - distance];
    }
    return ir_value_register(-1);
}

ir_instruction_t* ir_builder_emit(ir_builder_t* builder, ir_op_t op) {
    ir_instruction_t* instr = ir_instruction_new(op);
    ir_block_add_instruction(builder->current_block, instr);
    return instr;
}

ir_instruction_t* ir_builder_emit_unary(ir_builder_t* builder, ir_op_t op,
                                        ir_value_t dest, ir_value_t operand) {
    ir_instruction_t* instr = ir_builder_emit(builder, op);
    instr->dest = dest;
    instr->operand1 = operand;
    return instr;
}

ir_instruction_t* ir_builder_emit_binary(ir_builder_t* builder, ir_op_t op,
                                         ir_value_t dest, ir_value_t left,
                                         ir_value_t right) {
    ir_instruction_t* instr = ir_builder_emit(builder, op);
    instr->dest = dest;
    instr->operand1 = left;
    instr->operand2 = right;
    return instr;
}

static ir_function_t* ir_builder_lookup_function(ir_builder_t* builder, obj_function_t* function) {
    for (int i = 0; i < builder->function_map_count; i++) {
        if (builder->function_keys[i] == function) {
            return builder->function_values[i];
        }
    }
    return NULL;
}

static void ir_builder_record_function(ir_builder_t* builder, obj_function_t* function,
                                       ir_function_t* ir_func) {
    if (builder->function_map_capacity < builder->function_map_count + 1) {
        int old_capacity = builder->function_map_capacity;
        builder->function_map_capacity = (old_capacity < 4) ? 4 : old_capacity * 2;
        builder->function_keys = (obj_function_t**)l_mem_realloc(
            builder->function_keys,
            sizeof(obj_function_t*) * old_capacity,
            sizeof(obj_function_t*) * builder->function_map_capacity
        );
        builder->function_values = (ir_function_t**)l_mem_realloc(
            builder->function_values,
            sizeof(ir_function_t*) * old_capacity,
            sizeof(ir_function_t*) * builder->function_map_capacity
        );
    }

    builder->function_keys[builder->function_map_count] = function;
    builder->function_values[builder->function_map_count] = ir_func;
    builder->function_map_count++;
}

static void ir_builder_record_global_function(ir_builder_t* builder, obj_string_t* name,
                                              ir_function_t* func) {
    if (!name || !func) return;
    if (builder->global_function_capacity < builder->global_function_count + 1) {
        int old_capacity = builder->global_function_capacity;
        builder->global_function_capacity = (old_capacity < 4) ? 4 : old_capacity * 2;
        builder->global_function_names = (obj_string_t**)l_mem_realloc(
            builder->global_function_names,
            sizeof(obj_string_t*) * old_capacity,
            sizeof(obj_string_t*) * builder->global_function_capacity
        );
        builder->global_function_values = (ir_function_t**)l_mem_realloc(
            builder->global_function_values,
            sizeof(ir_function_t*) * old_capacity,
            sizeof(ir_function_t*) * builder->global_function_capacity
        );
    }

    builder->global_function_names[builder->global_function_count] = name;
    builder->global_function_values[builder->global_function_count] = func;
    builder->global_function_count++;
}

static ir_function_t* ir_builder_lookup_global_function(ir_builder_t* builder, obj_string_t* name) {
    if (!name) return NULL;
    for (int i = 0; i < builder->global_function_count; i++) {
        if (builder->global_function_names[i] == name) {
            return builder->global_function_values[i];
        }
    }
    return NULL;
}

static void sanitize_symbol(const char* input, char* output, size_t output_len) {
    if (!input || output_len == 0) return;
    size_t write_index = 0;
    for (size_t i = 0; input[i] != '\0' && write_index < output_len - 1; i++) {
        char c = input[i];
        if (isalnum((unsigned char)c) || c == '_') {
            output[write_index++] = c;
        } else {
            output[write_index++] = '_';
        }
    }
    output[write_index] = '\0';
}

static void ir_builder_ensure_locals(ir_builder_t* builder, int count) {
    if (builder->local_capacity < count) {
        int old_capacity = builder->local_capacity;
        builder->local_capacity = count;
        builder->locals = (ir_value_t*)l_mem_realloc(
            builder->locals,
            sizeof(ir_value_t) * old_capacity,
            sizeof(ir_value_t) * builder->local_capacity
        );
    }
    builder->local_count = count;
}

// Translate bytecode to IR
static ir_function_t* ir_builder_create_function(obj_function_t* function) {
    const char* base_name = function->name ? function->name->chars : "<script>";
    char sanitized[96];
    sanitize_symbol(base_name, sanitized, sizeof(sanitized));
    char name_buffer[160];
    snprintf(name_buffer, sizeof(name_buffer), "%s_fn_%p", sanitized, (void*)function);
    return ir_function_new(name_buffer, function->arity);
}

static void ir_builder_build_function_body(ir_builder_t* builder, obj_function_t* function,
                                          ir_function_t* ir_func) {
    builder->current_function = ir_func;
    builder->current_block = ir_function_new_block(ir_func);
    builder->stack_count = 0;

    // Set up local variables
    ir_func->local_count = (int)function->chunk.constants.count + 10; // Estimate
    ir_func->upvalue_count = function->upvalue_count;
    ir_builder_ensure_locals(builder, ir_func->local_count);

    chunk_t* chunk = &function->chunk;

    // PASS 1: Identify all jump targets and create labels for them
    int* offset_to_label = (int*)l_mem_alloc(sizeof(int) * chunk->count);
    for (int i = 0; i < chunk->count; i++) {
        offset_to_label[i] = -1;  // -1 means not a jump target
    }

    int scan_ip = 0;
    while (scan_ip < chunk->count) {
        uint8_t instruction = chunk->code[scan_ip];
        scan_ip++;

        switch (instruction) {
            case OP_JUMP:
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = (chunk->code[scan_ip] << 8) | chunk->code[scan_ip + 1];
                scan_ip += 2;
                int target = scan_ip + offset;
                if (target < chunk->count && offset_to_label[target] == -1) {
                    offset_to_label[target] = ir_function_alloc_label(ir_func);
                }
                break;
            }
            case OP_LOOP: {
                uint16_t offset = (chunk->code[scan_ip] << 8) | chunk->code[scan_ip + 1];
                scan_ip += 2;
                int target = scan_ip - offset;
                if (target >= 0 && target < chunk->count && offset_to_label[target] == -1) {
                    offset_to_label[target] = ir_function_alloc_label(ir_func);
                }
                break;
            }
            case OP_CONSTANT:
            case OP_GET_LOCAL:
            case OP_SET_LOCAL:
            case OP_GET_GLOBAL:
            case OP_DEFINE_GLOBAL:
            case OP_SET_GLOBAL:
            case OP_GET_UPVALUE:
            case OP_SET_UPVALUE:
            case OP_GET_PROPERTY:
            case OP_SET_PROPERTY:
            case OP_CLASS:
                scan_ip++; // These have 1-byte operands
                break;
            case OP_CLOSURE: {
                uint8_t constant_idx = chunk->code[scan_ip++];
                if (constant_idx < chunk->constants.count) {
                    value_t constant = chunk->constants.values[constant_idx];
                    if (IS_OBJ(constant) && AS_OBJ(constant)->type == OBJ_FUNCTION) {
                        obj_function_t* func_obj = AS_FUNCTION(constant);
                        scan_ip += func_obj->upvalue_count * 2;
                    }
                }
                break;
            }
            case OP_CALL:
                scan_ip++; // CALL has arg count
                break;
            default:
                break; // Other instructions have no operands
        }
    }

    // PASS 2: Build IR
    int ip = 0;

    while (ip < chunk->count) {
        // Check if this position is a jump target - start new block if so
        if (offset_to_label[ip] != -1 && builder->current_block->first != NULL) {
            // We need to start a new block
            ir_block_t* new_block = ir_function_new_block(ir_func);
            new_block->label = offset_to_label[ip];
            builder->current_block = new_block;
        } else if (offset_to_label[ip] != -1) {
            // This is the first block and it's a jump target - assign the label
            builder->current_block->label = offset_to_label[ip];
        }
        uint8_t instruction = chunk->code[ip];
        int line = chunk->lines[ip];
        (void)line; // Line number available for debugging if needed
        ip++;

        switch (instruction) {
            case OP_CONSTANT: {
                uint8_t constant_idx = chunk->code[ip++];
                value_t constant = chunk->constants.values[constant_idx];

                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);
                ir_value_t src = ir_value_constant(constant);

                // Determine constant type and emit appropriate instruction
                if (IS_NUMBER(constant)) {
                    ir_builder_emit_unary(builder, IR_CONST_FLOAT, dest, src);
                } else if (IS_BOOL(constant)) {
                    ir_builder_emit_unary(builder, IR_CONST_BOOL, dest, src);
                } else if (IS_NIL(constant)) {
                    ir_builder_emit(builder, IR_CONST_NIL)->dest = dest;
                } else if (IS_STRING(constant)) {
                    // String constant - extract string data for runtime allocation
                    obj_string_t* str = AS_STRING(constant);
                    ir_instruction_t* instr = ir_builder_emit(builder, IR_CONST_STRING);
                    instr->dest = dest;
                    // Copy string data (will be freed when IR is freed)
                    instr->string_length = str->length;
                    char* copied_str = (char*)l_mem_alloc(str->length + 1);
                    memcpy(copied_str, str->chars, str->length);
                    copied_str[str->length] = '\0';
                    instr->string_data = copied_str;
                } else {
                    // Other object constant (not supported yet in native code)
                    ir_builder_emit_unary(builder, IR_CONST_INT, dest, src);
                }

                ir_builder_push(builder, dest);
                break;
            }

            case OP_NIL: {
                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);
                ir_builder_emit(builder, IR_CONST_NIL)->dest = dest;
                ir_builder_push(builder, dest);
                break;
            }

            case OP_TRUE: {
                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);
                ir_value_t val = ir_value_constant(BOOL_VAL(true));
                ir_builder_emit_unary(builder, IR_CONST_BOOL, dest, val);
                ir_builder_push(builder, dest);
                break;
            }

            case OP_FALSE: {
                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);
                ir_value_t val = ir_value_constant(BOOL_VAL(false));
                ir_builder_emit_unary(builder, IR_CONST_BOOL, dest, val);
                ir_builder_push(builder, dest);
                break;
            }

            case OP_POP: {
                ir_builder_pop(builder);
                break;
            }

            case OP_GET_LOCAL: {
                uint8_t slot = chunk->code[ip++];
                if (slot < builder->local_count && builder->locals[slot].type != 0) {
                    ir_builder_push(builder, builder->locals[slot]);
                } else {
                    int reg = ir_function_alloc_register(ir_func);
                    ir_value_t dest = ir_value_register(reg);
                    ir_value_t local_idx = ir_value_constant(NUMBER_VAL(slot));
                    ir_builder_emit_unary(builder, IR_LOAD_LOCAL, dest, local_idx);
                    ir_builder_push(builder, dest);
                }
                break;
            }

            case OP_SET_LOCAL: {
                uint8_t slot = chunk->code[ip++];
                ir_value_t value = ir_builder_peek(builder, 0);
                ir_value_t local_idx = ir_value_constant(NUMBER_VAL(slot));
                ir_builder_emit_binary(builder, IR_STORE_LOCAL, local_idx, value, (ir_value_t){0});

                if (slot < builder->local_capacity) {
                    builder->locals[slot] = value;
                }
                break;
            }

            case OP_GET_GLOBAL: {
                uint8_t constant_idx = chunk->code[ip++];
                value_t name_value = chunk->constants.values[constant_idx];
                obj_string_t* name_str = NULL;
                if (IS_OBJ(name_value) && AS_OBJ(name_value)->type == OBJ_STRING) {
                    name_str = AS_STRING(name_value);
                }
                if (name_str) {
                    ir_function_t* target = ir_builder_lookup_global_function(builder, name_str);
                    if (target) {
                        ir_builder_push(builder, ir_value_function(target));
                        break;
                    }
                }
                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);
                ir_value_t name = ir_value_constant(name_value);
                ir_builder_emit_unary(builder, IR_LOAD_GLOBAL, dest, name);
                ir_builder_push(builder, dest);
                break;
            }

            case OP_DEFINE_GLOBAL:
            case OP_SET_GLOBAL: {
                uint8_t constant_idx = chunk->code[ip++];
                ir_value_t value = ir_builder_pop(builder);
                value_t name_value = chunk->constants.values[constant_idx];
                ir_value_t name = ir_value_constant(name_value);
                if (value.type == IR_VAL_FUNCTION && IS_OBJ(name_value) && AS_OBJ(name_value)->type == OBJ_STRING) {
                    ir_builder_record_global_function(builder, AS_STRING(name_value), value.as.func);
                }
                ir_builder_emit_binary(builder, IR_STORE_GLOBAL, name, value, (ir_value_t){0});
                break;
            }

            case OP_GET_UPVALUE: {
                uint8_t slot = chunk->code[ip++];
                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);
                ir_value_t upvalue_idx = ir_value_constant(NUMBER_VAL(slot));
                ir_builder_emit_unary(builder, IR_LOAD_UPVALUE, dest, upvalue_idx);
                ir_builder_push(builder, dest);
                break;
            }

            case OP_SET_UPVALUE: {
                uint8_t slot = chunk->code[ip++];
                ir_value_t value = ir_builder_peek(builder, 0);
                ir_value_t upvalue_idx = ir_value_constant(NUMBER_VAL(slot));
                ir_builder_emit_binary(builder, IR_STORE_UPVALUE, upvalue_idx, value, (ir_value_t){0});
                break;
            }

            case OP_GET_PROPERTY: {
                uint8_t constant_idx = chunk->code[ip++];
                ir_value_t object = ir_builder_pop(builder);
                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);
                ir_value_t name = ir_value_constant(chunk->constants.values[constant_idx]);
                ir_builder_emit_binary(builder, IR_GET_PROPERTY, dest, object, name);
                ir_builder_push(builder, dest);
                break;
            }

            case OP_SET_PROPERTY: {
                uint8_t constant_idx = chunk->code[ip++];
                ir_value_t value = ir_builder_pop(builder);
                ir_value_t object = ir_builder_pop(builder);
                ir_value_t name = ir_value_constant(chunk->constants.values[constant_idx]);
                ir_instruction_t* instr = ir_builder_emit(builder, IR_SET_PROPERTY);
                instr->dest = object;
                instr->operand1 = name;
                instr->operand2 = value;
                ir_builder_push(builder, value);
                break;
            }

            case OP_GET_INDEX: {
                ir_value_t index = ir_builder_pop(builder);
                ir_value_t object = ir_builder_pop(builder);
                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);
                ir_builder_emit_binary(builder, IR_GET_INDEX, dest, object, index);
                ir_builder_push(builder, dest);
                break;
            }

            case OP_SET_INDEX: {
                ir_value_t value = ir_builder_pop(builder);
                ir_value_t index = ir_builder_pop(builder);
                ir_value_t object = ir_builder_pop(builder);
                ir_instruction_t* instr = ir_builder_emit(builder, IR_SET_INDEX);
                instr->dest = object;
                instr->operand1 = index;
                instr->operand2 = value;
                ir_builder_push(builder, value);
                break;
            }

            case OP_ADD:
            case OP_SUBTRACT:
            case OP_MULTIPLY:
            case OP_DIVIDE:
            case OP_EQUAL:
            case OP_GREATER:
            case OP_LESS: {
                ir_value_t right = ir_builder_pop(builder);
                ir_value_t left = ir_builder_pop(builder);
                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);

                ir_op_t ir_op;
                switch (instruction) {
                    case OP_ADD: ir_op = IR_ADD; break;
                    case OP_SUBTRACT: ir_op = IR_SUB; break;
                    case OP_MULTIPLY: ir_op = IR_MUL; break;
                    case OP_DIVIDE: ir_op = IR_DIV; break;
                    case OP_EQUAL: ir_op = IR_EQ; break;
                    case OP_GREATER: ir_op = IR_GT; break;
                    case OP_LESS: ir_op = IR_LT; break;
                    default: ir_op = IR_ADD;
                }

                // Infer destination register size based on operation type
                if (instruction == OP_EQUAL || instruction == OP_GREATER || instruction == OP_LESS) {
                    // Comparisons always return value_t (16-byte) from runtime functions
                    dest.size = IR_SIZE_16BYTE;
                } else {
                    // Arithmetic operations (ADD, SUB, MUL, DIV):
                    // Result is 16-byte if any operand is 16-byte, else 8-byte
                    if (left.size == IR_SIZE_16BYTE || right.size == IR_SIZE_16BYTE) {
                        dest.size = IR_SIZE_16BYTE;
                    } else {
                        dest.size = IR_SIZE_8BYTE;
                    }
                }

                ir_builder_emit_binary(builder, ir_op, dest, left, right);
                ir_builder_push(builder, dest);
                break;
            }

            case OP_NEGATE:
            case OP_NOT: {
                ir_value_t operand = ir_builder_pop(builder);
                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);
                ir_op_t ir_op = (instruction == OP_NEGATE) ? IR_NEG : IR_NOT;

                // Infer destination register size based on operation type
                if (instruction == OP_NOT) {
                    // NOT always returns value_t (16-byte) from runtime functions
                    dest.size = IR_SIZE_16BYTE;
                } else {
                    // NEGATE inherits operand size
                    dest.size = operand.size;
                }

                ir_builder_emit_unary(builder, ir_op, dest, operand);
                ir_builder_push(builder, dest);
                break;
            }

            case OP_PRINT: {
                ir_value_t value = ir_builder_pop(builder);
                ir_builder_emit_unary(builder, IR_PRINT, (ir_value_t){0}, value);
                break;
            }

            case OP_RETURN: {
                ir_value_t value;
                if (builder->stack_count > 0) {
                    value = ir_builder_pop(builder);
                } else {
                    value = ir_value_constant(NIL_VAL);
                }
                ir_builder_emit_unary(builder, IR_RETURN, (ir_value_t){0}, value);
                break;
            }

            case OP_JUMP: {
                uint16_t offset = (chunk->code[ip] << 8) | chunk->code[ip + 1];
                ip += 2;
                int target_offset = ip + offset;
                int target_label = offset_to_label[target_offset];
                ir_value_t target = ir_value_label(target_label);
                ir_builder_emit_unary(builder, IR_JUMP, (ir_value_t){0}, target);
                // After a jump, if there's more code, start a new block
                if (ip < chunk->count && offset_to_label[ip] == -1) {
                    ir_block_t* new_block = ir_function_new_block(ir_func);
                    builder->current_block = new_block;
                }
                break;
            }

            case OP_JUMP_IF_FALSE: {
                uint16_t offset = (chunk->code[ip] << 8) | chunk->code[ip + 1];
                ip += 2;
                ir_value_t condition = ir_builder_pop(builder);
                int target_offset = ip + offset;
                int target_label = offset_to_label[target_offset];
                ir_value_t target = ir_value_label(target_label);
                ir_builder_emit_binary(builder, IR_BRANCH, (ir_value_t){0}, condition, target);
                // After a conditional branch, continue in same block (fall-through)
                break;
            }

            case OP_LOOP: {
                uint16_t offset = (chunk->code[ip] << 8) | chunk->code[ip + 1];
                ip += 2;
                int target_offset = ip - offset;
                int target_label = offset_to_label[target_offset];
                ir_value_t target = ir_value_label(target_label);
                ir_builder_emit_unary(builder, IR_JUMP, (ir_value_t){0}, target);
                // After a loop jump, if there's more code, start a new block
                if (ip < chunk->count && offset_to_label[ip] == -1) {
                    ir_block_t* new_block = ir_function_new_block(ir_func);
                    builder->current_block = new_block;
                }
                break;
            }

            case OP_CLOSURE: {
                uint8_t constant_idx = chunk->code[ip++];
                value_t constant = chunk->constants.values[constant_idx];
                obj_function_t* func_obj = NULL;
                if (IS_OBJ(constant) && AS_OBJ(constant)->type == OBJ_FUNCTION) {
                    func_obj = AS_FUNCTION(constant);
                }

                if (func_obj && func_obj->upvalue_count == 0) {
                    ir_function_t* target = ir_builder_lookup_function(builder, func_obj);
                    if (target) {
                        ir_builder_push(builder, ir_value_function(target));
                    } else {
                        int reg = ir_function_alloc_register(ir_func);
                        ir_builder_push(builder, ir_value_register(reg));
                    }
                } else {
                    int reg = ir_function_alloc_register(ir_func);
                    ir_builder_push(builder, ir_value_register(reg));
                }

                if (func_obj) {
                    for (int i = 0; i < func_obj->upvalue_count; i++) {
                        ip += 2;
                    }
                }
                break;
            }

            case OP_CALL: {
                uint8_t arg_count = chunk->code[ip++];
                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);
                ir_value_t func = ir_builder_peek(builder, arg_count);

                // Create call instruction
                ir_instruction_t* call = ir_builder_emit(builder, IR_CALL);
                call->dest = dest;
                call->operand1 = func; // Function to call

                if (func.type == IR_VAL_FUNCTION && func.as.func) {
                    call->call_target = func.as.func->name;
                    call->call_function = func.as.func;
                }

                // Capture arguments from stack
                if (arg_count > 0) {
                    call->call_args = (ir_value_t*)l_mem_alloc(sizeof(ir_value_t) * arg_count);
                    call->call_arg_count = arg_count;
                    // Arguments are on stack in order (first arg is deepest)
                    for (int i = 0; i < arg_count; i++) {
                        call->call_args[i] = ir_builder_peek(builder, arg_count - 1 - i);
                    }
                }

                // Pop function and arguments
                for (int i = 0; i <= arg_count; i++) {
                    ir_builder_pop(builder);
                }
                ir_builder_push(builder, dest);
                break;
            }

            case OP_ARRAY_EMPTY: {
                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);
                ir_builder_emit(builder, IR_NEW_ARRAY)->dest = dest;
                ir_builder_push(builder, dest);
                break;
            }

            case OP_CLASS: {
                uint8_t constant_idx = chunk->code[ip++];
                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);
                ir_value_t name = ir_value_constant(chunk->constants.values[constant_idx]);
                ir_builder_emit_unary(builder, IR_NEW_CLASS, dest, name);
                ir_builder_push(builder, dest);
                break;
            }

            // For now, skip unsupported opcodes with a runtime call
            default: {
                int reg = ir_function_alloc_register(ir_func);
                ir_value_t dest = ir_value_register(reg);
                ir_value_t opcode = ir_value_constant(NUMBER_VAL(instruction));
                ir_builder_emit_unary(builder, IR_RUNTIME_CALL, dest, opcode);
                break;
            }
        }
    }

    // Clean up
    l_mem_free(offset_to_label, sizeof(int) * chunk->count);

}

ir_function_t* ir_builder_build_function(ir_builder_t* builder, obj_function_t* function) {
    ir_function_t* ir_func = ir_builder_create_function(function);
    ir_builder_build_function_body(builder, function, ir_func);
    return ir_func;
}

static ir_function_t* ir_builder_build_function_recursive(ir_builder_t* builder, obj_function_t* function) {
    ir_function_t* existing = ir_builder_lookup_function(builder, function);
    if (existing) {
        if (existing->block_count == 0) {
            ir_builder_build_function_body(builder, function, existing);
        }
        return existing;
    }

    ir_function_t* ir_func = ir_builder_create_function(function);
    ir_module_add_function(builder->module, ir_func);
    ir_builder_record_function(builder, function, ir_func);

    value_array_t* constants = &function->chunk.constants;
    for (size_t i = 0; i < constants->count; i++) {
        value_t constant = constants->values[i];
        if (!IS_OBJ(constant)) {
            continue;
        }

        obj_function_t* nested = NULL;
        if (AS_OBJ(constant)->type == OBJ_FUNCTION) {
            nested = AS_FUNCTION(constant);
        } else if (AS_OBJ(constant)->type == OBJ_CLOSURE) {
            nested = ((obj_closure_t*)AS_OBJ(constant))->function;
        }

        if (nested) {
            ir_builder_build_function_recursive(builder, nested);
        }
    }

    ir_builder_build_function_body(builder, function, ir_func);
    return ir_func;
}

ir_module_t* ir_builder_build_module(obj_closure_t* entry_closure) {
    ir_builder_t* builder = ir_builder_new();
    ir_module_t* module = ir_module_new();
    builder->module = module;

    ir_builder_build_function_recursive(builder, entry_closure->function);

    ir_builder_free(builder);
    return module;
}
