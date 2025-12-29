
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "lib/native_api.h"
#include "vm_config.h"

#include "lib/table.h"
#include "object.h"
#include "vm.h"


static obj_error_t* _native_error(const char * message) {
    l_vm_runtime_error(message);
    obj_string_t* msg = l_copy_string(message, strlen(message));
    return l_new_error(msg, NULL);
}

static value_t _new_table(int argCount, value_t* args) {
    obj_table_t* table = l_new_table();
    return OBJ_VAL(table);
}

static value_t _len(int argCount, value_t* args) {
    if (argCount > 1 || argCount == 0) {
        return OBJ_VAL(_native_error("len(): invalid parameter count"));
    }
    switch (args[0].type) {
        case VAL_OBJ: {
            obj_t* obj = AS_OBJ(args[0]);
            switch (obj->type) {
                case OBJ_TABLE: {
                    obj_table_t* table = AS_TABLE(args[0]);
                    return NUMBER_VAL(table->table.count);
                }
                case OBJ_ARRAY: {
                    obj_array_t* array = AS_ARRAY(args[0]);
                    return NUMBER_VAL(array->values.count);
                }
                case OBJ_STRING: {
                    obj_string_t* string = AS_STRING(args[0]);
                    return NUMBER_VAL(string->length);
                }
                case OBJ_BOUND_METHOD:
                case OBJ_CLASS:
                case OBJ_CLOSURE:
                case OBJ_FUNCTION:
                case OBJ_INSTANCE:
                case OBJ_NATIVE:
                case OBJ_UPVALUE:
                case OBJ_ERROR:
                    break;
            }
        }
        case VAL_BOOL:
        case VAL_NIL:
        case VAL_NUMBER:
            break;
    }
    return OBJ_VAL(_native_error("len(): invalid parameter type"));
}

static value_t _push(int argCount, value_t* args) {
    if (argCount != 2) {
        return OBJ_VAL(_native_error("push(): invalid parameter count"));
    }
    if (args[0].type != VAL_OBJ) {
        return OBJ_VAL(_native_error("push(): invalid parameter type"));
    }
    obj_t* obj = AS_OBJ(args[0]);
    if (obj->type != OBJ_ARRAY) {
        return OBJ_VAL(_native_error("push(): invalid parameter type"));
    }
    obj_array_t* array = AS_ARRAY(args[0]);
    l_push_array(array, args[1]);
    return args[0];
}

static value_t _pop(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("pop(): invalid parameter count"));
    }
    if (args[0].type != VAL_OBJ) {
        return OBJ_VAL(_native_error("pop(): invalid parameter type"));
    }
    obj_t* obj = AS_OBJ(args[0]);
    if (obj->type != OBJ_ARRAY) {
        return OBJ_VAL(_native_error("pop(): invalid parameter type"));
    }
    obj_array_t* array = AS_ARRAY(args[0]);
    if (array->values.count == 0) {
        return OBJ_VAL(_native_error("pop(): array is empty"));
    }
    return l_pop_array(array);
}

static value_t _clock_native(int argCount, value_t* args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static value_t _usleep_native(int argCount, value_t* args) {
    if ( argCount == 1 && IS_NUMBER(args[0]) ) {
        return NUMBER_VAL(usleep((unsigned int)AS_NUMBER(args[0])));
    }
    return NUMBER_VAL(-1);
}

static value_t _type(int argCount, value_t* args) {
    if ( argCount == 1 ) {
        const char* type = NULL;
        switch (args[0].type) {
            case VAL_BOOL:
                type = "<bool>";
                break;
            case VAL_NIL:
                type = "<nil>";
                break;
            case VAL_NUMBER:
                type = "<number>";
                break;
            case VAL_OBJ: {
                obj_t* obj = AS_OBJ(args[0]);
                type = obj_type_to_string[obj->type];
                break;
            }
        }
        return OBJ_VAL(l_copy_string(type, strlen(type)));
    }
    const char* message = "type(): invalid argument(s)";
    l_vm_runtime_error(message);
    obj_string_t* msg = l_copy_string(message, strlen(message));
    return OBJ_VAL(l_new_error(msg, NULL));
}

// String functions
static value_t _string_upper(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("stringUpper(): invalid parameter count"));
    }
    if (!IS_STRING(args[0])) {
        return OBJ_VAL(_native_error("stringUpper(): parameter must be a string"));
    }
    obj_string_t* str = AS_STRING(args[0]);
    char* result = malloc(str->length + 1);
    if (result == NULL) {
        return OBJ_VAL(_native_error("stringUpper(): memory allocation failed"));
    }
    for (size_t i = 0; i < str->length; i++) {
        result[i] = toupper((unsigned char)str->chars[i]);
    }
    result[str->length] = '\0';
    return OBJ_VAL(l_take_string(result, str->length));
}

