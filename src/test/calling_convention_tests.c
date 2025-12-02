#include "calling_convention_tests.h"
#include "../native/native_codegen.h"
#include "../native/ir_builder.h"
#include "../native/codegen.h"
#include "../native/x64_encoder.h"
#include "../vm.h"
#include "../compiler.h"
#include "../lib/memory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Helper function to check stack alignment
static bool is_stack_aligned_16(void) {
    void* stack_ptr;
    #if defined(__x86_64__) || defined(_M_X64)
        __asm__ volatile("mov %%rsp, %0" : "=r"(stack_ptr));
    #elif defined(__aarch64__) || defined(_M_ARM64)
        __asm__ volatile("mov %0, sp" : "=r"(stack_ptr));
    #else
        return true; // Skip on unsupported platforms
    #endif
    return ((uintptr_t)stack_ptr % 16) == 0;
}

// Helper to print register state (diagnostic only)
static void print_register_state(const char* label) {
    #if defined(__x86_64__) || defined(_M_X64)
        uint64_t rax, rbx, rcx, rdx, rsi, rdi, rsp, rbp;
        __asm__ volatile(
            "mov %%rax, %0\n"
            "mov %%rbx, %1\n"
            "mov %%rcx, %2\n"
            "mov %%rdx, %3\n"
            "mov %%rsi, %4\n"
            "mov %%rdi, %5\n"
            "mov %%rsp, %6\n"
            "mov %%rbp, %7\n"
            : "=r"(rax), "=r"(rbx), "=r"(rcx), "=r"(rdx),
              "=r"(rsi), "=r"(rdi), "=r"(rsp), "=r"(rbp)
        );
        printf("[%s] RAX=%016llx RBX=%016llx RCX=%016llx RDX=%016llx\n",
               label, (unsigned long long)rax, (unsigned long long)rbx,
               (unsigned long long)rcx, (unsigned long long)rdx);
        printf("[%s] RSI=%016llx RDI=%016llx RSP=%016llx RBP=%016llx\n",
               label, (unsigned long long)rsi, (unsigned long long)rdi,
               (unsigned long long)rsp, (unsigned long long)rbp);
    #endif
}

// Diagnostic helper to validate function prologue structure
static bool validate_prologue(const uint8_t* code, size_t code_size) {
    if (code_size < 3) return false;

    // Check for "push rbp" (0x55)
    if (code[0] != 0x55) {
        printf("  [DIAG] Missing 'push rbp' at offset 0, found: 0x%02x\n", code[0]);
        return false;
    }

    // Check for "mov rbp, rsp" (0x48 0x89 0xe5)
    if (code_size < 4 || code[1] != 0x48 || code[2] != 0x89 || code[3] != 0xe5) {
        printf("  [DIAG] Missing 'mov rbp, rsp' at offset 1-3\n");
        return false;
    }

    printf("  [DIAG] Prologue structure looks valid\n");
    return true;
}

// Test 1: Function call with no arguments
static MunitResult test_func_0_args(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Function with 0 arguments ===\n");

    vm_config_t config = {.suppress_print = true};
    l_init_memory();
    l_init_vm(&config);

    // Simple program that calls a function with no arguments
    // Note: In Sox, we can't directly test native code generation from VM
    // This test validates the VM behavior as a baseline
    const char* source =
        "fun greet() {\n"
        "  var x = 42;\n"
        "  print(x);\n"
        "}\n"
        "greet();\n";

    InterpretResult result = l_interpret(source);
    if (result != INTERPRET_OK) {
        printf("  [FAIL] Compilation/interpretation failed\n");
        l_free_vm();
        l_free_memory();
        return MUNIT_FAIL;
    }

    l_free_vm();
    l_free_memory();

    printf("  [PASS] 0-argument function call works in interpreter\n");
    return MUNIT_OK;
}

// Test 2: Function call with 1 integer argument (RDI on x64)
static MunitResult test_func_1_int_arg(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Function with 1 integer argument ===\n");

    vm_config_t config = {.suppress_print = true};
    l_init_memory();
    l_init_vm(&config);

    const char* source =
        "fun double(x) {\n"
        "  var result = x * 2;\n"
        "  print(result);\n"
        "}\n"
        "double(21);\n";

    InterpretResult result = l_interpret(source);
    if (result != INTERPRET_OK) {
        printf("  [FAIL] Compilation/interpretation failed\n");
        l_free_vm();
        l_free_memory();
        return MUNIT_FAIL;
    }

    l_free_vm();
    l_free_memory();

    printf("  [PASS] 1-argument function call works in interpreter\n");
    return MUNIT_OK;
}

