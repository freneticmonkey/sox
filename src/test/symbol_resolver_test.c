/*
 * Unit tests for symbol_resolver.c
 *
 * Tests the symbol resolution engine including:
 * - Hash table implementation
 * - Phase 1: Collect defined symbols
 * - Phase 2: Resolve undefined symbols
 * - Runtime library symbol detection
 * - Error handling
 */

#include "../native/symbol_resolver.h"
#include "../native/linker_core.h"
#include "../../ext/munit/munit.h"
#include <string.h>
#include <stdio.h>

/* Test: Create and destroy symbol resolver */
static MunitResult test_symbol_resolver_lifecycle(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    symbol_resolver_t* resolver = symbol_resolver_new();
    munit_assert_not_null(resolver);

    symbol_resolver_free(resolver);

    return MUNIT_OK;
}

/* Test: Hash function consistency */
static MunitResult test_symbol_hash_consistency(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    const char* name = "test_symbol";
    uint32_t hash1 = symbol_hash(name);
    uint32_t hash2 = symbol_hash(name);

    munit_assert_uint32(hash1, ==, hash2);
    munit_assert_uint32(hash1, !=, 0);

    /* Different strings should (usually) have different hashes */
    uint32_t hash3 = symbol_hash("different_symbol");
    munit_assert_uint32(hash1, !=, hash3);

    return MUNIT_OK;
}

/* Test: Runtime symbol detection */
static MunitResult test_is_runtime_symbol(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Known runtime symbols */
    munit_assert_true(is_runtime_symbol("sox_runtime_add"));
    munit_assert_true(is_runtime_symbol("sox_runtime_print"));
    munit_assert_true(is_runtime_symbol("sox_runtime_alloc"));

    /* Not runtime symbols */
    munit_assert_false(is_runtime_symbol("main"));
    munit_assert_false(is_runtime_symbol("my_function"));
    munit_assert_false(is_runtime_symbol("sox_not_runtime"));
    munit_assert_false(is_runtime_symbol(NULL));

    return MUNIT_OK;
}

/* Test: Single object with all defined symbols */
static MunitResult test_single_object_all_defined(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create object with defined symbols */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(obj);

    /* Add symbols */
    linker_symbol_t* sym1 = linker_object_add_symbol(obj);
    sym1->name = strdup("main");
    sym1->type = SYMBOL_TYPE_FUNC;
    sym1->binding = SYMBOL_BINDING_GLOBAL;
    sym1->is_defined = true;
    sym1->value = 0x1000;

    linker_symbol_t* sym2 = linker_object_add_symbol(obj);
    sym2->name = strdup("helper");
    sym2->type = SYMBOL_TYPE_FUNC;
    sym2->binding = SYMBOL_BINDING_GLOBAL;
    sym2->is_defined = true;
    sym2->value = 0x2000;

    /* Create resolver and add object */
    symbol_resolver_t* resolver = symbol_resolver_new();
    munit_assert_not_null(resolver);

    symbol_resolver_add_object(resolver, obj, 0);

    /* Resolve symbols */
    bool result = symbol_resolver_resolve(resolver);
    munit_assert_true(result);

    /* Verify symbols can be looked up */
    linker_symbol_t* found1 = symbol_resolver_lookup(resolver, "main");
    munit_assert_not_null(found1);
    munit_assert_string_equal(found1->name, "main");
    munit_assert_uint64(found1->value, ==, 0x1000);

    linker_symbol_t* found2 = symbol_resolver_lookup(resolver, "helper");
    munit_assert_not_null(found2);
    munit_assert_string_equal(found2->name, "helper");
    munit_assert_uint64(found2->value, ==, 0x2000);

    /* Check errors */
    int error_count = 0;
    symbol_resolver_get_errors(resolver, &error_count);
    munit_assert_int(error_count, ==, 0);

    symbol_resolver_free(resolver);
    linker_object_free(obj);

    return MUNIT_OK;
}