static value_t _string_lower(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("stringLower(): invalid parameter count"));
    }
    if (!IS_STRING(args[0])) {
        return OBJ_VAL(_native_error("stringLower(): parameter must be a string"));
    }
    obj_string_t* str = AS_STRING(args[0]);
    char* result = malloc(str->length + 1);
    if (result == NULL) {
        return OBJ_VAL(_native_error("stringLower(): memory allocation failed"));
    }
    for (size_t i = 0; i < str->length; i++) {
        result[i] = tolower((unsigned char)str->chars[i]);
    }
    result[str->length] = '\0';
    return OBJ_VAL(l_take_string(result, str->length));
}

static value_t _string_trim(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("stringTrim(): invalid parameter count"));
    }
    if (!IS_STRING(args[0])) {
        return OBJ_VAL(_native_error("stringTrim(): parameter must be a string"));
    }
    obj_string_t* str = AS_STRING(args[0]);

    // Handle empty string
    if (str->length == 0) {
        return OBJ_VAL(l_copy_string("", 0));
    }

    const char* start = str->chars;
    const char* end = str->chars + str->length - 1;

    while (start <= end && isspace((unsigned char)*start)) {
        start++;
    }
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }

    size_t new_length = end - start + 1;
    return OBJ_VAL(l_copy_string(start, new_length));
}

static value_t _string_index_of(int argCount, value_t* args) {
    if (argCount != 2) {
        return OBJ_VAL(_native_error("stringIndexOf(): invalid parameter count"));
    }
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return OBJ_VAL(_native_error("stringIndexOf(): both parameters must be strings"));
    }
    obj_string_t* haystack = AS_STRING(args[0]);
    obj_string_t* needle = AS_STRING(args[1]);

    const char* found = strstr(haystack->chars, needle->chars);
    if (found == NULL) {
        return NUMBER_VAL(-1);
    }
    return NUMBER_VAL(found - haystack->chars);
}

static value_t _string_substring(int argCount, value_t* args) {
    if (argCount < 2 || argCount > 3) {
        return OBJ_VAL(_native_error("stringSubstring(): requires 2 or 3 parameters"));
    }
    if (!IS_STRING(args[0]) || !IS_NUMBER(args[1])) {
        return OBJ_VAL(_native_error("stringSubstring(): invalid parameter types"));
    }

    obj_string_t* str = AS_STRING(args[0]);

    // Validate string length fits in int
    if (str->length > (size_t)INT_MAX) {
        return OBJ_VAL(_native_error("stringSubstring(): string too large"));
    }

    int start = (int)AS_NUMBER(args[1]);
    int end = (int)str->length;

    if (argCount == 3) {
        if (!IS_NUMBER(args[2])) {
            return OBJ_VAL(_native_error("stringSubstring(): end parameter must be a number"));
        }
        end = (int)AS_NUMBER(args[2]);
    }

    if (start < 0) start = 0;
    if (end > (int)str->length) end = (int)str->length;
    if (start >= end) return OBJ_VAL(l_copy_string("", 0));

    return OBJ_VAL(l_copy_string(str->chars + start, end - start));
}