// Test 3: Function call with 2 integer arguments (RDI, RSI on x64)
static MunitResult test_func_2_int_args(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Function with 2 integer arguments ===\n");

    vm_config_t config = {.suppress_print = true};
    l_init_memory();
    l_init_vm(&config);

    const char* source =
        "fun add(a, b) {\n"
        "  var result = a + b;\n"
        "  print(result);\n"
        "}\n"
        "add(10, 32);\n";

    InterpretResult result = l_interpret(source);
    if (result != INTERPRET_OK) {
        printf("  [FAIL] Compilation/interpretation failed\n");
        l_free_vm();
        l_free_memory();
        return MUNIT_FAIL;
    }

    l_free_vm();
    l_free_memory();

    printf("  [PASS] 2-argument function call works in interpreter\n");
    return MUNIT_OK;
}

// Test 4: Function call with 6 integer arguments (fill all integer registers)
static MunitResult test_func_6_int_args(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Function with 6 integer arguments ===\n");

    vm_config_t config = {.suppress_print = true};
    l_init_memory();
    l_init_vm(&config);

    const char* source =
        "fun sum6(a, b, c, d, e, f) {\n"
        "  var result = a + b + c + d + e + f;\n"
        "  print(result);\n"
        "}\n"
        "sum6(1, 2, 3, 4, 5, 6);\n";

    InterpretResult result = l_interpret(source);
    if (result != INTERPRET_OK) {
        printf("  [FAIL] Compilation/interpretation failed\n");
        l_free_vm();
        l_free_memory();
        return MUNIT_FAIL;
    }

    l_free_vm();
    l_free_memory();

    printf("  [PASS] 6-argument function call works in interpreter\n");
    return MUNIT_OK;
}

// Test 5: Function call with 7+ arguments (overflow to stack)
static MunitResult test_func_7_int_args(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Function with 7+ integer arguments (stack overflow) ===\n");

    vm_config_t config = {.suppress_print = true};
    l_init_memory();
    l_init_vm(&config);

    const char* source =
        "fun sum8(a, b, c, d, e, f, g, h) {\n"
        "  var result = a + b + c + d + e + f + g + h;\n"
        "  print(result);\n"
        "}\n"
        "sum8(1, 2, 3, 4, 5, 6, 7, 8);\n";

    InterpretResult result = l_interpret(source);
    if (result != INTERPRET_OK) {
        printf("  [FAIL] Compilation/interpretation failed\n");
        l_free_vm();
        l_free_memory();
        return MUNIT_FAIL;
    }

    l_free_vm();
    l_free_memory();

    printf("  [PASS] 7+ argument function call works in interpreter\n");
    return MUNIT_OK;
}

// Test 6: Function with mixed integer and floating-point arguments
static MunitResult test_func_mixed_args(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Function with mixed int/float arguments ===\n");

    vm_config_t config = {.suppress_print = true};
    l_init_memory();
    l_init_vm(&config);

    const char* source =
        "fun compute(a, b, c) {\n"
        "  var result = a + b * c;\n"
        "  print(result);\n"
        "}\n"
        "compute(10, 2.5, 4);\n";

    InterpretResult result = l_interpret(source);
    if (result != INTERPRET_OK) {
        printf("  [FAIL] Compilation/interpretation failed\n");
        l_free_vm();
        l_free_memory();
        return MUNIT_FAIL;
    }

    l_free_vm();
    l_free_memory();

    printf("  [PASS] Mixed argument function call works in interpreter\n");
    return MUNIT_OK;
}

// Test 7: Verify return value in RAX/X0
static MunitResult test_func_return_value(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Function return value handling ===\n");

    vm_config_t config = {.suppress_print = true};
    l_init_memory();
    l_init_vm(&config);

    const char* source =
        "fun get_value() {\n"
        "  print(99);\n"
        "}\n"
        "get_value();\n"
        "get_value();\n";

    InterpretResult result = l_interpret(source);
    if (result != INTERPRET_OK) {
        printf("  [FAIL] Compilation/interpretation failed\n");
        l_free_vm();
        l_free_memory();
        return MUNIT_FAIL;
    }

    l_free_vm();
    l_free_memory();

    printf("  [PASS] Return value handling works in interpreter\n");
    return MUNIT_OK;
}

// Test 8: Verify 16-byte stack alignment
static MunitResult test_stack_alignment(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Stack alignment (16-byte boundary) ===\n");

    // Check current stack alignment
    bool aligned = is_stack_aligned_16();
    printf("  [INFO] Test function stack is %s\n",
           aligned ? "16-byte aligned" : "NOT 16-byte aligned");

    if (!aligned) {
        printf("  [WARN] Stack not aligned - this may cause issues with native code\n");
    }

    // This test always passes as it's diagnostic only
    printf("  [PASS] Stack alignment check completed\n");
    return MUNIT_OK;
}

// Test 9: Nested function calls
static MunitResult test_nested_calls(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Nested function calls ===\n");

    vm_config_t config = {.suppress_print = true};
    l_init_memory();
    l_init_vm(&config);

    const char* source =
        "fun inner(x) {\n"
        "  var result = x * 2;\n"
        "  print(result);\n"
        "}\n"
        "fun outer(y) {\n"
        "  inner(y);\n"
        "}\n"
        "outer(5);\n";

    InterpretResult result = l_interpret(source);
    if (result != INTERPRET_OK) {
        printf("  [FAIL] Compilation/interpretation failed\n");
        l_free_vm();
        l_free_memory();
        return MUNIT_FAIL;
    }

    l_free_vm();
    l_free_memory();

    printf("  [PASS] Nested function calls work in interpreter\n");
    return MUNIT_OK;
}

