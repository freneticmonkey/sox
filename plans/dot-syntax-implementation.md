# Dot Syntax Implementation Plan

## Overview

Add dot notation syntax for table property access to Sox, enabling cleaner code like `table.key` instead of `table["key"]`. This builds on the recently implemented table literal syntax to provide a more ergonomic developer experience.

## Current State

### What We Have
- ✅ Table literal syntax: `{key: value, foo: bar}`
- ✅ Bracket notation: `table["key"]`
- ✅ Module system using tables for exports
- ✅ `OP_GET_PROPERTY` and `OP_SET_PROPERTY` opcodes (for class instances)

### What's Missing
- ❌ Dot notation for table access: `table.key`
- ❌ Distinction between table property access and class property access

## Goals

1. **Primary**: Enable `table.key` syntax for reading table properties
2. **Secondary**: Enable `table.key = value` syntax for writing table properties
3. **Tertiary**: Update example modules to use dot notation where appropriate

## Design Decisions

### Decision 1: Compile-Time vs Runtime Approach

**Option A: Compile-Time Desugaring (RECOMMENDED)**
- Convert `table.key` to `table["key"]` during compilation
- Reuse existing `OP_GET_INDEX`/`OP_SET_INDEX` opcodes
- No VM changes required
- Simpler implementation
- Zero runtime overhead

**Option B: New Runtime Opcodes**
- Create `OP_GET_TABLE_PROPERTY`/`OP_SET_TABLE_PROPERTY` opcodes
- Requires VM changes
- More flexibility for future optimizations
- Slight runtime overhead for opcode dispatch

**Recommendation**: Start with **Option A** (compile-time desugaring) for simplicity and performance. Can migrate to Option B later if needed for optimizations.

### Decision 2: Handling Ambiguity with Classes

Currently Sox has:
- Classes with instance properties (using `OP_GET_PROPERTY`/`OP_SET_PROPERTY`)
- Tables with key-value storage (using `OP_GET_INDEX`/`OP_SET_INDEX`)

**Challenge**: How does `obj.field` know whether `obj` is a class instance or a table?

**Solution**: Type-based dispatch at runtime
- Both syntax forms (`obj.field` and `obj["field"]`) work for both types
- At runtime, the VM checks the value type and uses appropriate operation
- For `OP_GET_PROPERTY`: Check if OBJ_INSTANCE, fall back to table lookup
- For `OP_GET_INDEX`: Check if OBJ_TABLE, fall back to instance property

This is actually the **current behavior** - we can leverage existing opcodes!

### Decision 3: Identifier Restrictions

**Question**: Should dot notation support all table keys or only valid identifiers?

**Answer**: Only valid identifiers
- `table.foo` ✅ (valid identifier)
- `table.my_var` ✅ (valid identifier)
- `table.123` ❌ (not a valid identifier, use `table["123"]`)
- `table.my-key` ❌ (not a valid identifier, use `table["my-key"]`)

This matches JavaScript, Python, and most other languages.

## Implementation Strategy

### Phase 1: Read-Only Dot Access (GET)

Enable `table.key` for reading values.

#### 1.1 Update Scanner (No Changes Needed)
- `TOKEN_DOT` already exists
- `TOKEN_IDENTIFIER` already exists

#### 1.2 Update Compiler Parser

**File**: `src/compiler.c`

**Current State**: The dot operator is used in the Pratt parser for:
- Method calls on instances: `instance.method()`
- Property access on instances: `instance.field`

**Changes Needed**:
```c
// In the _dot() function (infix handler for TOKEN_DOT):
static void _dot(bool canAssign) {
    _consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = _identifier_constant(&_parser.previous);

    if (canAssign && _match(TOKEN_EQUAL)) {
        // Setting: obj.field = value
        _expression();
        _emit_bytes(OP_SET_PROPERTY, name);
    } else if (_match(TOKEN_LEFT_PAREN)) {
        // Method call: obj.method()
        uint8_t argCount = _argument_list();
        _emit_bytes(OP_INVOKE, name);
        _emit_byte(argCount);
    } else {
        // Getting: obj.field
        _emit_bytes(OP_GET_PROPERTY, name);
    }
}
```