static value_t _string_replace(int argCount, value_t* args) {
    if (argCount != 3) {
        return OBJ_VAL(_native_error("stringReplace(): requires 3 parameters"));
    }
    if (!IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2])) {
        return OBJ_VAL(_native_error("stringReplace(): all parameters must be strings"));
    }

    obj_string_t* str = AS_STRING(args[0]);
    obj_string_t* old = AS_STRING(args[1]);
    obj_string_t* new = AS_STRING(args[2]);

    if (old->length == 0) {
        return args[0];
    }

    const char* pos = strstr(str->chars, old->chars);
    if (pos == NULL) {
        return args[0];
    }

    size_t prefix_len = pos - str->chars;
    size_t suffix_len = str->length - prefix_len - old->length;
    size_t new_length = prefix_len + new->length + suffix_len;

    char* result = malloc(new_length + 1);
    if (result == NULL) {
        return OBJ_VAL(_native_error("stringReplace(): memory allocation failed"));
    }

    memcpy(result, str->chars, prefix_len);
    memcpy(result + prefix_len, new->chars, new->length);
    memcpy(result + prefix_len + new->length, pos + old->length, suffix_len);
    result[new_length] = '\0';

    return OBJ_VAL(l_take_string(result, new_length));
}

static value_t _string_starts_with(int argCount, value_t* args) {
    if (argCount != 2) {
        return OBJ_VAL(_native_error("stringStartsWith(): requires 2 parameters"));
    }
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return OBJ_VAL(_native_error("stringStartsWith(): both parameters must be strings"));
    }

    obj_string_t* str = AS_STRING(args[0]);
    obj_string_t* prefix = AS_STRING(args[1]);

    if (prefix->length > str->length) {
        return BOOL_VAL(false);
    }

    return BOOL_VAL(memcmp(str->chars, prefix->chars, prefix->length) == 0);
}

static value_t _string_ends_with(int argCount, value_t* args) {
    if (argCount != 2) {
        return OBJ_VAL(_native_error("stringEndsWith(): requires 2 parameters"));
    }
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return OBJ_VAL(_native_error("stringEndsWith(): both parameters must be strings"));
    }

    obj_string_t* str = AS_STRING(args[0]);
    obj_string_t* suffix = AS_STRING(args[1]);

    if (suffix->length > str->length) {
        return BOOL_VAL(false);
    }

    const char* start = str->chars + str->length - suffix->length;
    return BOOL_VAL(memcmp(start, suffix->chars, suffix->length) == 0);
}

static value_t _string_split(int argCount, value_t* args) {
    if (argCount != 2) {
        return OBJ_VAL(_native_error("stringSplit(): requires 2 parameters"));
    }
    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return OBJ_VAL(_native_error("stringSplit(): both parameters must be strings"));
    }

    obj_string_t* str = AS_STRING(args[0]);
    obj_string_t* delim = AS_STRING(args[1]);
    obj_array_t* result = l_new_array();

    if (delim->length == 0) {
        l_push_array(result, args[0]);
        return OBJ_VAL(result);
    }

    const char* current = str->chars;
    const char* end = str->chars + str->length;

    while (current < end) {
        const char* pos = strstr(current, delim->chars);
        if (pos == NULL) {
            size_t len = end - current;
            l_push_array(result, OBJ_VAL(l_copy_string(current, len)));
            break;
        }

        size_t len = pos - current;
        l_push_array(result, OBJ_VAL(l_copy_string(current, len)));
        current = pos + delim->length;
    }

    return OBJ_VAL(result);
}

// Math functions
static value_t _math_sqrt(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("mathSqrt(): invalid parameter count"));
    }
    if (!IS_NUMBER(args[0])) {
        return OBJ_VAL(_native_error("mathSqrt(): parameter must be a number"));
    }
    double value = AS_NUMBER(args[0]);
    if (value < 0) {
        return OBJ_VAL(_native_error("mathSqrt(): cannot take square root of negative number"));
    }
    return NUMBER_VAL(sqrt(value));
}