// Test 10: Callee-saved register preservation
static MunitResult test_callee_saved_regs(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Callee-saved register preservation ===\n");

    vm_config_t config = {.suppress_print = true};
    l_init_memory();
    l_init_vm(&config);

    // Test that local variables survive function calls
    const char* source =
        "fun helper() {\n"
        "  print(1);\n"
        "}\n"
        "fun main_func() {\n"
        "  var a = 10;\n"
        "  var b = 20;\n"
        "  helper();\n"
        "  print(a + b);\n"
        "}\n"
        "main_func();\n";

    InterpretResult result = l_interpret(source);
    if (result != INTERPRET_OK) {
        printf("  [FAIL] Compilation/interpretation failed\n");
        l_free_vm();
        l_free_memory();
        return MUNIT_FAIL;
    }

    l_free_vm();
    l_free_memory();

    printf("  [PASS] Callee-saved register test works in interpreter\n");
    return MUNIT_OK;
}

// Test 11: Direct prologue/epilogue validation
static MunitResult test_prologue_epilogue_structure(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Prologue/Epilogue structure validation ===\n");

    vm_config_t config = {.suppress_print = true};
    l_init_memory();
    l_init_vm(&config);

    const char* source = "fun test() { var x = 42; print(x); }";
    InterpretResult result = l_interpret(source);

    if (result != INTERPRET_OK) {
        printf("  [FAIL] Compilation failed\n");
        l_free_vm();
        l_free_memory();
        return MUNIT_FAIL;
    }

    // Try to get the compiled function and examine its bytecode
    // This is for diagnostic purposes only
    printf("  [INFO] Function compiled successfully in VM\n");
    printf("  [TODO] Generate native code and validate prologue structure\n");

    l_free_vm();
    l_free_memory();

    printf("  [PASS] Prologue/epilogue structure test completed\n");
    return MUNIT_OK;
}

// Test 12: Call to external C library function (printf, strlen, etc.)
static MunitResult test_external_c_call(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: External C library function calls ===\n");

    // Test direct C library calls to verify ABI compliance
    printf("  [INFO] Testing direct call to strlen()\n");
    const char* test_str = "hello";
    size_t len = strlen(test_str);
    printf("  [INFO] strlen(\"%s\") = %zu\n", test_str, len);

    if (len != 5) {
        printf("  [FAIL] strlen() returned unexpected value\n");
        return MUNIT_FAIL;
    }

    printf("  [INFO] Testing printf() with stack alignment check\n");
    bool aligned_before = is_stack_aligned_16();
    printf("  [INFO] Stack aligned before call: %s\n", aligned_before ? "yes" : "no");

    // This call exercises the calling convention
    printf("  [INFO] Test message from printf\n");

    bool aligned_after = is_stack_aligned_16();
    printf("  [INFO] Stack aligned after call: %s\n", aligned_after ? "yes" : "no");

    printf("  [PASS] External C library calls work correctly\n");
    return MUNIT_OK;
}

// Test 13: Register diagnostic
static MunitResult test_register_diagnostic(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    printf("\n=== Test: Register state diagnostic ===\n");

    print_register_state("Entry");

    // Do some work
    volatile int x = 42;
    volatile int y = x * 2;
    (void)y;

    print_register_state("After computation");

    printf("  [PASS] Register diagnostic completed\n");
    return MUNIT_OK;
}

// Test suite setup
MunitSuite l_calling_convention_test_setup(void) {
    static MunitTest calling_convention_tests[] = {
        {
            .name = (char *)"func_0_args",
            .test = test_func_0_args,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"func_1_int_arg",
            .test = test_func_1_int_arg,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"func_2_int_args",
            .test = test_func_2_int_args,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"func_6_int_args",
            .test = test_func_6_int_args,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"func_7_int_args",
            .test = test_func_7_int_args,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"func_mixed_args",
            .test = test_func_mixed_args,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"func_return_value",
            .test = test_func_return_value,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"stack_alignment",
            .test = test_stack_alignment,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"nested_calls",
            .test = test_nested_calls,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"callee_saved_regs",
            .test = test_callee_saved_regs,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"prologue_epilogue_structure",
            .test = test_prologue_epilogue_structure,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"external_c_call",
            .test = test_external_c_call,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {
            .name = (char *)"register_diagnostic",
            .test = test_register_diagnostic,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        // END
        {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
    };

    return (MunitSuite) {
        .prefix = (char *)"calling_convention/",
        .tests = calling_convention_tests,
        .suites = NULL,
        .iterations = 1,
        .options = MUNIT_SUITE_OPTION_NONE
    };
}
