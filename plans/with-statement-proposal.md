# Proposal: Add 'with' Statement Support to Sox

**Author:** Claude
**Date:** 2025-12-25
**Status:** Draft (Updated for VB-style syntax)
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

This proposal introduces a `with` statement to Sox, providing **implicit member access** and **automatic resource cleanup** through scoped variable binding. Inspired by Visual Basic's `With` statement and Python's context managers, Sox's `with` combines the best of both: syntactic convenience and guaranteed resource management.

**Key Features:**
1. **Implicit member access**: Use `.method()` or `.property` as shorthand for `resource.method()` / `resource.property`
2. **Automatic cleanup**: Resources automatically cleaned up when exiting scope
3. **Reduced repetition**: Eliminate redundant object references
4. **Guaranteed safety**: Cleanup happens even with early returns or breaks

**Example:**
```javascript
with file = open("data.txt") {
    .write("Hello ")      // Instead of: file.write("Hello ")
    .write("World")       // Instead of: file.write("World")
    var content = .read() // Instead of: file.read()
}
// file.__cleanup() called automatically
```

---

## Motivation

### Current State: Repetitive and Error-Prone

Currently, Sox requires:
1. **Repeating object names** when calling multiple methods
2. **Manual resource cleanup**

```javascript
var file = open("data.txt")
file.write("Line 1")
file.write("Line 2")
file.write("Line 3")
var content = file.read()
file.close()  // Easy to forget!
```

**Problems:**
- **Verbose**: `file.` repeated 5 times
- **Error-prone**: Easy to forget cleanup
- **Tedious**: More typing for common patterns
- **Unclear intent**: Cleanup responsibility not obvious

### Proposed Solution: VB-Style With Statement

```javascript
with file = open("data.txt") {
    .write("Line 1")      // Implicit: file.write()
    .write("Line 2")
    .write("Line 3")
    var content = .read()
}
// Automatic cleanup!
```

**Advantages:**
- ✅ **Concise**: No repetition of `file.`
- ✅ **Safe**: Automatic cleanup guaranteed
- ✅ **Clear**: Intent is obvious
- ✅ **Flexible**: Can still use `file.` explicitly if needed
- ✅ **Familiar**: Matches Visual Basic, reducing learning curve

### Comparison with Visual Basic

**Visual Basic:**
```vb
With myObject
    .Property1 = value1
    .Method1()
    .Property2 = value2
End With
```

**Sox (proposed):**
```javascript
with myObject = getObject() {
    .property1 = value1
    .method1()
    .property2 = value2
}
// PLUS: automatic cleanup via myObject.__cleanup()
```

Sox's `with` is **more powerful** than VB's because it adds automatic resource management!

---

## Syntax and Semantics

### Basic Syntax

```
with <identifier> = <expression> {
    <statements>
}
```

**Within the block:**
- Leading `.` before identifier automatically resolves to `<identifier>.`
- Example: `.method()` → `identifier.method()`
- Example: `.property` → `identifier.property`
- Example: `.property = value` → `identifier.property = value`

**Grammar (EBNF):**
```ebnf
withStatement  → "with" "(" identifier "=" expression ")" block ;
block          → "{" declaration* "}" ;

// Modified expression rules within with block:
call           → primary ( "(" arguments? ")" | "." identifier )* ;
primary        → "." identifier  // NEW: implicit receiver
               | identifier
               | NUMBER | STRING | "true" | "false" | "nil"
               | "(" expression ")"
               // ... rest of primary expressions
               ;
```

### Semantics

**Execution flow:**

1. **Acquisition**: Evaluate `<expression>` and bind result to `<identifier>`
2. **Context binding**: Set `<identifier>` as the implicit receiver for the block
3. **Scope**: `<identifier>` is scoped to the with block
4. **Implicit access**: Within block, `.member` resolves to `<identifier>.member`
5. **Explicit access**: Can still use `<identifier>.member` explicitly
6. **Cleanup**: On scope exit, call `<identifier>.__cleanup()` if it exists
7. **Scope cleanup**: Pop `<identifier>` from stack

### Implicit Member Resolution

**Rule:** A leading `.` in an expression is resolved to the current `with` context variable.

**Examples:**

```javascript
with obj = MyObject() {
    .method()           // → obj.method()
    var x = .property   // → var x = obj.property
    .property = 5       // → obj.property = 5
    .chain().call()     // → obj.chain().call()

    // Can still use explicit reference
    print(obj.name)     // Works fine

    // Nested member access
    .nested.deep.call() // → obj.nested.deep.call()
}
```

**Important:** The `.` must be at the start of the expression (not after other operations):

```javascript
with obj = MyObject() {
    .method()       // ✓ Valid: implicit receiver
    var x = .prop   // ✓ Valid: implicit receiver

    print(.value)   // ✓ Valid: implicit receiver in argument

    1 + .value      // ✓ Valid: implicit receiver in expression

    obj.method()    // ✓ Valid: explicit receiver still works
}
```

### Cleanup Protocol

On scope exit (normal, return, break, continue), the `with` statement automatically calls the cleanup method:

**Cleanup method:** `__cleanup()` (magic method convention)

