# Proposal: Add 'with' Statement Support to Sox

**Author:** Claude
**Date:** 2025-12-25
**Status:** Draft
**Target Version:** TBD

## Table of Contents

1. [Overview](#overview)
2. [Motivation](#motivation)
3. [Syntax and Semantics](#syntax-and-semantics)
4. [Design Rationale](#design-rationale)
5. [Implementation Strategy](#implementation-strategy)
6. [Examples](#examples)
7. [Edge Cases and Considerations](#edge-cases-and-considerations)
8. [Testing Strategy](#testing-strategy)
9. [Implementation Plan](#implementation-plan)
10. [Alternatives Considered](#alternatives-considered)
11. [Open Questions](#open-questions)

---

## Overview

This proposal introduces a `with` statement to Sox, providing automatic resource management and cleanup through scoped variable binding. The `with` statement ensures resources are properly cleaned up when they go out of scope, similar to Python's context managers, C#'s `using` statement, or Go's `defer` pattern.

**Key Benefits:**
- Automatic resource cleanup (files, connections, locks, etc.)
- Reduced boilerplate for common resource patterns
- Guaranteed cleanup even with early returns or errors
- Composable with existing closure and scoping mechanisms

---

## Motivation

### Current State

Currently, Sox requires manual resource management:

```javascript
var file = open("data.txt")
var content = file.read()
file.close()  // Easy to forget!
```

**Problems with manual cleanup:**
1. **Error-prone**: Developers may forget to call cleanup methods
2. **Verbose**: Requires explicit cleanup code
3. **Complex control flow**: Early returns or nested conditions make cleanup tricky
4. **No guarantees**: No enforcement that cleanup happens

### Proposed Solution

With the `with` statement:

```javascript
with file = open("data.txt") {
    var content = file.read()
    // file.close() called automatically
}
```

**Advantages:**
- Cleanup is automatic and guaranteed
- Cleaner, more readable code
- Works with any resource that implements a cleanup protocol
- Integrates naturally with Sox's existing scoping and closure mechanisms

---

## Syntax and Semantics

### Basic Syntax

```
with <identifier> = <expression> {
    <statements>
}
```

**Grammar (EBNF):**
```ebnf
withStatement  → "with" "(" identifier "=" expression ")" block ;
block          → "{" declaration* "}" ;
```

### Semantics

1. **Acquisition**: Evaluate `<expression>` and bind result to `<identifier>`
2. **Scope**: `<identifier>` is scoped to the with block
3. **Usage**: Execute `<statements>` with access to `<identifier>`
4. **Cleanup**: When exiting the block (normally or via return/break), automatically call cleanup

**Cleanup Protocol:**

The `with` statement will call a cleanup method on the resource. Three options:

**Option A: Magic Method Convention**
- Resources implement `__cleanup()` method
- Called automatically on scope exit
- Example: `file.__cleanup()` → `file.close()`

**Option B: Explicit Cleanup Expression**
- Cleanup logic specified inline
- Syntax: `with x = expr cleanup { cleanup_expr }`
- More flexible but more verbose

**Option C: Defer Integration**
- Syntactic sugar for variable binding + defer
- Uses existing defer mechanism
- Simplest implementation

**Recommendation: Option A** (magic method) - most ergonomic and follows Python/C# precedent.

### Execution Order

```javascript
with resource = acquire() {
    print("using")
}
print("done")
```

Equivalent to:

```javascript
{
    var resource = acquire()
    defer () { resource.__cleanup() }
    {
        print("using")
    }
}
print("done")
```

**Output:**
```
using
done
```

Cleanup happens after "using" but before "done" (at scope exit).

### Nested With Statements

Multiple resources cleaned up in reverse order:

```javascript
with a = resource_a() {
    with b = resource_b() {
        with c = resource_c() {
            // Use a, b, c
        }
        // c cleaned up here
    }
    // b cleaned up here
}
// a cleaned up here
```

**Cleanup order:** c → b → a (LIFO)

---

## Design Rationale

### Why Not Just Use Defer?

Defer is powerful but requires explicit cleanup calls:

```javascript
var file = open("data.txt")
defer () { file.close() }
// Use file...
```

The `with` statement:
- Makes intent clearer (resource management vs. general cleanup)
- Reduces boilerplate
- Enforces cleanup protocol
- Provides better ergonomics for the common case

**Both mechanisms are valuable:**
- `with`: For resource management (files, locks, connections)
- `defer`: For general cleanup logic (logging, state restoration)

### Why Magic Methods?

Using `__cleanup()` convention:
- **Familiar**: Follows Python's `__enter__/__exit__`, Go's `defer`
- **Flexible**: Any object can implement cleanup
- **Discoverable**: Clear protocol for resource types
- **Type-agnostic**: Works with Sox's dynamic typing

### Scoping Design

The resource variable is scoped to the with block, following Sox's existing scoping rules:
- Prevents accidental use after cleanup
- Works naturally with closures (via upvalues)
- Integrates with existing `_begin_scope()`/`_end_scope()` mechanism

---

## Implementation Strategy

### Overview

The `with` statement can be implemented entirely with existing Sox infrastructure:
- Scoping mechanism (scope depth tracking)
- Defer mechanism (cleanup scheduling)
- Upvalue system (closure capture)

**No new VM opcodes required!**

### Phase 1: Scanner (Lexical Analysis)

**File:** `src/scanner.h`, `src/scanner.c`

**Changes:**

1. Add `TOKEN_WITH` to `TokenType` enum:

```c
// In src/scanner.h
typedef enum {
    // ... existing tokens ...
    TOKEN_WHILE,
    TOKEN_WITH,        // NEW
    // ... rest ...
} TokenType;
```

2. Add keyword recognition in `_identifier_type()`:

```c
// In src/scanner.c, _identifier_type()
case 'w':
    if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
            case 'h': return _check_keyword(2, 3, "ile", TOKEN_WHILE);
            case 'i': return _check_keyword(2, 2, "th", TOKEN_WITH);  // NEW
        }
    }
    break;
```

**Estimated effort:** ~10 lines of code

### Phase 2: Compiler (Parsing and Code Generation)

**File:** `src/compiler.c`

**Changes:**

1. Add with statement handler to statement dispatcher:

```c
// In _statement() function
static void _statement() {
    if (_match(TOKEN_PRINT)) {
        _print_statement();
    // ... other statements ...
    } else if (_match(TOKEN_WITH)) {
        _with_statement();  // NEW
    // ...
}
```

2. Implement `_with_statement()`:

```c
static void _with_statement() {
    // Begin new scope for resource variable
    _begin_scope();

    // Parse: with ( <name> = <expr> )
    _consume(TOKEN_LEFT_PAREN, "Expect '(' after 'with'.");

    // Parse variable name
    _consume(TOKEN_IDENTIFIER, "Expect variable name.");
    token_t name_token = parser.previous;
    uint8_t name_var = _parse_variable("Expect variable name in with statement.");

    _consume(TOKEN_EQUAL, "Expect '=' after variable name in with statement.");

    // Parse and emit resource acquisition expression
    _expression();

    // Define the variable (now initialized)
    _define_variable(name_var);
    _mark_initialized();

    _consume(TOKEN_RIGHT_PAREN, "Expect ')' after with expression.");

    // Generate cleanup code using defer mechanism
    // Approach: Emit code that stores a cleanup closure

    // Store current local count (resource variable is at top)
    int resource_local = _current->local_count - 1;

    // Parse and execute body block
    _consume(TOKEN_LEFT_BRACE, "Expect '{' before with body.");
    _block();
    _consume(TOKEN_RIGHT_BRACE, "Expect '}' after with body.");

    // Emit cleanup before scope ends
    // Get resource, call __cleanup() method
    _emit_bytes(OP_GET_LOCAL, (uint8_t)resource_local);
    _emit_constant(OBJ_VAL(l_copy_string("__cleanup", 9)));
    _emit_bytes(OP_INVOKE, 0);  // Call with 0 arguments
    _emit_byte(OP_POP);  // Pop return value

    // End scope (pops resource variable)
    _end_scope();
}
```

**Alternative: Use Defer Mechanism**

For simpler implementation, leverage existing defer:

```c
static void _with_statement() {
    _begin_scope();

    // Parse and bind resource
    _consume(TOKEN_LEFT_PAREN, "Expect '(' after 'with'.");
    uint8_t name_var = _parse_variable("Expect variable name.");
    _consume(TOKEN_EQUAL, "Expect '='.");
    _expression();
    _define_variable(name_var);
    _mark_initialized();
    _consume(TOKEN_RIGHT_PAREN, "Expect ')'.");

    int resource_local = _current->local_count - 1;

    // Auto-generate defer cleanup
    // Create anonymous function: () { resource.__cleanup() }
    compiler_t cleanup_compiler;
    _init_compiler(&cleanup_compiler, TYPE_DEFER);
    _begin_scope();

    // Emit: GET_LOCAL resource, INVOKE __cleanup, POP
    _emit_bytes(OP_GET_UPVALUE, _add_upvalue(&cleanup_compiler, resource_local, true));
    _emit_constant(OBJ_VAL(l_copy_string("__cleanup", 9)));
    _emit_bytes(OP_INVOKE, 0);
    _emit_byte(OP_POP);

    obj_function_t* cleanup_fn = _end_compiler();
    _emit_bytes(OP_CLOSURE, _make_constant(OBJ_VAL(cleanup_fn)));

    // Register as deferred function
    _current->deferred_functions[_current->defer_count++] = _current->local_count;
    _current->local_count++;

    // Parse body
    _consume(TOKEN_LEFT_BRACE, "Expect '{'.");
    _block();
    _consume(TOKEN_RIGHT_BRACE, "Expect '}'.");

    _end_scope();
}
```

**Estimated effort:** ~80-120 lines of code

### Phase 3: VM Execution

**File:** `src/vm.c`

**Changes:** None required!

The `with` statement uses existing opcodes:
- `OP_GET_LOCAL` - Access resource variable
- `OP_INVOKE` - Call `__cleanup()` method
- `OP_CLOSE_UPVALUE` - Handle captured resources
- `OP_POP` - Stack management

### Phase 4: Serialization

**File:** `src/serialise.c`

**Changes:** None required!

No new opcodes means no serialization changes needed. The compiled bytecode uses existing instructions.

### Phase 5: WASM Generation

**Files:** `src/wasm_generator.c`, `src/wat_generator.c`

**Changes:** None required immediately.

Since `with` compiles to existing opcodes, WASM generation works automatically. Future optimization: recognize with pattern for native WASM resource management.

---

## Examples

### Example 1: File Management

```javascript
// Define a file class with cleanup
class File {
    init(path) {
        this.path = path
        this.handle = native_open(path)
    }

    read() {
        return native_read(this.handle)
    }

    __cleanup() {
        print("Closing file: " + this.path)
        native_close(this.handle)
    }
}

// Use with statement
with file = File("data.txt") {
    var content = file.read()
    print(content)
}
// file.__cleanup() called automatically here
print("File closed")
```

**Output:**
```
<file contents>
Closing file: data.txt
File closed
```

### Example 2: Nested Resources

```javascript
class Transaction {
    init(db) {
        this.db = db
        this.db.begin_transaction()
    }

    commit() {
        this.db.commit()
        this.committed = true
    }

    __cleanup() {
        if (!this.committed) {
            print("Rolling back transaction")
            this.db.rollback()
        }
    }
}

with db = Database() {
    with tx = Transaction(db) {
        db.insert("users", {"name": "Alice"})
        db.insert("users", {"name": "Bob"})
        tx.commit()
    }
    // Transaction cleaned up here
}
// Database cleaned up here
```

### Example 3: Lock Management

```javascript
class Lock {
    init(name) {
        this.name = name
        print("Acquiring lock: " + name)
    }

    __cleanup() {
        print("Releasing lock: " + this.name)
    }
}

fun critical_section() {
    with lock = Lock("data_lock") {
        print("In critical section")
        // Do work...
    }
    // Lock released before return
}

critical_section()
```

**Output:**
```
Acquiring lock: data_lock
In critical section
Releasing lock: data_lock
```

### Example 4: Early Return

```javascript
fun process_file(path) {
    with file = File(path) {
        var line = file.read_line()
        if (line == nil) {
            return "Empty file"  // Cleanup still happens!
        }
        return line
    }
}

print(process_file("test.txt"))
// File closed even with early return
```

### Example 5: With Statement in Closures

```javascript
fun make_reader(path) {
    with file = File(path) {
        return () {
            return file.read()  // Closure captures file!
        }
    }
    // Cleanup happens here, but file is captured by closure
}

var reader = make_reader("data.txt")
print(reader())  // Works! Upvalue mechanism handles it
```

**Note:** When closure captures resource, upvalue mechanism promotes it to heap. Cleanup still runs at scope exit, but captured value survives.

---

## Edge Cases and Considerations

### 1. Missing `__cleanup()` Method

**Scenario:** Resource doesn't implement `__cleanup()`

**Options:**
- **A. Runtime error**: Throw error if method missing
- **B. Silent no-op**: Skip cleanup if method doesn't exist
- **C. Warning**: Print warning but continue

**Recommendation:** Option B (silent no-op) for flexibility. Allows with to work with any value.

**Implementation:**
Check if method exists before calling:
```c
// Pseudo-code in compiler or runtime
if (has_method(resource, "__cleanup")) {
    call_method(resource, "__cleanup");
}
```

### 2. Cleanup Throws Error

**Scenario:** `__cleanup()` throws runtime error

**Behavior:** Error propagates normally (consistent with defer behavior)

Example:
```javascript
class BrokenResource {
    __cleanup() {
        error("Cleanup failed!")
    }
}

with x = BrokenResource() {
    print("using")
}
// Runtime error: Cleanup failed!
```

### 3. Nested With with Same Variable Name

**Scenario:**
```javascript
with x = Resource1() {
    with x = Resource2() {
        // Which x?
    }
}
```

**Behavior:** Inner `x` shadows outer `x` (standard scoping rules)
- Inner block sees Resource2
- Outer block sees Resource1
- Each cleaned up at respective scope exit

### 4. Break/Continue in With Statement

**Scenario:**
```javascript
for (var i = 0; i < 10; i++) {
    with file = File("log.txt") {
        if (i == 5) break
        file.write(i)
    }
}
```

**Behavior:** Cleanup happens before break/continue (consistent with scope exit)
- Each loop iteration: acquire → use → cleanup
- Break exits loop after cleanup

### 5. Return in With Statement

See Example 4 - cleanup happens before return.

**Implementation detail:** Return handling in `_end_scope()` ensures proper cleanup order.

### 6. Resource Captured by Closure

See Example 5 - upvalue mechanism handles it.

**Key insight:** Cleanup runs at scope exit regardless of capture. If captured, upvalue has already promoted value to heap.

### 7. Nil or Non-Object Resources

**Scenario:**
```javascript
with x = nil {
    // ...
}
// Try to call nil.__cleanup() ?
```

**Recommendation:** Skip cleanup for nil/non-objects (silent no-op).

### 8. Multiple Resources in One Statement?

**Current design:** One resource per with statement.

**Future enhancement:** Multiple bindings?
```javascript
with (a = res1(), b = res2()) {
    // Use both
}
// Both cleaned up
```

**Recommendation:** Start with single resource, add multiple later if needed.

---

## Testing Strategy

### Unit Tests (`src/test/`)

**New test file:** `src/test/with_test.c`

Test cases:
1. Basic with statement compilation
2. Nested with statements
3. With statement in loop
4. With statement with return
5. Closure capturing with variable
6. Missing `__cleanup()` method (error handling)
7. Cleanup execution order

### Integration Tests (`src/test/scripts/`)

**New test file:** `src/test/scripts/with.sox`

**Test scenarios:**

```javascript
// Test 1: Basic cleanup
print("=== Test 1: Basic cleanup ===")
class Resource {
    init(name) { this.name = name }
    __cleanup() { print("Cleanup: " + this.name) }
}

with r = Resource("test1") {
    print("Using: " + r.name)
}
print("Done")

// Expected output:
// Using: test1
// Cleanup: test1
// Done

// Test 2: Nested with
print("=== Test 2: Nested with ===")
with a = Resource("outer") {
    with b = Resource("inner") {
        print("Both active")
    }
    print("Inner cleaned up")
}
print("Both cleaned up")

// Expected output:
// Both active
// Cleanup: inner
// Inner cleaned up
// Cleanup: outer
// Both cleaned up

// Test 3: Early return
print("=== Test 3: Early return ===")
fun test_return() {
    with r = Resource("return_test") {
        print("Before return")
        return "result"
    }
}
print(test_return())

// Expected output:
// Before return
// Cleanup: return_test
// result

// Test 4: Break in loop
print("=== Test 4: Break in loop ===")
for (var i = 0; i < 3; i++) {
    with r = Resource("iter" + i) {
        print("Iteration: " + i)
        if (i == 1) break
    }
}
print("Loop done")

// Expected output:
// Iteration: 0
// Cleanup: iter0
// Iteration: 1
// Cleanup: iter1
// Loop done

// Test 5: Closure capture
print("=== Test 5: Closure capture ===")
var closure
with r = Resource("captured") {
    closure = () { print("Closure: " + r.name) }
}
// r cleaned up here, but closure still works (upvalue)
closure()

// Expected output:
// Cleanup: captured
// Closure: captured

// Test 6: No cleanup method
print("=== Test 6: No cleanup method ===")
with x = 42 {
    print("Value: " + x)
}
print("Done")

// Expected output:
// Value: 42
// Done
```

**Expected output file:** `src/test/scripts/with.sox.out`

### Performance Tests

**Benchmark:** Compare with manual cleanup and defer:

```javascript
// Manual cleanup
var start = clock()
for (var i = 0; i < 10000; i++) {
    var r = Resource()
    use(r)
    r.__cleanup()
}
print("Manual: " + (clock() - start))

// With statement
start = clock()
for (var i = 0; i < 10000; i++) {
    with r = Resource() {
        use(r)
    }
}
print("With: " + (clock() - start))

// Defer
start = clock()
for (var i = 0; i < 10000; i++) {
    var r = Resource()
    defer () { r.__cleanup() }
    use(r)
}
print("Defer: " + (clock() - start))
```

**Expected:** `with` performance should be comparable to manual cleanup (no overhead).

### Serialization Tests

Verify with statement bytecode serializes correctly:

```c
// In src/test/serialise_test.c
MunitResult test_with_statement_serialization(const MunitParameter params[], void* fixture) {
    // Compile with statement
    // Serialize bytecode
    // Deserialize and verify
    // Execute and verify output
}
```

---

## Implementation Plan

### Phase 1: Foundation (Week 1)

**Tasks:**
1. Add `TOKEN_WITH` to scanner ✓
2. Implement basic `_with_statement()` compiler function ✓
3. Write unit tests for compilation ✓
4. Update documentation (this proposal) ✓

**Deliverables:**
- `with` statement parses successfully
- Compiles to bytecode (no execution yet)
- Unit tests pass

### Phase 2: Execution (Week 1-2)

**Tasks:**
1. Implement cleanup code generation ✓
2. Add `__cleanup()` method calling ✓
3. Test basic with statement execution ✓
4. Add integration test (`with.sox`) ✓

**Deliverables:**
- Basic with statement works end-to-end
- Integration tests pass
- Cleanup happens at scope exit

### Phase 3: Advanced Features (Week 2)

**Tasks:**
1. Test nested with statements ✓
2. Test with + break/continue/return ✓
3. Test closure capture ✓
4. Handle edge cases (nil, missing cleanup) ✓

**Deliverables:**
- All edge cases handled
- Comprehensive test coverage
- Documentation updated

### Phase 4: Optimization and Polish (Week 3)

**Tasks:**
1. Performance benchmarking ✓
2. Error message improvement ✓
3. Serialization testing ✓
4. WASM generation testing ✓
5. Update README with examples ✓

**Deliverables:**
- Performance acceptable
- All tests pass
- Documentation complete
- Feature ready for merge

### Phase 5: Optional Enhancements (Future)

**Tasks:**
- Multiple resources per with statement
- Explicit cleanup expressions (Option B)
- WASM optimization for with pattern
- Standard library resources (File, Lock, etc.)

---

## Alternatives Considered

### Alternative 1: Try-Finally

```javascript
try {
    var file = open("data.txt")
    use(file)
} finally {
    file.close()
}
```

**Pros:**
- Familiar from Java/Python/JavaScript
- Explicit cleanup location
- Works with exceptions (if added to Sox)

**Cons:**
- More verbose than with
- Requires exception handling (not in Sox yet)
- Cleanup must be written manually

**Decision:** Defer this until exception handling is added. `with` is simpler for current Sox.

### Alternative 2: RAII via Destructors

```javascript
class File {
    destructor() {  // Called when GC collects
        this.close()
    }
}
```

**Pros:**
- Automatic cleanup
- No explicit syntax needed

**Cons:**
- Depends on GC implementation (currently incomplete)
- Non-deterministic cleanup timing
- Doesn't work for resources that need prompt cleanup

**Decision:** GC-based cleanup is complementary, not a replacement for `with`.

### Alternative 3: Do Nothing

Keep status quo with manual cleanup or defer.

**Cons:**
- Boilerplate for common pattern
- Easy to forget cleanup
- Less ergonomic than other languages

**Decision:** Adding `with` improves developer experience significantly.

---

## Open Questions

### 1. Syntax: Parentheses Required?

**Option A:** `with (x = expr) { ... }` (requires parens)
**Option B:** `with x = expr { ... }` (no parens)

**Current choice:** Option A (parens required) for consistency with `if`, `while`, `for`.

**Alternative:** Make parens optional like Python.

### 2. Cleanup Method Name

**Options:**
- `__cleanup()` (current choice) - Python-like
- `close()` - Simple, common convention
- `dispose()` - C# convention
- `destroy()` - Alternative

**Current choice:** `__cleanup()` for consistency with Sox's magic method pattern (like `__init__`).

### 3. Error Handling in Cleanup

If cleanup throws error, should we:
- **A.** Propagate error (current choice)
- **B.** Catch and log, continue
- **C.** Abort execution

**Current choice:** A (propagate) for consistency with defer.

### 4. Multiple Resources

Should we support:
```javascript
with (a = expr1, b = expr2) { ... }
```

**Current decision:** Not in MVP. Add if user feedback requests it.

### 5. Assignment vs Declaration

Should `with` declare a new variable or allow assignment to existing?

**Current:** Declares new variable (scoped to block)

**Alternative:** Allow `with existing_var = expr`?

**Decision:** Declaration only (simpler, clearer scoping).

---

## Conclusion

The `with` statement is a natural fit for Sox's architecture:
- Builds on existing scoping mechanism
- Uses existing bytecode instructions
- Minimal implementation complexity (~100 lines)
- Significant ergonomic improvement

**Recommendation:** Proceed with implementation following the plan outlined above.

The feature aligns with Sox's goals:
- Simple, clear syntax
- Consistent with existing language features
- Practical for real-world resource management

**Next Steps:**
1. Review and approve this proposal
2. Implement Phase 1 (scanner + basic compiler)
3. Iterate based on testing and feedback
4. Add to README as supported feature

---

## References

**Inspiration from other languages:**
- **Python:** `with` statement and context managers (`__enter__`/`__exit__`)
- **C#:** `using` statement and IDisposable interface
- **Go:** `defer` statement (already in Sox!)
- **Java:** try-with-resources
- **Zig:** `errdefer` for error-path cleanup

**Sox documentation:**
- CLAUDE.md - Project overview and architecture
- README.md - Feature list and examples
- docs/wasm.md - WASM implementation status

**Related code:**
- `src/compiler.c:_defer_declaration()` - Existing defer mechanism
- `src/compiler.c:_for_statement()` - Example of scoped variable binding
- `src/compiler.c:_end_scope()` - Scope cleanup logic
- `src/vm.c:OP_CLOSE_UPVALUE` - Upvalue closure mechanism