**Analysis**: The current implementation already emits `OP_GET_PROPERTY` and `OP_SET_PROPERTY`. We need to check if the VM opcodes handle tables correctly.

#### 1.3 Update VM Opcodes

**File**: `src/vm.c`

**Current `OP_GET_PROPERTY` Implementation**:
```c
case OP_GET_PROPERTY: {
    if (!IS_INSTANCE(_peek(0))) {
        _runtime_error("Only instances have properties.");
        return INTERPRET_RUNTIME_ERROR;
    }

    obj_instance_t* instance = AS_INSTANCE(_peek(0));
    obj_string_t* name = READ_STRING();

    value_t value;
    if (l_table_get(&instance->fields, name, &value)) {
        _pop(); // Instance
        _push(value);
        break;
    }

    // ... method binding logic
}
```

**Problem**: Hardcoded check for `IS_INSTANCE` - rejects tables!

**Solution**: Extend to support tables:
```c
case OP_GET_PROPERTY: {
    value_t receiver = _peek(0);
    obj_string_t* name = READ_STRING();

    // Handle tables
    if (IS_TABLE(receiver)) {
        obj_table_t* table = AS_TABLE(receiver);
        value_t value;
        if (l_table_get(&table->table, name, &value)) {
            _pop(); // Table
            _push(value);
            break;
        }
        // Key not found - push nil or error?
        _runtime_error("Table has no property '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    // Handle instances (existing code)
    if (!IS_INSTANCE(receiver)) {
        _runtime_error("Only instances and tables have properties.");
        return INTERPRET_RUNTIME_ERROR;
    }

    obj_instance_t* instance = AS_INSTANCE(receiver);
    value_t value;
    if (l_table_get(&instance->fields, name, &value)) {
        _pop(); // Instance
        _push(value);
        break;
    }

    // ... rest of method binding logic
}
```

**Similar changes for `OP_SET_PROPERTY`**:
```c
case OP_SET_PROPERTY: {
    value_t receiver = _peek(1);
    obj_string_t* name = READ_STRING();

    // Handle tables
    if (IS_TABLE(receiver)) {
        obj_table_t* table = AS_TABLE(receiver);
        l_table_set(&table->table, name, _peek(0));
        value_t value = _pop();
        _pop(); // Table
        _push(value);
        break;
    }

    // Handle instances (existing code)
    if (!IS_INSTANCE(receiver)) {
        _runtime_error("Only instances and tables have fields.");
        return INTERPRET_RUNTIME_ERROR;
    }

    obj_instance_t* instance = AS_INSTANCE(receiver);
    l_table_set(&instance->fields, name, _peek(0));
    value_t value = _pop();
    _pop();
    _push(value);
    break;
}
```

#### 1.4 Handle Method Invocation

**Current `OP_INVOKE` Implementation**: Only works with instances

**Question**: Should `table.func()` work if `table["func"]` is a function?

**Answer**: YES - for consistency and usability

**Change Needed**: Check if receiver is table, lookup function, then call it
```c
case OP_INVOKE: {
    obj_string_t* method = READ_STRING();
    int arg_count = READ_BYTE();

    value_t receiver = _peek(arg_count);

    // Handle tables - lookup and call function
    if (IS_TABLE(receiver)) {
        obj_table_t* table = AS_TABLE(receiver);
        value_t value;
        if (l_table_get(&table->table, method, &value)) {
            if (IS_CLOSURE(value) || IS_FUNCTION(value)) {
                // Replace table with function and call
                _vm.stack_top[-arg_count - 1] = value;
                return _call_value(value, arg_count);
            }
        }
        _runtime_error("Table property '%s' is not callable.", method->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    // Handle instances (existing code)
    if (!IS_INSTANCE(receiver)) {
        _runtime_error("Only instances have methods.");
        return INTERPRET_RUNTIME_ERROR;
    }

    // ... rest of instance method invocation
}
```

### Phase 2: Writable Dot Access (SET)

The `OP_SET_PROPERTY` changes above already handle this!

### Phase 3: Testing

#### 3.1 Unit Tests