static value_t _math_pow(int argCount, value_t* args) {
    if (argCount != 2) {
        return OBJ_VAL(_native_error("mathPow(): requires 2 parameters"));
    }
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        return OBJ_VAL(_native_error("mathPow(): both parameters must be numbers"));
    }
    return NUMBER_VAL(pow(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

static value_t _math_floor(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("mathFloor(): invalid parameter count"));
    }
    if (!IS_NUMBER(args[0])) {
        return OBJ_VAL(_native_error("mathFloor(): parameter must be a number"));
    }
    return NUMBER_VAL(floor(AS_NUMBER(args[0])));
}

static value_t _math_ceil(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("mathCeil(): invalid parameter count"));
    }
    if (!IS_NUMBER(args[0])) {
        return OBJ_VAL(_native_error("mathCeil(): parameter must be a number"));
    }
    return NUMBER_VAL(ceil(AS_NUMBER(args[0])));
}

static value_t _math_round(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("mathRound(): invalid parameter count"));
    }
    if (!IS_NUMBER(args[0])) {
        return OBJ_VAL(_native_error("mathRound(): parameter must be a number"));
    }
    return NUMBER_VAL(round(AS_NUMBER(args[0])));
}

static value_t _math_sin(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("mathSin(): invalid parameter count"));
    }
    if (!IS_NUMBER(args[0])) {
        return OBJ_VAL(_native_error("mathSin(): parameter must be a number"));
    }
    return NUMBER_VAL(sin(AS_NUMBER(args[0])));
}

static value_t _math_cos(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("mathCos(): invalid parameter count"));
    }
    if (!IS_NUMBER(args[0])) {
        return OBJ_VAL(_native_error("mathCos(): parameter must be a number"));
    }
    return NUMBER_VAL(cos(AS_NUMBER(args[0])));
}

static value_t _math_tan(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("mathTan(): invalid parameter count"));
    }
    if (!IS_NUMBER(args[0])) {
        return OBJ_VAL(_native_error("mathTan(): parameter must be a number"));
    }
    return NUMBER_VAL(tan(AS_NUMBER(args[0])));
}

static value_t _math_random(int argCount, value_t* args) {
    if (argCount != 0) {
        return OBJ_VAL(_native_error("mathRandom(): takes no parameters"));
    }
    return NUMBER_VAL((double)rand() / RAND_MAX);
}

static value_t _math_random_int(int argCount, value_t* args) {
    if (argCount != 2) {
        return OBJ_VAL(_native_error("mathRandomInt(): requires 2 parameters"));
    }
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        return OBJ_VAL(_native_error("mathRandomInt(): both parameters must be numbers"));
    }
    int min = (int)AS_NUMBER(args[0]);
    int max = (int)AS_NUMBER(args[1]);
    if (min > max) {
        return OBJ_VAL(_native_error("mathRandomInt(): min must be <= max"));
    }
    return NUMBER_VAL(min + rand() % (max - min + 1));
}

static value_t _math_srand(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("mathSrand(): requires 1 parameter"));
    }
    if (!IS_NUMBER(args[0])) {
        return OBJ_VAL(_native_error("mathSrand(): seed must be a number"));
    }
    unsigned int seed = (unsigned int)AS_NUMBER(args[0]);
    srand(seed);
    return NIL_VAL;
}

// Array functions
static value_t _array_slice(int argCount, value_t* args) {
    if (argCount < 2 || argCount > 3) {
        return OBJ_VAL(_native_error("arraySlice(): requires 2 or 3 parameters"));
    }
    if (!IS_ARRAY(args[0])) {
        return OBJ_VAL(_native_error("arraySlice(): first parameter must be an array"));
    }
    if (!IS_NUMBER(args[1])) {
        return OBJ_VAL(_native_error("arraySlice(): start index must be a number"));
    }

    obj_array_t* source = AS_ARRAY(args[0]);

    // Validate array size fits in int
    if (source->values.count > (size_t)INT_MAX) {
        return OBJ_VAL(_native_error("arraySlice(): array too large"));
    }

    int start = (int)AS_NUMBER(args[1]);
    int end = (int)source->values.count;

    if (argCount == 3) {
        if (!IS_NUMBER(args[2])) {
            return OBJ_VAL(_native_error("arraySlice(): end index must be a number"));
        }
        end = (int)AS_NUMBER(args[2]);
    }

    if (start < 0) start = 0;
    if (end > (int)source->values.count) end = (int)source->values.count;
    if (start >= end) return OBJ_VAL(l_new_array());

    return OBJ_VAL(l_copy_array(source, start, end));
}

