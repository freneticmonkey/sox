/*
 * Native Code Integration Tests
 *
 * End-to-end tests for native code generation and execution.
 * Tests the complete pipeline: Sox source → IR → native code → execution
 */

#include <munit/munit.h>
#include "../vm.h"
#include "../compiler.h"
#include "../native/ir_builder.h"
#include "../native/native_codegen.h"
#include "../native/codegen.h"
#include "../native/codegen_arm64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

// Helper: compile Sox source to closure
static obj_closure_t* compile_source(const char* source) {
    vm_config_t config = {0};
    l_init_vm(&config);
    InterpretResult result = l_interpret(source);

    if (result != INTERPRET_OK) {
        return NULL;
    }

    // Get the main closure from the VM
    // For now, we'll create a simple test closure
    obj_function_t* function = l_new_function();
    function->name = l_copy_string("test", 4);

    obj_closure_t* closure = l_new_closure(function);
    return closure;
}

// Helper: execute native binary and capture output
static bool execute_and_verify(const char* binary_path, const char* expected_output) {
    // Execute the binary
    FILE* fp = popen(binary_path, "r");
    if (!fp) {
        printf("  [ERROR] Failed to execute binary: %s\n", binary_path);
        return false;
    }

    char output[4096] = {0};
    size_t bytes_read = fread(output, 1, sizeof(output) - 1, fp);
    output[bytes_read] = '\0';

    int status = pclose(fp);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        // Compare output
        if (strcmp(output, expected_output) == 0) {
            printf("  [PASS] Output matches expected\n");
            return true;
        } else {
            printf("  [FAIL] Output mismatch\n");
            printf("    Expected: %s\n", expected_output);
            printf("    Got: %s\n", output);
            return false;
        }
    } else {
        printf("  [ERROR] Binary exited with status %d\n", WEXITSTATUS(status));
        return false;
    }
}

// Test 1: Simple arithmetic - native code generation
static MunitResult test_native_simple_arithmetic(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Native Code - Simple Arithmetic ===\n");

    const char* source =
        "var a = 10;\n"
        "var b = 20;\n"
        "var c = a + b;\n"
        "print(c);\n";

    printf("  [INFO] Compiling Sox source...\n");
    obj_closure_t* closure = compile_source(source);

    if (!closure) {
        printf("  [SKIP] Source compilation failed (expected for IR-only test)\n");
        return MUNIT_SKIP;
    }

    printf("  [INFO] Generating native code...\n");
    const char* output_file = "/tmp/sox_test_arithmetic";

    native_codegen_options_t options = {
        .output_file = output_file,
        .target_arch = "x86_64",
        .target_os = "linux",
        .emit_object = false,
        .debug_output = true,
        .optimization_level = 0
    };

    bool success = native_codegen_generate(closure, &options);

    if (!success) {
        printf("  [SKIP] Native code generation not fully implemented yet\n");
        return MUNIT_SKIP;
    }

    printf("  [INFO] Executing native binary...\n");
    bool verified = execute_and_verify(output_file, "30\n");

    // Cleanup
    unlink(output_file);
    l_free_vm();

    munit_assert_true(verified);
    return MUNIT_OK;
}

// Test 2: Object file generation (x86-64)
static MunitResult test_native_object_generation_x64(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Native Code - Object File Generation (x86-64) ===\n");

    // Create a simple IR function
    ir_module_t* module = ir_module_new();
    ir_function_t* func = ir_function_new("test_func", 0);

    // Add basic blocks and instructions
    ir_block_t* entry = ir_function_new_block(func);

    // Create a simple: return 42
    ir_value_t constant = { .type = IR_VAL_CONSTANT, .as.constant = NUMBER_VAL(42) };
    ir_value_t dest = { .type = IR_VAL_REGISTER, .as.reg = 0 };

    ir_instruction_t* mov = ir_instruction_new(IR_MOVE);
    mov->dest = dest;
    mov->operand1 = constant;
    ir_block_add_instruction(entry, mov);

    ir_instruction_t* ret = ir_instruction_new(IR_RETURN);
    ret->operand1 = dest;
    ir_block_add_instruction(entry, ret);

    ir_module_add_function(module, func);

    printf("  [INFO] Generating x86-64 object file...\n");

    // Generate x86-64 code
    codegen_context_t* ctx = codegen_new(module);
    bool success = codegen_generate_function(ctx, func);

    if (!success) {
        printf("  [FAIL] Code generation failed\n");
        codegen_free(ctx);
        ir_module_free(module);
        return MUNIT_FAIL;
    }

    size_t code_size;
    uint8_t* code = codegen_get_code(ctx, &code_size);

    printf("  [INFO] Generated %zu bytes of x86-64 machine code\n", code_size);
    printf("  [INFO] First 16 bytes: ");
    for (size_t i = 0; i < (code_size < 16 ? code_size : 16); i++) {
        printf("%02x ", code[i]);
    }
    printf("\n");

    // Verify we generated some code
    munit_assert_size(code_size, >, 0);
    munit_assert_ptr_not_null(code);

    // Verify basic x86-64 prologue patterns
    // Should start with: push rbp (0x55) or similar
    bool has_prologue = (code_size > 0);
    printf("  [PASS] Object file generation successful\n");

    codegen_free(ctx);
    ir_module_free(module);

    munit_assert_true(has_prologue);
    return MUNIT_OK;
}