Create `src/test/dot_syntax_test.c`:
```c
// Test table property get
MU_TEST(test_table_dot_get) {
    const char* source =
        "var t = {x: 42, y: 'hello'}\n"
        "print(t.x)\n"
        "print(t.y)\n";
    // Assert: prints 42 and "hello"
}

// Test table property set
MU_TEST(test_table_dot_set) {
    const char* source =
        "var t = {x: 1}\n"
        "t.x = 99\n"
        "print(t.x)\n";
    // Assert: prints 99
}

// Test table method call
MU_TEST(test_table_method_call) {
    const char* source =
        "fn greet(name) { return 'Hello ' + name }\n"
        "var obj = {greet: greet}\n"
        "print(obj.greet('World'))\n";
    // Assert: prints "Hello World"
}

// Test mixed instance and table
MU_TEST(test_instance_and_table_coexist) {
    const char* source =
        "class Foo { init() { this.x = 1 } }\n"
        "var inst = Foo()\n"
        "var tab = {x: 2}\n"
        "print(inst.x)\n"
        "print(tab.x)\n";
    // Assert: prints 1 and 2
}
```

#### 3.2 Integration Tests

Create `src/test/scripts/dot_syntax.sox`:
```sox
// Test 1: Basic table access
var person = {
    name: "Alice",
    age: 30
}

print(person.name)
print(person.age)

// Test 2: Nested tables
var data = {
    user: {
        name: "Bob",
        id: 123
    }
}

print(data.user.name)
print(data.user.id)

// Test 3: Table modification
person.age = 31
print(person.age)

// Test 4: Module-style usage
import "simple_module"

print(simple_module.message)
simple_module.greet("World")
var result = simple_module.add(10, 20)
print(result)
```

Expected output in `src/test/scripts/dot_syntax.sox.out`.

#### 3.3 Update Example Files

Update all example test files to use dot notation:

**File**: `examples/test_simple.sox`
```sox
// BEFORE
import "simple_module"
print(simple_module["message"])
simple_module["greet"]("World")

// AFTER
import "simple_module"
print(simple_module.message)
simple_module.greet("World")
```

**File**: `examples/test_imports.sox`
```sox
// BEFORE
print(math["pi"])
var result = math["square"](5)

// AFTER
print(math.pi)
var result = math.square(5)
```

Apply to all examples in `examples/` directory.

### Phase 4: Documentation

#### 4.1 Update README.md

Add dot syntax to language features:
```markdown
### Table Access

Sox supports both bracket and dot notation for table access:

```sox
var person = {name: "Alice", age: 30}

// Bracket notation - works with any string key
print(person["name"])    // "Alice"
person["age"] = 31

// Dot notation - cleaner syntax for identifier keys
print(person.name)       // "Alice"
person.age = 31

// Use bracket notation for non-identifier keys
var data = {"key-with-dashes": 42}
print(data["key-with-dashes"])  // Must use brackets
```
```

#### 4.2 Update Module System Docs

Update examples to show both syntaxes work.

## Implementation Checklist

### Phase 1: Core Implementation
- [ ] Review current `_dot()` parser function in `src/compiler.c`
- [ ] Extend `OP_GET_PROPERTY` in `src/vm.c` to handle tables
- [ ] Extend `OP_SET_PROPERTY` in `src/vm.c` to handle tables
- [ ] Extend `OP_INVOKE` in `src/vm.c` to handle table method calls
- [ ] Add table type checks and error messages

### Phase 2: Testing
- [ ] Create `src/test/dot_syntax_test.c` with unit tests
- [ ] Create `src/test/scripts/dot_syntax.sox` integration test
- [ ] Create expected output file `dot_syntax.sox.out`
- [ ] Run test suite and verify all tests pass
- [ ] Test edge cases (nil properties, nested access, etc.)

### Phase 3: Examples
- [ ] Update `examples/test_simple.sox`
- [ ] Update `examples/test_imports.sox`
- [ ] Update `examples/test_alias.sox`
- [ ] Update `examples/test_multi_import.sox`
- [ ] Update all other example files
- [ ] Verify all examples still work

### Phase 4: Documentation
- [ ] Update README.md with dot syntax examples
- [ ] Update module system documentation
- [ ] Add language reference section for property access
- [ ] Document bracket vs dot notation trade-offs