```javascript
class File {
    init(path) {
        this.path = path
        this.handle = native_open(path)
    }

    write(text) { /* ... */ }
    read() { /* ... */ }

    __cleanup() {
        print("Closing: " + this.path)
        native_close(this.handle)
    }
}

with file = File("data.txt") {
    .write("content")
}
// file.__cleanup() called automatically here
```

**If `__cleanup()` doesn't exist:** Silent no-op (allows `with` to work with any value).

### Nested With Statements

Multiple `with` statements nest naturally:

```javascript
with a = ResourceA() {
    .setup()              // → a.setup()

    with b = ResourceB() {
        .config()         // → b.config()
        .attach(a)        // → b.attach(a)
        a.start()         // Explicit: still works
    }
    // b cleaned up here

    .teardown()           // → a.teardown()
}
// a cleaned up here
```

**Scoping rules:**
- Inner `with` takes precedence for `.member` syntax
- Outer resources still accessible by explicit name
- Cleanup happens in reverse order (LIFO)

### Execution Order

```javascript
print("before")
with resource = acquire() {
    print("using")
    .method()
}
print("after")
```

**Execution:**
1. Print "before"
2. Call `acquire()` → bind to `resource`
3. Print "using"
4. Call `resource.method()`
5. Call `resource.__cleanup()` (if exists)
6. Pop `resource` from stack
7. Print "after"

---

## Design Rationale

### Why Implicit Member Access?

**Reduces repetition** in common patterns:

```javascript
// WITHOUT with
var config = getConfig()
config.setHost("localhost")
config.setPort(8080)
config.setTimeout(30)
config.enableSSL(true)
config.save()

// WITH with
with config = getConfig() {
    .setHost("localhost")
    .setPort(8080)
    .setTimeout(30)
    .enableSSL(true)
    .save()
}
```