static value_t _array_reverse(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("arrayReverse(): requires 1 parameter"));
    }
    if (!IS_ARRAY(args[0])) {
        return OBJ_VAL(_native_error("arrayReverse(): parameter must be an array"));
    }

    obj_array_t* source = AS_ARRAY(args[0]);
    obj_array_t* result = l_new_array();

    // Validate array size fits in int
    if (source->values.count > (size_t)INT_MAX) {
        return OBJ_VAL(_native_error("arrayReverse(): array too large"));
    }

    for (int i = (int)source->values.count - 1; i >= 0; i--) {
        l_push_array(result, source->values.values[i]);
    }

    return OBJ_VAL(result);
}

static value_t _array_index_of(int argCount, value_t* args) {
    if (argCount != 2) {
        return OBJ_VAL(_native_error("arrayIndexOf(): requires 2 parameters"));
    }
    if (!IS_ARRAY(args[0])) {
        return OBJ_VAL(_native_error("arrayIndexOf(): first parameter must be an array"));
    }

    obj_array_t* arr = AS_ARRAY(args[0]);
    value_t search = args[1];

    for (int i = 0; i < arr->values.count; i++) {
        value_t elem = arr->values.values[i];

        if (elem.type == search.type) {
            if (IS_NUMBER(elem) && IS_NUMBER(search)) {
                if (AS_NUMBER(elem) == AS_NUMBER(search)) {
                    return NUMBER_VAL(i);
                }
            } else if (IS_BOOL(elem) && IS_BOOL(search)) {
                if (AS_BOOL(elem) == AS_BOOL(search)) {
                    return NUMBER_VAL(i);
                }
            } else if (IS_NIL(elem) && IS_NIL(search)) {
                return NUMBER_VAL(i);
            } else if (IS_OBJ(elem) && IS_OBJ(search)) {
                if (AS_OBJ(elem) == AS_OBJ(search)) {
                    return NUMBER_VAL(i);
                }
            }
        }
    }

    return NUMBER_VAL(-1);
}

static value_t _array_contains(int argCount, value_t* args) {
    if (argCount != 2) {
        return OBJ_VAL(_native_error("arrayContains(): requires 2 parameters"));
    }

    value_t index = _array_index_of(argCount, args);
    if (IS_NUMBER(index)) {
        return BOOL_VAL(AS_NUMBER(index) >= 0);
    }
    return BOOL_VAL(false);
}