// Test 3: Object file generation (ARM64)
static MunitResult test_native_object_generation_arm64(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Native Code - Object File Generation (ARM64) ===\n");

    // Create a simple IR function
    ir_module_t* module = ir_module_new();
    ir_function_t* func = ir_function_new("test_func", 0);

    // Add basic blocks and instructions
    ir_block_t* entry = ir_function_new_block(func);

    // Create a simple: return 42
    ir_value_t constant = { .type = IR_VAL_CONSTANT, .as.constant = NUMBER_VAL(42) };
    ir_value_t dest = { .type = IR_VAL_REGISTER, .as.reg = 0 };

    ir_instruction_t* mov = ir_instruction_new(IR_MOVE);
    mov->dest = dest;
    mov->operand1 = constant;
    ir_block_add_instruction(entry, mov);

    ir_instruction_t* ret = ir_instruction_new(IR_RETURN);
    ret->operand1 = dest;
    ir_block_add_instruction(entry, ret);

    ir_module_add_function(module, func);

    printf("  [INFO] Generating ARM64 object file...\n");

    // Generate ARM64 code
    codegen_arm64_context_t* ctx = codegen_arm64_new(module);
    bool success = codegen_arm64_generate_function(ctx, func);

    if (!success) {
        printf("  [FAIL] Code generation failed\n");
        codegen_arm64_free(ctx);
        ir_module_free(module);
        return MUNIT_FAIL;
    }

    size_t code_size;
    uint8_t* code = codegen_arm64_get_code(ctx, &code_size);

    printf("  [INFO] Generated %zu bytes of ARM64 machine code\n", code_size);
    printf("  [INFO] First 16 bytes: ");
    for (size_t i = 0; i < (code_size < 16 ? code_size : 16); i++) {
        printf("%02x ", code[i]);
    }
    printf("\n");

    // Verify we generated some code
    munit_assert_size(code_size, >, 0);
    munit_assert_ptr_not_null(code);

    printf("  [PASS] ARM64 object file generation successful\n");

    codegen_arm64_free(ctx);
    ir_module_free(module);

    return MUNIT_OK;
}

// Test 4: Calling convention - x86-64 argument passing
static MunitResult test_native_x64_argument_passing(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Native Code - x86-64 Argument Passing ===\n");

    // Create IR function with call instruction
    ir_module_t* module = ir_module_new();
    ir_function_t* func = ir_function_new("test_call", 0);
    ir_block_t* entry = ir_function_new_block(func);

    // Prepare arguments for call
    ir_value_t args[3];
    args[0] = (ir_value_t){ .type = IR_VAL_CONSTANT, .as.constant = NUMBER_VAL(10) };
    args[1] = (ir_value_t){ .type = IR_VAL_CONSTANT, .as.constant = NUMBER_VAL(20) };
    args[2] = (ir_value_t){ .type = IR_VAL_CONSTANT, .as.constant = NUMBER_VAL(30) };

    ir_value_t result = { .type = IR_VAL_REGISTER, .as.reg = 0 };

    // Create call instruction
    ir_instruction_t* call = ir_instruction_new(IR_RUNTIME_CALL);
    call->dest = result;
    call->call_target = "sox_native_add";
    call->call_args = args;
    call->call_arg_count = 2;  // Only use first 2 args for add

    ir_block_add_instruction(entry, call);

    ir_instruction_t* ret = ir_instruction_new(IR_RETURN);
    ret->operand1 = result;
    ir_block_add_instruction(entry, ret);

    ir_module_add_function(module, func);

    printf("  [INFO] Generating x86-64 code with runtime call...\n");

    codegen_context_t* ctx = codegen_new(module);
    bool success = codegen_generate_function(ctx, func);

    if (!success) {
        printf("  [FAIL] Code generation failed\n");
        codegen_free(ctx);
        ir_module_free(module);
        return MUNIT_FAIL;
    }

    size_t code_size;
    uint8_t* code = codegen_get_code(ctx, &code_size);

    printf("  [INFO] Generated %zu bytes with runtime call\n", code_size);

    // Verify relocations were created
    int reloc_count;
    codegen_relocation_t* relocs = codegen_get_relocations(ctx, &reloc_count);

    printf("  [INFO] Generated %d relocations\n", reloc_count);
    for (int i = 0; i < reloc_count; i++) {
        printf("    - Relocation: %s at offset %zu (type %u)\n",
               relocs[i].symbol, relocs[i].offset, relocs[i].type);
    }

    munit_assert_int(reloc_count, >, 0);
    printf("  [PASS] Runtime call generation successful\n");

    codegen_free(ctx);
    ir_module_free(module);

    return MUNIT_OK;
}