/* Test: Two objects with complementary symbols */
static MunitResult test_two_objects_complementary(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create first object */
    linker_object_t* obj1 = linker_object_new("main.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(obj1);

    /* main() defined, helper() undefined */
    linker_symbol_t* sym1 = linker_object_add_symbol(obj1);
    sym1->name = strdup("main");
    sym1->type = SYMBOL_TYPE_FUNC;
    sym1->binding = SYMBOL_BINDING_GLOBAL;
    sym1->is_defined = true;
    sym1->value = 0x1000;

    linker_symbol_t* sym2 = linker_object_add_symbol(obj1);
    sym2->name = strdup("helper");
    sym2->type = SYMBOL_TYPE_FUNC;
    sym2->binding = SYMBOL_BINDING_GLOBAL;
    sym2->is_defined = false;
    sym2->value = 0;

    /* Create second object */
    linker_object_t* obj2 = linker_object_new("helper.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(obj2);

    /* helper() defined */
    linker_symbol_t* sym3 = linker_object_add_symbol(obj2);
    sym3->name = strdup("helper");
    sym3->type = SYMBOL_TYPE_FUNC;
    sym3->binding = SYMBOL_BINDING_GLOBAL;
    sym3->is_defined = true;
    sym3->value = 0x2000;

    /* Create resolver and add objects */
    symbol_resolver_t* resolver = symbol_resolver_new();
    munit_assert_not_null(resolver);

    symbol_resolver_add_object(resolver, obj1, 0);
    symbol_resolver_add_object(resolver, obj2, 1);

    /* Resolve symbols */
    bool result = symbol_resolver_resolve(resolver);
    munit_assert_true(result);

    /* Verify undefined symbol was resolved */
    munit_assert_int(sym2->defining_object, ==, 1);  /* Points to obj2 */

    /* Check errors */
    int error_count = 0;
    symbol_resolver_get_errors(resolver, &error_count);
    munit_assert_int(error_count, ==, 0);

    symbol_resolver_free(resolver);
    linker_object_free(obj1);
    linker_object_free(obj2);

    return MUNIT_OK;
}

/* Test: Undefined symbol error */
static MunitResult test_undefined_symbol_error(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create object with undefined symbol */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(obj);

    linker_symbol_t* sym = linker_object_add_symbol(obj);
    sym->name = strdup("undefined_function");
    sym->type = SYMBOL_TYPE_FUNC;
    sym->binding = SYMBOL_BINDING_GLOBAL;
    sym->is_defined = false;
    sym->value = 0;

    /* Create resolver and add object */
    symbol_resolver_t* resolver = symbol_resolver_new();
    munit_assert_not_null(resolver);

    symbol_resolver_add_object(resolver, obj, 0);

    /* Resolve symbols - should fail */
    bool result = symbol_resolver_resolve(resolver);
    munit_assert_false(result);

    /* Check errors */
    int error_count = 0;
    linker_error_t* errors = symbol_resolver_get_errors(resolver, &error_count);
    munit_assert_int(error_count, ==, 1);
    munit_assert_not_null(errors);
    munit_assert_int(errors[0].type, ==, LINKER_ERROR_UNDEFINED_SYMBOL);
    munit_assert_string_equal(errors[0].symbol_name, "undefined_function");

    symbol_resolver_free(resolver);
    linker_object_free(obj);

    return MUNIT_OK;
}

/* Test: Duplicate global symbols error */
static MunitResult test_duplicate_global_symbols_error(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create first object */
    linker_object_t* obj1 = linker_object_new("first.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(obj1);

    linker_symbol_t* sym1 = linker_object_add_symbol(obj1);
    sym1->name = strdup("duplicate");
    sym1->type = SYMBOL_TYPE_FUNC;
    sym1->binding = SYMBOL_BINDING_GLOBAL;
    sym1->is_defined = true;
    sym1->value = 0x1000;

    /* Create second object */
    linker_object_t* obj2 = linker_object_new("second.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(obj2);

    linker_symbol_t* sym2 = linker_object_add_symbol(obj2);
    sym2->name = strdup("duplicate");
    sym2->type = SYMBOL_TYPE_FUNC;
    sym2->binding = SYMBOL_BINDING_GLOBAL;
    sym2->is_defined = true;
    sym2->value = 0x2000;

    /* Create resolver and add objects */
    symbol_resolver_t* resolver = symbol_resolver_new();
    munit_assert_not_null(resolver);

    symbol_resolver_add_object(resolver, obj1, 0);
    symbol_resolver_add_object(resolver, obj2, 1);

    /* Resolve symbols - should fail */
    bool result = symbol_resolver_resolve(resolver);
    munit_assert_false(result);

    /* Check errors */
    int error_count = 0;
    linker_error_t* errors = symbol_resolver_get_errors(resolver, &error_count);
    munit_assert_int(error_count, ==, 1);
    munit_assert_not_null(errors);
    munit_assert_int(errors[0].type, ==, LINKER_ERROR_DUPLICATE_DEFINITION);
    munit_assert_string_equal(errors[0].symbol_name, "duplicate");

    symbol_resolver_free(resolver);
    linker_object_free(obj1);
    linker_object_free(obj2);

    return MUNIT_OK;
}

/* Test: Weak symbol override */
static MunitResult test_weak_symbol_override(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create first object with weak symbol */
    linker_object_t* obj1 = linker_object_new("weak.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(obj1);

    linker_symbol_t* sym1 = linker_object_add_symbol(obj1);
    sym1->name = strdup("override_me");
    sym1->type = SYMBOL_TYPE_FUNC;
    sym1->binding = SYMBOL_BINDING_WEAK;
    sym1->is_defined = true;
    sym1->value = 0x1000;

    /* Create second object with global symbol */
    linker_object_t* obj2 = linker_object_new("strong.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(obj2);

    linker_symbol_t* sym2 = linker_object_add_symbol(obj2);
    sym2->name = strdup("override_me");
    sym2->type = SYMBOL_TYPE_FUNC;
    sym2->binding = SYMBOL_BINDING_GLOBAL;
    sym2->is_defined = true;
    sym2->value = 0x2000;

    /* Create resolver and add objects */
    symbol_resolver_t* resolver = symbol_resolver_new();
    munit_assert_not_null(resolver);

    symbol_resolver_add_object(resolver, obj1, 0);
    symbol_resolver_add_object(resolver, obj2, 1);

    /* Resolve symbols - should succeed */
    bool result = symbol_resolver_resolve(resolver);
    munit_assert_true(result);

    /* Verify global symbol won */
    linker_symbol_t* found = symbol_resolver_lookup(resolver, "override_me");
    munit_assert_not_null(found);
    munit_assert_uint64(found->value, ==, 0x2000);  /* Global version */
    munit_assert_int(found->binding, ==, SYMBOL_BINDING_GLOBAL);

    /* Check errors */
    int error_count = 0;
    symbol_resolver_get_errors(resolver, &error_count);
    munit_assert_int(error_count, ==, 0);

    symbol_resolver_free(resolver);
    linker_object_free(obj1);
    linker_object_free(obj2);

    return MUNIT_OK;
}

/* Test: Runtime library symbols */
static MunitResult test_runtime_library_symbols(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    /* Create object with runtime symbol reference */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);
    munit_assert_not_null(obj);

    linker_symbol_t* sym = linker_object_add_symbol(obj);
    sym->name = strdup("sox_runtime_add");
    sym->type = SYMBOL_TYPE_FUNC;
    sym->binding = SYMBOL_BINDING_GLOBAL;
    sym->is_defined = false;
    sym->value = 0;

    /* Create resolver and add object */
    symbol_resolver_t* resolver = symbol_resolver_new();
    munit_assert_not_null(resolver);

    symbol_resolver_add_object(resolver, obj, 0);

    /* Resolve symbols - should succeed */
    bool result = symbol_resolver_resolve(resolver);
    munit_assert_true(result);

    /* Verify symbol was marked as runtime */
    munit_assert_int(sym->defining_object, ==, -1);  /* Runtime marker */

    /* Check errors */
    int error_count = 0;
    symbol_resolver_get_errors(resolver, &error_count);
    munit_assert_int(error_count, ==, 0);

    symbol_resolver_free(resolver);
    linker_object_free(obj);

    return MUNIT_OK;
}

/* Test: Hash table insert and lookup */
static MunitResult test_hash_table_insert_lookup(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    symbol_resolver_t* resolver = symbol_resolver_new();
    munit_assert_not_null(resolver);

    /* Create a dummy symbol */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);
    linker_symbol_t* sym = linker_object_add_symbol(obj);
    sym->name = strdup("test_symbol");
    sym->type = SYMBOL_TYPE_FUNC;
    sym->binding = SYMBOL_BINDING_GLOBAL;
    sym->is_defined = true;
    sym->value = 0x1234;

    /* Insert into hash table */
    bool inserted = symbol_table_insert(resolver, "test_symbol", sym, 0);
    munit_assert_true(inserted);

    /* Look up symbol */
    symbol_table_entry_t* entry = symbol_table_find(resolver, "test_symbol");
    munit_assert_not_null(entry);
    munit_assert_string_equal(entry->key, "test_symbol");
    munit_assert_ptr_equal(entry->symbol, sym);
    munit_assert_int(entry->object_index, ==, 0);

    /* Look up non-existent symbol */
    symbol_table_entry_t* missing = symbol_table_find(resolver, "missing");
    munit_assert_null(missing);

    symbol_resolver_free(resolver);
    linker_object_free(obj);

    return MUNIT_OK;
}

/* Test: Hash table resize */
static MunitResult test_hash_table_resize(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    symbol_resolver_t* resolver = symbol_resolver_new();
    munit_assert_not_null(resolver);

    size_t initial_size = resolver->table_size;

    /* Create object with many symbols to force resize */
    linker_object_t* obj = linker_object_new("test.o", PLATFORM_FORMAT_ELF);

    /* Add enough symbols to exceed load factor */
    /* Explicitly truncate to int (intentional floor operation for test capacity) */
    int num_symbols = (int)(initial_size * 0.8);  /* 80% of initial capacity */
    for (int i = 0; i < num_symbols; i++) {
        linker_symbol_t* sym = linker_object_add_symbol(obj);
        char name[64];
        snprintf(name, sizeof(name), "symbol_%d", i);
        sym->name = strdup(name);
        sym->type = SYMBOL_TYPE_FUNC;
        sym->binding = SYMBOL_BINDING_GLOBAL;
        sym->is_defined = true;
        sym->value = 0x1000 + i;

        bool inserted = symbol_table_insert(resolver, sym->name, sym, 0);
        munit_assert_true(inserted);
    }

    /* Table should have resized */
    munit_assert_size(resolver->table_size, >, initial_size);

    /* Verify all symbols are still accessible */
    for (int i = 0; i < num_symbols; i++) {
        char name[64];
        snprintf(name, sizeof(name), "symbol_%d", i);
        symbol_table_entry_t* entry = symbol_table_find(resolver, name);
        munit_assert_not_null(entry);
        munit_assert_string_equal(entry->key, name);
    }

    symbol_resolver_free(resolver);
    linker_object_free(obj);

    return MUNIT_OK;
}

/* Test: Error type names */
static MunitResult test_error_type_names(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

    munit_assert_string_equal(linker_error_type_name(LINKER_ERROR_NONE), "None");
    munit_assert_string_equal(linker_error_type_name(LINKER_ERROR_UNDEFINED_SYMBOL), "Undefined Symbol");
    munit_assert_string_equal(linker_error_type_name(LINKER_ERROR_DUPLICATE_DEFINITION), "Duplicate Definition");
    munit_assert_string_equal(linker_error_type_name(LINKER_ERROR_WEAK_SYMBOL_CONFLICT), "Weak Symbol Conflict");
    munit_assert_string_equal(linker_error_type_name(LINKER_ERROR_TYPE_MISMATCH), "Type Mismatch");
    munit_assert_string_equal(linker_error_type_name(LINKER_ERROR_ALLOCATION_FAILED), "Allocation Failed");

    return MUNIT_OK;
}

/* Test suite */
static MunitTest symbol_resolver_tests[] = {
    {"/lifecycle", test_symbol_resolver_lifecycle, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/hash_consistency", test_symbol_hash_consistency, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/is_runtime_symbol", test_is_runtime_symbol, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/single_object_all_defined", test_single_object_all_defined, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/two_objects_complementary", test_two_objects_complementary, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/undefined_symbol_error", test_undefined_symbol_error, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/duplicate_global_symbols_error", test_duplicate_global_symbols_error, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/weak_symbol_override", test_weak_symbol_override, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/runtime_library_symbols", test_runtime_library_symbols, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/hash_table_insert_lookup", test_hash_table_insert_lookup, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/hash_table_resize", test_hash_table_resize, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/error_type_names", test_error_type_names, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
};

const MunitSuite symbol_resolver_suite = {
    "symbol_resolver",
    symbol_resolver_tests,
    NULL,
    1,
    MUNIT_SUITE_OPTION_NONE
};