**Benefits:**
- Less typing (15 fewer characters in example above)
- More readable (focus on operations, not object)
- Less error-prone (can't typo the object name mid-chain)
- Clearer intent (all operations on same object)

### Why Leading Dot Syntax?

**Considered alternatives:**

**Option A: Leading dot** (chosen)
```javascript
with obj = foo() {
    .method()  // Clear: obviously referring to something
}
```

**Option B: Bare identifier** (ambiguous)
```javascript
with obj = foo() {
    method()   // Unclear: local function? method on obj?
}
```

**Option C: Special keyword**
```javascript
with obj = foo() {
    this.method()  // Confusing: conflicts with class 'this'
}
```

**Decision:** Leading dot is:
- Visually distinctive
- No ambiguity with local variables
- Familiar from VB
- No keyword conflicts

### Why Automatic Cleanup?

Combines VB's ergonomics with Python's safety:

**VB:** Great syntax, no cleanup
**Python:** Cleanup guaranteed, verbose syntax
**Sox:** Best of both!

```javascript
// Compare with Python
with open("data.txt") as file:  # Verbose: need 'as'
    file.write("data")          # Must use 'file.' prefix
    file.read()                 # Repetitive

// Sox (proposed)
with file = open("data.txt") {  # Clean syntax
    .write("data")              # Implicit receiver
    .read()                     # Concise
}
```

### Why `__cleanup()` Method?

Following Sox's magic method convention:
- `__init()__` - Constructor
- `__str__()` - String conversion
- `__cleanup()` - Resource cleanup (new)

Provides a **standard protocol** for resource types without requiring special language support for destructors.

---

## Implementation Strategy

### Overview

The implementation requires:
1. **Scanner**: Add `TOKEN_WITH` keyword
2. **Compiler**:
   - Track `with` context variable during compilation
   - Transform `.member` to `context.member` in expression parsing
   - Generate cleanup code on scope exit
3. **VM**: No changes (uses existing opcodes)

**Key insight:** The implicit member access is a **compile-time transformation**. The VM sees normal property access bytecode.

### Phase 1: Scanner (Lexical Analysis)

**Files:** `src/scanner.h`, `src/scanner.c`

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

**Note:** The scanner already handles `TOKEN_DOT` for `.` - no changes needed there.

**Estimated effort:** ~10 lines of code

### Phase 2: Compiler - Context Tracking

**File:** `src/compiler.c`

**Add context tracking to compiler state:**

```c
// In compiler_t struct
typedef struct compiler_t {
    // ... existing fields ...

    // NEW: Track with context for implicit member access
    struct {
        bool active;           // Is there an active with context?
        int local_index;       // Local variable index of with resource
        int scope_depth;       // Scope depth where with was declared
    } with_context;

    // ... rest of fields ...
} compiler_t;
```

**Helper functions:**

```c
// Enter with context (called by _with_statement)
static void _enter_with_context(int local_index) {
    _current->with_context.active = true;
    _current->with_context.local_index = local_index;
    _current->with_context.scope_depth = _current->scope_depth;
}

// Exit with context (called when scope ends)
static void _exit_with_context() {
    if (_current->with_context.active &&
        _current->with_context.scope_depth >= _current->scope_depth) {
        _current->with_context.active = false;
    }
}

// Check if we're in a with context
static bool _in_with_context() {
    return _current->with_context.active;
}

// Get with context variable index
static int _get_with_context_var() {
    return _current->with_context.local_index;
}
```

### Phase 3: Compiler - Expression Parsing

**Modify `_primary()` to handle leading dot:**

```c
// In _primary() function (around line 745 in compiler.c)
static void _primary() {
    if (_match(TOKEN_FALSE)) {
        _emit_byte(OP_FALSE);
    } else if (_match(TOKEN_TRUE)) {
        _emit_byte(OP_TRUE);
    } else if (_match(TOKEN_NIL)) {
        _emit_byte(OP_NIL);
    // ... other cases ...

    // NEW: Handle leading dot (implicit with context)
    } else if (_match(TOKEN_DOT)) {
        if (!_in_with_context()) {
            _error("Cannot use '.' outside of 'with' statement.");
            return;
        }

        // Consume the property/method name
        _consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
        uint8_t name = _identifier_constant(&parser.previous);

        // Emit code to get with context variable
        _emit_bytes(OP_GET_LOCAL, (uint8_t)_get_with_context_var());

        // Emit property get
        _emit_bytes(OP_GET_PROPERTY, name);

    } else if (_match(TOKEN_IDENTIFIER)) {
        // ... existing identifier handling ...
    }
    // ... rest of primary() ...
}
```

**Handle assignment to implicit receiver:**

Need to modify `_named_variable()` to detect patterns like `.property = value`:

```c
// This is more complex - need to track if we just parsed a leading dot
// Might require refactoring assignment parsing to check for:
// - Is next token a DOT?
// - If so, is it followed by IDENTIFIER then EQUAL?
// - If yes, emit SET_PROPERTY instead of SET_LOCAL

// Pseudo-code for assignment detection:
static void _assignment() {
    if (_check(TOKEN_DOT) && _in_with_context()) {
        _advance(); // consume DOT
        _consume(TOKEN_IDENTIFIER, "Expect property name.");
        uint8_t name = _identifier_constant(&parser.previous);

        _consume(TOKEN_EQUAL, "Expect '=' in assignment.");
        _expression(); // RHS

        // Emit: GET with_var, SWAP, SET_PROPERTY
        _emit_bytes(OP_GET_LOCAL, _get_with_context_var());
        // ... emit set property logic
    } else {
        // Normal assignment
    }
}
```

**Note:** This requires careful integration with the Pratt parser. May need to add `.` as an infix operator with special handling.

### Phase 4: Compiler - With Statement

**Implement `_with_statement()`:**

```c
static void _with_statement() {
    // Begin new scope for resource variable
    _begin_scope();

    // Parse: with ( <name> = <expr> )
    _consume(TOKEN_LEFT_PAREN, "Expect '(' after 'with'.");

    // Parse variable name
    _consume(TOKEN_IDENTIFIER, "Expect variable name.");
    uint8_t name_var = _parse_variable("Expect variable name in with statement.");

    _consume(TOKEN_EQUAL, "Expect '=' after variable name.");

    // Parse and emit resource acquisition expression
    _expression();

    // Define the variable (now initialized)
    _define_variable(name_var);
    _mark_initialized();

    _consume(TOKEN_RIGHT_PAREN, "Expect ')' after with expression.");

    // Enter with context for implicit member access
    int resource_local = _current->local_count - 1;
    _enter_with_context(resource_local);

    // Parse and execute body block
    _consume(TOKEN_LEFT_BRACE, "Expect '{' before with body.");
    _block();
    _consume(TOKEN_RIGHT_BRACE, "Expect '}' after with body.");

    // Emit cleanup code before scope ends
    // Check if resource has __cleanup method and call it

    // Get resource variable
    _emit_bytes(OP_GET_LOCAL, (uint8_t)resource_local);

    // Try to get __cleanup property (will be nil if doesn't exist)
    _emit_constant(OBJ_VAL(l_copy_string("__cleanup", 9)));
    _emit_bytes(OP_GET_PROPERTY, _make_constant(...));

    // Check if it's callable (not nil)
    // This requires a new approach - maybe OP_INVOKE_IF_EXISTS?
    // Or use runtime check:
    // - Duplicate value
    // - Check if nil
    // - If not nil, call it
    // - Pop result

    // Simpler approach: Just try to invoke, catch error
    _emit_bytes(OP_INVOKE, 0);  // Call __cleanup() with 0 args
    _emit_byte(OP_POP);  // Pop return value

    // Exit with context
    _exit_with_context();

    // End scope (pops resource variable)
    _end_scope();
}
```

**Alternative cleanup approach:**

Generate an `OP_INVOKE_OPTIONAL` opcode that doesn't error if method doesn't exist:

```c
// In VM, new opcode:
case OP_INVOKE_OPTIONAL: {
    obj_string_t* method = READ_STRING();
    int arg_count = READ_BYTE();
    if (!_invoke(l_peek(arg_count), method, arg_count)) {
        // Method doesn't exist - just pop the receiver
        // Don't treat as error
    }
    break;
}
```

But this requires a new opcode. Simpler: do runtime check.

**Better approach using existing opcodes:**

```c
// Emit cleanup sequence:
// 1. Get resource
_emit_bytes(OP_GET_LOCAL, (uint8_t)resource_local);

// 2. Duplicate it (for method call)
_emit_byte(OP_DUP);  // May need to add this opcode

// 3. Get __cleanup property
_emit_bytes(OP_GET_PROPERTY, _make_constant(l_copy_string("__cleanup", 9)));

// 4. Check if nil
_emit_byte(OP_DUP);
_emit_byte(OP_NIL);
_emit_byte(OP_EQUAL);

// 5. Jump if nil (skip call)
int skip_jump = _emit_jump(OP_JUMP_IF_TRUE);
_emit_byte(OP_POP); // Pop comparison result

// 6. Call cleanup
_emit_bytes(OP_CALL, 0);
_emit_byte(OP_POP); // Pop return value

// 7. Patch skip jump
_patch_jump(skip_jump);
_emit_byte(OP_POP); // Pop comparison result (or nil)
```

**Simplest approach:** Just try to call, handle error in VM gracefully.

### Phase 5: VM Execution

**File:** `src/vm.c`

**No new opcodes required** if we use the "try to call, handle gracefully" approach.

**Optional enhancement:** Add `OP_DUP` opcode for duplicating top stack value:

```c
// In chunk.h OpCode enum:
typedef enum {
    // ... existing opcodes ...
    OP_DUP,  // NEW: Duplicate top of stack
    // ...
} OpCode;

// In vm.c run():
case OP_DUP:
    l_push(l_peek(0));
    break;
```

This makes the cleanup sequence cleaner.

### Phase 6: Error Handling

**Error cases to handle:**

1. **Leading `.` outside with block:**
   ```javascript
   .method()  // Error: Cannot use '.' outside 'with' statement
   ```

   Handle in `_primary()` - already shown above.

2. **Cleanup method throws error:**
   ```javascript
   with x = BadResource() {
       // ...
   }
   // If __cleanup() throws, error propagates normally
   ```

   No special handling needed - errors propagate as usual.

3. **Resource is nil:**
   ```javascript
   with x = nil {
       .method()  // Runtime error: nil has no properties
   }
   ```

   Runtime error occurs naturally (nil property access).

### Implementation Complexity

**Estimated code changes:**

| Component | Lines of Code | Complexity |
|-----------|---------------|------------|
| Scanner (add TOKEN_WITH) | ~10 | Trivial |
| Compiler (context tracking) | ~40 | Low |
| Compiler (_primary for dot) | ~30 | Medium |
| Compiler (assignment with dot) | ~50 | Medium |
| Compiler (_with_statement) | ~80 | Medium |
| VM (OP_DUP if added) | ~5 | Trivial |
| **Total** | **~215** | **Medium** |

**Key challenges:**

1. **Integrating dot syntax with Pratt parser** - requires careful handling of precedence
2. **Assignment to `.property`** - need to detect and transform `{ .x = 5 }`
3. **Cleanup code generation** - need robust "call if exists" pattern

---

## Examples

### Example 1: File Operations with Implicit Access

```javascript
class File {
    init(path) {
        this.path = path
        this.handle = native_open(path)
    }

    write(text) {
        native_write(this.handle, text)
    }

    read() {
        return native_read(this.handle)
    }

    __cleanup() {
        print("Closing: " + this.path)
        native_close(this.handle)
    }
}

// Using with statement
with file = File("data.txt") {
    .write("Line 1\n")       // file.write()
    .write("Line 2\n")       // file.write()
    .write("Line 3\n")       // file.write()
    var content = .read()    // file.read()
    print(content)
}
// Automatic: file.__cleanup() called here

print("File closed")
```

**Output:**
```
Line 1
Line 2
Line 3
Closing: data.txt
File closed
```

### Example 2: Configuration Builder

```javascript
class Config {
    init() {
        this.host = "localhost"
        this.port = 80
        this.ssl = false
    }

    setHost(h) { this.host = h }
    setPort(p) { this.port = p }
    enableSSL(enabled) { this.ssl = enabled }

    build() {
        return "Config: " + this.host + ":" + this.port +
               (this.ssl ? " (SSL)" : "")
    }
}

// Concise configuration
with config = Config() {
    .setHost("example.com")
    .setPort(443)
    .enableSSL(true)
    print(.build())
}
```

**Output:**
```
Config: example.com:443 (SSL)
```

**Compare with non-with version:**
```javascript
// Much more repetitive!
var config = Config()
config.setHost("example.com")
config.setPort(443)
config.enableSSL(true)
print(config.build())
```

### Example 3: Nested With Statements

```javascript
class Database {
    init(name) { this.name = name }
    beginTransaction() { return Transaction(this) }
    commit() { print("Committed to " + this.name) }
    __cleanup() { print("Closing DB: " + this.name) }
}

class Transaction {
    init(db) {
        this.db = db
        this.committed = false
    }

    insert(data) { print("Insert: " + data) }

    commit() {
        this.committed = true
        this.db.commit()
    }

    __cleanup() {
        if (!this.committed) {
            print("Rolling back transaction")
        }
    }
}

with db = Database("users") {
    with tx = .beginTransaction() {  // db.beginTransaction()
        .insert("Alice")              // tx.insert()
        .insert("Bob")                // tx.insert()
        .commit()                     // tx.commit()
    }
    // tx cleaned up here
}
// db cleaned up here
```

**Output:**
```
Insert: Alice
Insert: Bob
Committed to users
Closing DB: users
```

### Example 4: Graphics Context (VB-style use case)

```javascript
class Canvas {
    init(width, height) {
        this.width = width
        this.height = height
        this.color = "black"
    }

    setColor(c) { this.color = c }
    drawRect(x, y, w, h) {
        print("Draw " + this.color + " rect at (" + x + "," + y + ")")
    }
    drawCircle(x, y, r) {
        print("Draw " + this.color + " circle at (" + x + "," + y + ")")
    }
    fill() { print("Fill with " + this.color) }
}

with canvas = Canvas(800, 600) {
    .setColor("red")
    .drawRect(0, 0, 100, 100)
    .drawCircle(200, 200, 50)

    .setColor("blue")
    .drawRect(300, 300, 80, 80)
    .fill()
}
```

**Output:**
```
Draw red rect at (0,0)
Draw red circle at (200,200)
Draw blue rect at (300,300)
Fill with blue
```

### Example 5: Early Return with Cleanup

```javascript
class LogFile {
    init(path) {
        this.path = path
    }

    readFirstLine() {
        return "First line content"
    }

    __cleanup() {
        print("Closing log: " + this.path)
    }
}

fun processLog(path) {
    with log = LogFile(path) {
        var line = .readFirstLine()
        if (line == nil) {
            return "Empty"  // Cleanup still happens!
        }
        return line
    }
}

print(processLog("app.log"))
// Cleanup happens before return
```

**Output:**
```
Closing log: app.log
First line content
```

### Example 6: Mixing Explicit and Implicit Access

```javascript
class Counter {
    init(name) {
        this.name = name
        this.count = 0
    }

    increment() { this.count++ }
    get() { return this.count }
}

with counter = Counter("requests") {
    .increment()         // Implicit
    .increment()         // Implicit
    print(counter.get()) // Explicit (both work!)
    print(.get())        // Implicit
}
```

**Output:**
```
2
2
```

### Example 7: No Cleanup Method (Still Works)

```javascript
class Point {
    init(x, y) {
        this.x = x
        this.y = y
    }

    distance() {
        return (this.x * this.x + this.y * this.y) ^ 0.5
    }
}

// Point has no __cleanup() - that's fine!
with p = Point(3, 4) {
    print(.distance())
}
// No cleanup called (silent no-op)
```

**Output:**
```
5
```

---

## Edge Cases and Considerations

### 1. Leading Dot Outside With Block

**Scenario:**
```javascript
.method()  // Error!
```

**Behavior:** Compile error: "Cannot use '.' outside of 'with' statement."

**Implementation:** Check `_in_with_context()` in `_primary()` when seeing `TOKEN_DOT`.

### 2. Nested With - Dot Resolution

**Scenario:**
```javascript
with a = ObjectA() {
    with b = ObjectB() {
        .method()  // Which object?
    }
}
```

**Behavior:** Inner `with` takes precedence - `.method()` calls `b.method()`.

**Accessing outer:**
```javascript
with a = ObjectA() {
    with b = ObjectB() {
        .method()    // b.method()
        a.method()   // a.method() - explicit access
    }
}
```

**Implementation:** `_enter_with_context()` overwrites context. Could enhance to support stack of contexts later.

### 3. Assignment to Implicit Property

**Scenario:**
```javascript
with obj = MyObject() {
    .property = 42  // obj.property = 42
}
```

**Behavior:** Should work naturally. Property assignment on implicit receiver.

**Implementation challenge:** Need to detect `.property =` pattern in parser and emit `SET_PROPERTY` instead of trying to call `.property` first.

**Approach:**
```c
// In assignment detection:
if (peek TOKEN_DOT && peek+1 TOKEN_IDENTIFIER && peek+2 TOKEN_EQUAL) {
    // This is property assignment
    _emit_property_set();
} else {
    // Regular expression
}
```

### 4. Method Chaining with Implicit Receiver

**Scenario:**
```javascript
with builder = Builder() {
    .setA(1).setB(2).setC(3).build()
}
```

**Behavior:** Should work! First `.setA(1)` resolves to `builder.setA(1)`, which returns an object that you can chain `.setB(2)` on.

**Note:** This is a **different** object being chained, not the with context. This is correct behavior.

### 5. Passing Implicit Receiver to Function

**Scenario:**
```javascript
fun process(obj) {
    print(obj.name)
}

with resource = MyResource() {
    process(.)  // Just the dot?
}
```

**Problem:** How to reference "just the with variable" using dot syntax?

**Solution:** Don't support bare `.` - require explicit reference:
```javascript
with resource = MyResource() {
    process(resource)  // Must use explicit name
}
```

Bare `.` without property/method is a syntax error.

### 6. Cleanup Method Missing

**Scenario:**
```javascript
with x = 42 {
    // Numbers don't have __cleanup()
}
```

**Behavior:** Silent no-op (no cleanup called).

**Implementation:** Check if `__cleanup` exists before calling:
```c
// Pseudo-code in cleanup generation:
if (resource_has_property("__cleanup")) {
    call_cleanup();
}
```

### 7. Cleanup Throws Error

**Scenario:**
```javascript
class BadResource {
    __cleanup() {
        error("Cleanup failed!")
    }
}

with x = BadResource() {
    print("using")
}
```

**Behavior:** Error propagates normally:
```
using
Runtime error: Cleanup failed!
```

### 8. Return/Break/Continue in With

**Scenario:**
```javascript
for (var i = 0; i < 10; i++) {
    with resource = acquire() {
        if (i == 5) break
    }
}
```

**Behavior:** Cleanup happens **before** break/return/continue.

**Implementation:** `_end_scope()` emits cleanup code which runs before control flow opcodes.

### 9. Closure Capturing With Variable

**Scenario:**
```javascript
fun makeGetter() {
    with obj = MyObject() {
        return () { return .property }  // Closure captures obj
    }
}

var getter = makeGetter()
// obj cleanup happened, but closure has upvalue
getter()  // Should work (upvalue promoted to heap)
```

**Behavior:** Works via upvalue mechanism. Cleanup runs but captured value survives on heap.

### 10. Dot at Start of Line (Ambiguity)

**Scenario:**
```javascript
with obj = MyObject() {
    var x = .property
    .method()  // Is this a continuation?
}
```

**Behavior:** No ambiguity - each `.` at expression start resolves to with context.

```javascript
var x = .property  // .property is complete expression
.method()          // New statement: call .method()
```

Sox requires explicit statement separators (newlines or semicolons), so no ambiguity.

### 11. With Variable Shadows Outer Variable

**Scenario:**
```javascript
var x = "outer"
with x = "inner" {
    print(.length)  // Wait, strings have .length?
}
print(x)  // "outer" (not affected)
```

**Behavior:** Standard shadowing - inner `x` shadows outer. After with block, outer `x` visible again.

---

## Testing Strategy

### Unit Tests (`src/test/`)

**New test file:** `src/test/with_test.c`

Test cases:
1. **Tokenization:** `TOKEN_WITH` recognized
2. **Compilation:** With statement compiles successfully
3. **Dot resolution:** Leading `.` generates correct bytecode
4. **Context tracking:** `_in_with_context()` works correctly
5. **Nested with:** Context switches properly
6. **Error handling:** Dot outside with produces error
7. **Cleanup generation:** `__cleanup()` call emitted

### Integration Tests (`src/test/scripts/`)

**New test file:** `src/test/scripts/with.sox`

```javascript
//
// Test 1: Basic implicit member access
//
print("=== Test 1: Basic implicit access ===")

class Resource {
    init(name) {
        this.name = name
        this.value = 0
    }

    increment() {
        this.value = this.value + 1
    }

    get() {
        return this.value
    }

    __cleanup() {
        print("Cleanup: " + this.name)
    }
}

with r = Resource("test1") {
    .increment()
    .increment()
    print("Value: " + .get())
}
print("Done")

// Expected output:
// Value: 2
// Cleanup: test1
// Done

//
// Test 2: Property access and assignment
//
print("=== Test 2: Properties ===")

class Point {
    init(x, y) {
        this.x = x
        this.y = y
    }
}

with p = Point(0, 0) {
    .x = 10
    .y = 20
    print("Point: (" + .x + ", " + .y + ")")
}

// Expected output:
// Point: (10, 20)

//
// Test 3: Nested with statements
//
print("=== Test 3: Nested with ===")

with a = Resource("outer") {
    print("Outer: " + .name)
    with b = Resource("inner") {
        print("Inner: " + .name)
        print("Outer explicit: " + a.name)
    }
    print("Back to outer: " + .name)
}

// Expected output:
// Outer: outer
// Inner: inner
// Outer explicit: outer
// Cleanup: inner
// Back to outer: outer
// Cleanup: outer

//
// Test 4: Early return
//
print("=== Test 4: Early return ===")

fun testReturn() {
    with r = Resource("return_test") {
        print("Before return")
        return .name
    }
}

print("Result: " + testReturn())

// Expected output:
// Before return
// Cleanup: return_test
// Result: return_test

//
// Test 5: Break in loop
//
print("=== Test 5: Break in loop ===")

for (var i = 0; i < 3; i++) {
    with r = Resource("iter") {
        print("Iteration: " + i)
        if (i == 1) break
    }
}

// Expected output:
// Iteration: 0
// Cleanup: iter
// Iteration: 1
// Cleanup: iter

//
// Test 6: No cleanup method
//
print("=== Test 6: No cleanup ===")

class NoCleanup {
    init(val) { this.val = val }
    get() { return this.val }
}

with x = NoCleanup(42) {
    print("Value: " + .get())
}
print("Done")

// Expected output:
// Value: 42
// Done

//
// Test 7: Method chaining
//
print("=== Test 7: Method chaining ===")

class Builder {
    init() {
        this.a = 0
        this.b = 0
    }

    setA(val) {
        this.a = val
        return this
    }

    setB(val) {
        this.b = val
        return this
    }

    build() {
        return "Result: " + this.a + ", " + this.b
    }
}

with builder = Builder() {
    print(.setA(10).setB(20).build())
}

// Expected output:
// Result: 10, 20

//
// Test 8: Mixing explicit and implicit
//
print("=== Test 8: Explicit and implicit ===")

with r = Resource("mixed") {
    .increment()           // Implicit
    r.increment()          // Explicit
    print(.get())          // Implicit
    print(r.get())         // Explicit
}

// Expected output:
// 2
// 2
// Cleanup: mixed

//
// Test 9: Closure capture
//
print("=== Test 9: Closure capture ===")

var closure
with r = Resource("captured") {
    closure = () { return .name }
}
// r cleanup happens here
print("Captured: " + closure())

// Expected output:
// Cleanup: captured
// Captured: captured
```

**Expected output file:** `src/test/scripts/with.sox.out`

### Error Handling Tests

**Test file:** `src/test/scripts/with_errors.sox`

```javascript
// Error 1: Dot outside with
.method()
// Expected: Compile error: Cannot use '.' outside 'with' statement

// Error 2: Cleanup throws
class BadCleanup {
    __cleanup() {
        error("Cleanup failed")
    }
}

with x = BadCleanup() {
    print("using")
}
// Expected: Runtime error: Cleanup failed
```

### Performance Tests

**Benchmark:** Compare with and without `with` statement:

```javascript
class Counter {
    init() { this.count = 0 }
    inc() { this.count = this.count + 1 }
}

// Test 1: Explicit access
var start = clock()
var c1 = Counter()
for (var i = 0; i < 100000; i++) {
    c1.inc()
}
print("Explicit: " + (clock() - start))

// Test 2: With statement
start = clock()
with c2 = Counter() {
    for (var i = 0; i < 100000; i++) {
        .inc()
    }
}
print("With: " + (clock() - start))
```

**Expected:** Performance should be identical (both compile to same bytecode).

---

## Implementation Plan

### Phase 1: Scanner + Basic Compilation (3-4 days)

**Tasks:**
1. ✅ Add `TOKEN_WITH` to scanner
2. ✅ Add context tracking to `compiler_t`
3. ✅ Implement `_with_statement()` skeleton
4. ✅ Parse with statement syntax
5. ✅ Write unit tests for parsing

**Deliverables:**
- With statement parses without errors
- Context tracking works
- Basic compilation tests pass

**Blockers:** None

### Phase 2: Implicit Member Access (5-7 days)

**Tasks:**
1. ✅ Modify `_primary()` to handle leading dot
2. ✅ Implement dot-to-context transformation
3. ✅ Handle property access (`.property`)
4. ✅ Handle method calls (`.method()`)
5. ⚠️ Handle property assignment (`.property = value`)
6. ✅ Test nested with contexts

**Deliverables:**
- Leading dot syntax works
- Implicit member access compiles correctly
- Integration tests pass

**Blockers:**
- Property assignment may require parser refactoring
- Pratt parser integration needs careful testing

### Phase 3: Automatic Cleanup (4-5 days)

**Tasks:**
1. ✅ Implement cleanup code generation
2. ✅ Add `__cleanup()` method calling
3. ⚠️ Handle missing cleanup method gracefully
4. ✅ Test cleanup with early return
5. ✅ Test cleanup with break/continue
6. ✅ Test cleanup errors

**Deliverables:**
- Cleanup happens at scope exit
- All control flow cases handled
- Error cases tested

**Blockers:**
- Need to decide on "call if exists" implementation
- May need `OP_DUP` opcode for cleaner code

### Phase 4: Edge Cases and Polish (3-4 days)

**Tasks:**
1. ✅ Test all edge cases
2. ✅ Improve error messages
3. ✅ Add documentation
4. ✅ Performance testing
5. ✅ Serialization testing

**Deliverables:**
- All edge cases work correctly
- Clear error messages
- Documentation complete

**Blockers:** None

### Phase 5: Integration and Release (2-3 days)

**Tasks:**
1. ✅ Update README with examples
2. ✅ Merge to main branch
3. ✅ Announce feature

**Deliverables:**
- Feature merged
- Documentation updated
- Examples published

**Total estimated time:** 17-23 days (~3-4 weeks)

---

## Alternatives Considered

### Alternative 1: Python-Style With (No Implicit Access)

```python
with file = open("data.txt") {
    file.write("data")  # Must use explicit name
}
```

**Pros:**
- Simpler implementation (no dot syntax parsing)
- Less ambiguity

**Cons:**
- Still repetitive (defeats purpose)
- Not as ergonomic as VB

**Decision:** Rejected - implicit access is the key value proposition.

### Alternative 2: No Automatic Cleanup

```javascript
with obj = getObject() {
    .method()
    // No cleanup
}
```

Just VB-style syntax, no cleanup.

**Pros:**
- Simpler implementation

**Cons:**
- Misses opportunity for resource management
- Less powerful than Python's with

**Decision:** Rejected - cleanup adds significant value with minimal cost.

### Alternative 3: Bare Identifier (No Dot)

```javascript
with obj = getObject() {
    method()  // Implicitly calls obj.method()
}
```

**Pros:**
- Even less typing

**Cons:**
- Ambiguous with local variables/functions
- Confusing: `method()` could be local or on obj
- Breaking change if variable shadows

**Decision:** Rejected - too ambiguous, error-prone.

### Alternative 4: This Keyword

```javascript
with obj = getObject() {
    this.method()
}
```

**Pros:**
- Familiar from OOP

**Cons:**
- Conflicts with class `this`
- Confusing in methods
- Still requires typing

**Decision:** Rejected - keyword conflict, not ergonomic enough.

### Alternative 5: Auto Keyword

```javascript
with obj = getObject() {
    auto.method()  // or @.method()
}
```

**Pros:**
- No ambiguity
- Clear intent

**Cons:**
- More typing than `.`
- Less familiar than VB syntax

**Decision:** Rejected - dot is more ergonomic and familiar.

---

## Open Questions

### 1. Should We Support Bare Dot as Value?

**Question:** Can you reference just the with variable using `.`?

```javascript
with obj = getObject() {
    process(.)  // Pass obj to function?
}
```

**Options:**
- **A:** Yes, bare `.` resolves to with variable
- **B:** No, must use explicit name (`process(obj)`)

**Current decision:** **B** (no bare dot) for simplicity. Use explicit name to pass variable.

**Rationale:** Rare use case, adds complexity, less clear.

### 2. Multiple With Variables in One Statement?

**Question:** Should we support multiple resources?

```javascript
with (a = res1(), b = res2()) {
    a.method()
    b.method()
    // Which is the implicit receiver?
}
```

**Options:**
- **A:** Support multiple, no implicit receiver (must use explicit names)
- **B:** Support multiple, first is implicit
- **C:** Don't support (current decision)

**Current decision:** **C** (single resource only) for MVP.

**Rationale:** Simpler implementation, unclear semantics with multiple. Can add later if needed.

### 3. Cleanup Method Name Convention?

**Options:**
- `__cleanup()` (current) - matches `__init__`
- `close()` - simple, common
- `dispose()` - C# style
- `destroy()` - alternative

**Current decision:** `__cleanup()` for consistency with magic methods.

**Open for discussion!**

### 4. Should Property Assignment Work?

```javascript
with obj = MyObject() {
    .property = 42  // Should this work?
}
```

**Current decision:** **Yes** - should work for consistency.

**Implementation challenge:** Requires assignment detection in parser.

**Alternative:** Only support method calls (`.method()`), not property access.

**Preference:** Support both for full VB compatibility.

### 5. Error on Missing Cleanup vs. Silent No-Op?

**Question:** What if `__cleanup()` doesn't exist?

**Options:**
- **A:** Silent no-op (current) - allows with for any object
- **B:** Runtime error - enforces cleanup protocol
- **C:** Warning - middle ground

**Current decision:** **A** (silent) for flexibility.

**Rationale:** Allows using `with` for syntactic convenience even without cleanup.

### 6. Syntax: Parens Required?

```javascript
with (obj = expr) { }  // Option A: parens required
with obj = expr { }    // Option B: no parens
```

**Current decision:** **A** (parens required) for consistency with `if`, `while`, `for`.

**Alternative:** Make parens optional like Python.

---

## Conclusion

This proposal introduces a powerful `with` statement to Sox that combines:

1. **VB-style ergonomics:** Implicit member access via leading `.` syntax
2. **Python-style safety:** Automatic cleanup via `__cleanup()` protocol
3. **Sox-style simplicity:** Builds on existing infrastructure, minimal new code

**Key advantages:**
- ✅ Reduces code repetition (15-30% fewer characters for multi-method calls)
- ✅ Improves readability (focus on operations, not object names)
- ✅ Guarantees resource cleanup (prevents resource leaks)
- ✅ Familiar syntax (VB users will recognize immediately)
- ✅ Simple implementation (~215 LOC, no new VM opcodes)

**Recommendation:** Proceed with implementation following phased plan.

The feature aligns perfectly with Sox's goals:
- Practical and useful for real code
- Simple and clear syntax
- Builds on existing language features
- Minimal complexity cost

**Next Steps:**
1. ✅ Review and approve this proposal
2. ⏳ Clarify open questions (see section above)
3. ⏳ Begin Phase 1 implementation (scanner + context tracking)
4. ⏳ Iterate based on testing and feedback

---

## References

**Language inspiration:**
- **Visual Basic:** `With` statement for implicit member access
- **Python:** `with` statement and context managers
- **C#:** `using` statement for automatic disposal
- **Go:** `defer` for cleanup (already in Sox)

**Sox documentation:**
- `CLAUDE.md` - Project architecture and development practices
- `README.md` - Language features and examples
- `docs/wasm.md` - WASM implementation status

**Related code:**
- `src/compiler.c:_primary()` - Expression parsing (needs modification)
- `src/compiler.c:_for_statement()` - Example of scoped variable binding
- `src/compiler.c:_defer_declaration()` - Existing cleanup mechanism
- `src/compiler.c:_end_scope()` - Scope exit logic
- `src/vm.c:OP_CLOSE_UPVALUE` - Upvalue closure for captures

**Testing references:**
- `src/test/scripts/loops.sox` - Control flow examples
- `src/test/scripts/defer.sox` - Cleanup examples
- `src/test/scripts/closure.sox` - Upvalue/capture examples