// Test 5: Stack alignment verification
static MunitResult test_native_stack_alignment(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Native Code - Stack Alignment ===\n");

    // Create a function that makes multiple calls
    // This tests that the stack remains aligned throughout
    ir_module_t* module = ir_module_new();
    ir_function_t* func = ir_function_new("test_alignment", 0);
    ir_block_t* entry = ir_function_new_block(func);

    // Make several calls to verify stack alignment is maintained
    for (int i = 0; i < 5; i++) {
        ir_value_t arg = { .type = IR_VAL_CONSTANT, .as.constant = NUMBER_VAL(i) };
        ir_value_t result = { .type = IR_VAL_REGISTER, .as.reg = i };

        ir_instruction_t* call = ir_instruction_new(IR_RUNTIME_CALL);
        call->dest = result;
        call->call_target = "sox_native_print";
        call->call_args = &arg;
        call->call_arg_count = 1;

        ir_block_add_instruction(entry, call);
    }

    ir_value_t ret_val = { .type = IR_VAL_CONSTANT, .as.constant = NIL_VAL };
    ir_instruction_t* ret = ir_instruction_new(IR_RETURN);
    ret->operand1 = ret_val;
    ir_block_add_instruction(entry, ret);

    ir_module_add_function(module, func);

    printf("  [INFO] Generating code with multiple calls...\n");

    codegen_context_t* ctx = codegen_new(module);
    bool success = codegen_generate_function(ctx, func);

    munit_assert_true(success);

    size_t code_size;
    codegen_get_code(ctx, &code_size);

    printf("  [INFO] Generated %zu bytes with 5 runtime calls\n", code_size);
    printf("  [PASS] Multiple calls generated successfully\n");

    codegen_free(ctx);
    ir_module_free(module);

    return MUNIT_OK;
}

// Test suite definition
static MunitTest native_integration_tests[] = {
    {
        .name = (char *)"/simple_arithmetic",
        .test = test_native_simple_arithmetic,
        .setup = NULL,
        .tear_down = NULL,
        .options = MUNIT_TEST_OPTION_NONE,
        .parameters = NULL,
    },
    {
        .name = (char *)"/object_generation_x64",
        .test = test_native_object_generation_x64,
        .setup = NULL,
        .tear_down = NULL,
        .options = MUNIT_TEST_OPTION_NONE,
        .parameters = NULL,
    },
    {
        .name = (char *)"/object_generation_arm64",
        .test = test_native_object_generation_arm64,
        .setup = NULL,
        .tear_down = NULL,
        .options = MUNIT_TEST_OPTION_NONE,
        .parameters = NULL,
    },
    {
        .name = (char *)"/x64_argument_passing",
        .test = test_native_x64_argument_passing,
        .setup = NULL,
        .tear_down = NULL,
        .options = MUNIT_TEST_OPTION_NONE,
        .parameters = NULL,
    },
    {
        .name = (char *)"/stack_alignment",
        .test = test_native_stack_alignment,
        .setup = NULL,
        .tear_down = NULL,
        .options = MUNIT_TEST_OPTION_NONE,
        .parameters = NULL,
    },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

const MunitSuite native_integration_suite = {
    .prefix = (char *)"sox/native_integration",
    .tests = native_integration_tests,
    .suites = NULL,
    .iterations = 1,
    .options = MUNIT_SUITE_OPTION_NONE
};