static value_t _array_join(int argCount, value_t* args) {
    if (argCount != 2) {
        return OBJ_VAL(_native_error("arrayJoin(): requires 2 parameters"));
    }
    if (!IS_ARRAY(args[0])) {
        return OBJ_VAL(_native_error("arrayJoin(): first parameter must be an array"));
    }
    if (!IS_STRING(args[1])) {
        return OBJ_VAL(_native_error("arrayJoin(): separator must be a string"));
    }

    obj_array_t* arr = AS_ARRAY(args[0]);
    obj_string_t* sep = AS_STRING(args[1]);

    if (arr->values.count == 0) {
        return OBJ_VAL(l_copy_string("", 0));
    }

    // Helper to convert value to string representation
    char temp_buffer[64];

    // Calculate total length needed, converting non-strings
    size_t total_length = 0;
    for (int i = 0; i < arr->values.count; i++) {
        value_t val = arr->values.values[i];
        if (IS_STRING(val)) {
            total_length += AS_STRING(val)->length;
        } else if (IS_NUMBER(val)) {
            snprintf(temp_buffer, sizeof(temp_buffer), "%.14g", AS_NUMBER(val));
            total_length += strlen(temp_buffer);
        } else if (IS_BOOL(val)) {
            total_length += AS_BOOL(val) ? 4 : 5; // "true" or "false"
        } else if (IS_NIL(val)) {
            total_length += 3; // "nil"
        } else {
            total_length += 10; // Rough estimate for object types
        }

        if (i < arr->values.count - 1) {
            total_length += sep->length;
        }
    }

    char* result = malloc(total_length + 100); // Extra buffer for safety
    if (result == NULL) {
        return OBJ_VAL(_native_error("arrayJoin(): memory allocation failed"));
    }

    size_t pos = 0;
    for (int i = 0; i < arr->values.count; i++) {
        value_t val = arr->values.values[i];

        if (IS_STRING(val)) {
            obj_string_t* str = AS_STRING(val);
            memcpy(result + pos, str->chars, str->length);
            pos += str->length;
        } else if (IS_NUMBER(val)) {
            int written = snprintf(result + pos, total_length - pos + 100, "%.14g", AS_NUMBER(val));
            pos += written;
        } else if (IS_BOOL(val)) {
            const char* bool_str = AS_BOOL(val) ? "true" : "false";
            int len = AS_BOOL(val) ? 4 : 5;
            memcpy(result + pos, bool_str, len);
            pos += len;
        } else if (IS_NIL(val)) {
            memcpy(result + pos, "nil", 3);
            pos += 3;
        } else {
            // For objects, use a generic representation
            int written = snprintf(result + pos, total_length - pos + 100, "<object>");
            pos += written;
        }

        if (i < arr->values.count - 1) {
            memcpy(result + pos, sep->chars, sep->length);
            pos += sep->length;
        }
    }
    result[pos] = '\0';

    return OBJ_VAL(l_take_string(result, pos));
}

// Extern reference to VM for accessing config
extern vm_t vm;

// System functions
static value_t _sys_args(int argCount, value_t* args) {
    if (argCount != 0) {
        return OBJ_VAL(_native_error("sysArgs(): takes no parameters"));
    }

    obj_array_t* result = l_new_array();

    if (vm.config != NULL && vm.config->args.argc > 0) {
        for (int i = 0; i < vm.config->args.argc; i++) {
            obj_string_t* arg = l_copy_string(vm.config->args.argv[i], strlen(vm.config->args.argv[i]));
            l_push_array(result, OBJ_VAL(arg));
        }
    }

    return OBJ_VAL(result);
}

static value_t _sys_getenv(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("sysGetenv(): requires 1 parameter"));
    }
    if (!IS_STRING(args[0])) {
        return OBJ_VAL(_native_error("sysGetenv(): parameter must be a string"));
    }

    obj_string_t* name = AS_STRING(args[0]);
    const char* value = getenv(name->chars);

    if (value == NULL) {
        return NIL_VAL;
    }

    return OBJ_VAL(l_copy_string(value, strlen(value)));
}

static value_t _sys_on_exit(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("sysOnExit(): requires 1 parameter (function)"));
    }

    value_t handler = args[0];

    if (!IS_CLOSURE(handler) && !IS_FUNCTION(handler)) {
        return OBJ_VAL(_native_error("sysOnExit(): parameter must be a function"));
    }

    l_vm_register_exit_handler(handler);
    return NIL_VAL;
}

static value_t _sys_exit(int argCount, value_t* args) {
    int exit_code = 0;

    if (argCount == 1) {
        if (!IS_NUMBER(args[0])) {
            return OBJ_VAL(_native_error("sysExit(): exit code must be a number"));
        }
        exit_code = (int)AS_NUMBER(args[0]);
    } else if (argCount > 1) {
        return OBJ_VAL(_native_error("sysExit(): takes 0 or 1 parameters"));
    }

    // Call cleanup before exiting
    l_vm_cleanup();

    exit(exit_code);
    return NIL_VAL;
}