### Phase 5: Edge Cases
- [ ] Test `nil` table access: `nil.field` should error
- [ ] Test undefined properties: `table.missing` should error or return nil?
- [ ] Test numeric keys: `table.123` should be syntax error
- [ ] Test reserved keywords: `table.if` - allowed or error?
- [ ] Test chaining: `a.b.c.d` should work
- [ ] Test with function calls: `getTable().field` should work

## Potential Issues and Solutions

### Issue 1: Backward Compatibility

**Problem**: Existing code uses bracket notation extensively.

**Solution**: Both notations work side-by-side. No breaking changes.

### Issue 2: Reserved Keywords

**Problem**: What if table key is a reserved word like `if`, `while`, `class`?

**Solution**:
- Dot notation: `table.if` - syntax error (can't use keyword as identifier)
- Bracket notation: `table["if"]` - works fine
- Document this limitation

### Issue 3: nil Property Access

**Problem**: Should `table.missing` return `nil` or error?

**Analysis**:
- JavaScript: returns `undefined` (equivalent to nil)
- Python: raises `AttributeError`
- Lua: returns `nil`

**Recommendation**: Return `nil` for missing properties (Lua-like behavior)
- More flexible for optional properties
- Matches table behavior with bracket notation
- Simplifies code generation

**Implementation**: Change error to return nil:
```c
if (l_table_get(&table->table, name, &value)) {
    _pop();
    _push(value);
    break;
}
// Key not found - push nil instead of error
_pop();
_push(NIL_VAL);
break;
```

### Issue 4: Performance

**Problem**: Is dot notation slower than bracket notation?

**Analysis**:
- Both use same opcodes (`OP_GET_PROPERTY`)
- Same runtime performance
- Dot notation is purely syntactic sugar
- No performance difference

## Alternative Approaches Considered

### Alternative 1: Separate Opcodes for Tables

Create `OP_GET_TABLE_FIELD` and `OP_SET_TABLE_FIELD` separate from instance property opcodes.

**Pros**:
- Cleaner separation of concerns
- Potentially optimize table access differently
- Clearer debugging

**Cons**:
- More opcodes to maintain
- Duplicate code between table and instance handling
- No clear performance benefit

**Decision**: Rejected - reuse existing opcodes

### Alternative 2: Compile-Time Type Checking

Try to determine at compile-time whether expression is table or instance.

**Pros**:
- Could generate optimal code for known types
- Potential performance improvement

**Cons**:
- Sox is dynamically typed - types not known at compile time
- Would only work for literal tables
- Significant complexity
- Limited benefit

**Decision**: Rejected - too complex for dynamic language

### Alternative 3: Method Syntax Sugar Only

Only support `table.func()` method calls, not `table.field` property access.

**Pros**:
- Simpler implementation
- Less ambiguity

**Cons**:
- Inconsistent with table literals using identifier keys
- Less useful for module system
- Users expect full dot notation

**Decision**: Rejected - implement full dot notation

## Success Criteria

Implementation is complete when:

1. ✅ All existing tests pass
2. ✅ New dot syntax tests pass (unit + integration)
3. ✅ All example files updated and working
4. ✅ Documentation updated with examples
5. ✅ Both `table.key` and `table["key"]` work identically
6. ✅ Both instance properties and table properties work with dot notation
7. ✅ Error messages are clear and helpful
8. ✅ No performance regression

## Timeline Estimate

- Phase 1 (Core Implementation): ~2-3 hours
- Phase 2 (Testing): ~1-2 hours
- Phase 3 (Examples): ~1 hour
- Phase 4 (Documentation): ~1 hour
- **Total**: ~5-7 hours of focused work

## Future Enhancements

After basic dot syntax is working:

1. **Optional Chaining**: `table?.field` (returns nil if table is nil)
2. **Computed Properties**: Support expressions like `table[expr]` more fully
3. **Destructuring**: `var {x, y} = point` (separate feature)
4. **Property Shortcuts**: `{x, y}` equivalent to `{x: x, y: y}` (already done in literals)

## References

- Current implementation: `src/compiler.c` `_dot()` function
- Existing opcodes: `src/chunk.h` `OP_GET_PROPERTY`, `OP_SET_PROPERTY`
- Table implementation: `src/object.c` `obj_table_t`
- Similar feature in Wren: https://wren.io/maps.html
- Similar feature in Lua: https://www.lua.org/manual/5.4/manual.html#3.4.7