static value_t _sys_platform(int argCount, value_t* args) {
    if (argCount != 0) {
        return OBJ_VAL(_native_error("sysPlatform(): takes no parameters"));
    }

#if defined(_WIN32) || defined(_WIN64)
    return OBJ_VAL(l_copy_string("windows", 7));
#elif defined(__APPLE__) || defined(__MACH__)
    return OBJ_VAL(l_copy_string("darwin", 6));
#elif defined(__linux__)
    return OBJ_VAL(l_copy_string("linux", 5));
#elif defined(__unix__)
    return OBJ_VAL(l_copy_string("unix", 4));
#else
    return OBJ_VAL(l_copy_string("unknown", 7));
#endif
}

static value_t _sys_sleep(int argCount, value_t* args) {
    if (argCount != 1) {
        return OBJ_VAL(_native_error("sysSleep(): requires 1 parameter"));
    }
    if (!IS_NUMBER(args[0])) {
        return OBJ_VAL(_native_error("sysSleep(): parameter must be a number"));
    }

    double seconds = AS_NUMBER(args[0]);
    if (seconds < 0) {
        return OBJ_VAL(_native_error("sysSleep(): sleep time cannot be negative"));
    }

#if defined(_WIN32) || defined(_WIN64)
    Sleep((DWORD)(seconds * 1000));
#else
    usleep((useconds_t)(seconds * 1000000));
#endif

    return NIL_VAL;
}

void l_table_add_native() {
    l_vm_define_native("Table", _new_table);

    l_vm_define_native("len", _len);
    // l_vm_define_native("set", _set);
    // l_vm_define_native("get", _get);


    // TODO: Table functions
    // len()
    // add()
    // get()
    // set()
    // range()

    // array functions
    l_vm_define_native("push", _push);
    l_vm_define_native("pop", _pop);

    l_vm_define_native("type", _type);

    // String functions
    l_vm_define_native("stringUpper", _string_upper);
    l_vm_define_native("stringLower", _string_lower);
    l_vm_define_native("stringTrim", _string_trim);
    l_vm_define_native("stringIndexOf", _string_index_of);
    l_vm_define_native("stringSubstring", _string_substring);
    l_vm_define_native("stringReplace", _string_replace);
    l_vm_define_native("stringStartsWith", _string_starts_with);
    l_vm_define_native("stringEndsWith", _string_ends_with);
    l_vm_define_native("stringSplit", _string_split);

    // Math functions
    l_vm_define_native("mathSqrt", _math_sqrt);
    l_vm_define_native("mathPow", _math_pow);
    l_vm_define_native("mathFloor", _math_floor);
    l_vm_define_native("mathCeil", _math_ceil);
    l_vm_define_native("mathRound", _math_round);
    l_vm_define_native("mathSin", _math_sin);
    l_vm_define_native("mathCos", _math_cos);
    l_vm_define_native("mathTan", _math_tan);
    l_vm_define_native("mathRandom", _math_random);
    l_vm_define_native("mathRandomInt", _math_random_int);
    l_vm_define_native("mathSrand", _math_srand);

    // Array functions
    l_vm_define_native("arraySlice", _array_slice);
    l_vm_define_native("arrayReverse", _array_reverse);
    l_vm_define_native("arrayIndexOf", _array_index_of);
    l_vm_define_native("arrayContains", _array_contains);
    l_vm_define_native("arrayJoin", _array_join);

    // System functions
    l_vm_define_native("sysArgs", _sys_args);
    l_vm_define_native("sysGetenv", _sys_getenv);
    l_vm_define_native("sysOnExit", _sys_on_exit);
    l_vm_define_native("sysExit", _sys_exit);
    l_vm_define_native("sysPlatform", _sys_platform);
    l_vm_define_native("sysSleep", _sys_sleep);

    // native functions
    l_vm_define_native("clock", _clock_native);
    l_vm_define_native("usleep", _usleep_native);
}