# Implementation Plan: Full String Constant Support for Native Code Generation

## Context

The Sox compiler currently supports native ARM64 code generation for most language features, but string constants are only partially implemented. The infrastructure exists to detect and extract string constants from bytecode, but the strings are not embedded in the generated object files. Currently, string constants are replaced with `nil` placeholders.

**Current State (as of 2025-12-26)**:
- ✅ IR builder detects string constants and creates `IR_CONST_STRING` instructions
- ✅ String data is extracted and stored in IR (`ir_instruction_t.string_data`, `string_length`)
- ✅ ARM64 codegen generates valid code (loads NIL as placeholder)
- ✅ No crashes or timeouts
- ❌ Actual string values not embedded in object file
- ❌ Strings not allocated at runtime

**Test Status**: `strings.sox` outputs "nil nil nil" instead of "Hello World Native compilation works!"

**Goal**: Implement full string constant support so that native binaries can create and print actual string objects.

## Technical Requirements

### 1. Mach-O Object File String Section

String literals must be embedded in the generated Mach-O object file in a data section. There are two common approaches:

**Option A: `__TEXT,__cstring` section** (Recommended)
- C-string section for null-terminated strings
- Standard location for string literals
- Automatically deduplicated by the linker
- Section flags: `S_CSTRING_LITERALS`

**Option B: `__DATA,__data` section**
- General data section
- More flexible but requires manual deduplication
- Section flags: `S_REGULAR`

### 2. String Data Layout

Each unique string literal needs:
1. **Storage**: Null-terminated C string in the data section
2. **Symbol**: A unique symbol name (e.g., `.L.str.0`, `.L.str.1`)
3. **Relocation**: PC-relative relocation to load the string address

### 3. Runtime String Allocation

At runtime, the native code must call `sox_native_alloc_string(const char* chars, size_t length)` to create a Sox string object from the C string data.

**Function Signature** (from `src/runtime_lib/runtime_api.h:167`):
```c
SOX_API value_t sox_native_alloc_string(const char* chars, size_t length);
```

**Parameters**:
- `chars`: Pointer to null-terminated C string (loaded via PC-relative addressing)
- `length`: String length (known at compile time, passed as immediate)

**Returns**: `value_t` containing the allocated string object

## Implementation Steps

### Step 1: Extend Mach-O Writer to Support Data Sections

**File**: `src/native/macho_writer.c` / `src/native/macho_writer.h`

**Tasks**:
1. Add a method to add a `__cstring` section:
   ```c
   int macho_add_cstring_section(macho_builder_t* builder,
                                  const char* data,
                                  size_t size);
   ```

2. Create symbols for string literals:
   ```c
   int macho_add_string_literal_symbol(macho_builder_t* builder,
                                        const char* symbol_name,
                                        int section_index,
                                        size_t offset);
   ```

3. Update `macho_create_executable_object_file_with_arm64_relocs()` to:
   - Accept an array of string literals
   - Create a `__cstring` section with all string data
   - Create symbols for each string literal
   - Return the section index and symbol mappings

**Implementation Notes**:
- String literals should be concatenated into a single section
- Each string needs a unique symbol pointing to its offset in the section
- Maintain a mapping from IR string index to (section, offset, symbol_name)

### Step 2: Track String Literals in Code Generator

**File**: `src/native/codegen_arm64.h` / `src/native/codegen_arm64.c`

**Tasks**:
1. Add string literal tracking to `codegen_arm64_context_t`:
   ```c
   typedef struct {
       const char* data;      // String data
       size_t length;         // String length
       const char* symbol;    // Symbol name (e.g., ".L.str.0")
       int section_index;     // Section index in object file
       size_t section_offset; // Offset within section
   } string_literal_t;

   // Add to codegen_arm64_context_t:
   string_literal_t* string_literals;
   int string_literal_count;
   int string_literal_capacity;
   ```

2. Add a function to register string literals:
   ```c
   int codegen_arm64_add_string_literal(codegen_arm64_context_t* ctx,
                                        const char* data,
                                        size_t length);
   ```

3. In `codegen_arm64_generate()`, after generating code:
   - Collect all string literals
   - Create `__cstring` section with concatenated strings
   - Generate symbols for each literal
   - Store the mapping for relocation resolution

### Step 3: Generate Code for IR_CONST_STRING

**File**: `src/native/codegen_arm64.c` (around line 409)

**Replace the placeholder implementation** with actual code generation:

```c
case IR_CONST_STRING: {
    // Register the string literal and get its index
    int str_idx = codegen_arm64_add_string_literal(ctx,
                                                     instr->string_data,
                                                     instr->string_length);

    if (instr->dest.type == IR_VAL_REGISTER && instr->dest.size == IR_SIZE_16BYTE) {
        // Get the destination register pair for the value_t result
        arm64_reg_pair_t dest_pair = get_register_pair_arm64(ctx, instr->dest);

        if (dest_pair.low != ARM64_NO_REG && dest_pair.is_pair) {
            // Step 1: Load the address of the string literal
            // Use X15 as temporary register for string address
            arm64_register_t str_addr_reg = ARM64_X15;

            // ADRP + ADD to load PC-relative address with relocation
            // This will be patched by the linker to point to the string data
            size_t adrp_offset = arm64_get_offset(ctx->asm_);
            arm64_adrp(ctx->asm_, str_addr_reg, 0);  // Page-relative address

            size_t add_offset = arm64_get_offset(ctx->asm_);
            arm64_add_imm(ctx->asm_, str_addr_reg, str_addr_reg, 0);  // Page offset

            // Add relocations for the string literal address
            // ARM64_RELOC_PAGE21 for ADRP (page-relative)
            arm64_add_relocation(ctx->asm_, adrp_offset,
                                ARM64_RELOC_PAGE21,
                                ctx->string_literals[str_idx].symbol,
                                0);

            // ARM64_RELOC_PAGEOFF12 for ADD (offset within page)
            arm64_add_relocation(ctx->asm_, add_offset,
                                ARM64_RELOC_PAGEOFF12,
                                ctx->string_literals[str_idx].symbol,
                                0);

            // Step 2: Prepare arguments for sox_native_alloc_string
            // X0 = const char* chars (string address)
            // X1 = size_t length (string length as immediate)
            arm64_mov_reg_reg(ctx->asm_, ARM64_X0, str_addr_reg);
            arm64_mov_reg_imm(ctx->asm_, ARM64_X1, instr->string_length);

            // Step 3: Call sox_native_alloc_string
            size_t call_offset = arm64_get_offset(ctx->asm_);
            arm64_bl(ctx->asm_, 0);  // Placeholder, will be relocated

            // Add relocation for the function call
            arm64_add_relocation(ctx->asm_, call_offset,
                                ARM64_RELOC_CALL26,
                                "sox_native_alloc_string",
                                0);

            // Step 4: Move return value (in X0:X1) to destination register pair
            // sox_native_alloc_string returns value_t in X0:X1
            arm64_mov_reg_reg(ctx->asm_, dest_pair.low, ARM64_X0);
            arm64_mov_reg_reg(ctx->asm_, dest_pair.high, ARM64_X1);
        }
    }
    break;
}
```

**Key Points**:
- Use `ADRP` + `ADD` for PC-relative address loading (required for position-independent code)
- Use `ARM64_RELOC_PAGE21` and `ARM64_RELOC_PAGEOFF12` relocations for the address
- Call `sox_native_alloc_string` with proper ARM64 calling convention
- Result is returned in X0:X1 (16-byte value_t)

### Step 4: Update ARM64 Encoder for Missing Instructions

**File**: `src/native/arm64_encoder.c` / `src/native/arm64_encoder.h`

**Tasks**:
Add the following instructions if they don't already exist:

1. **ADRP** (Load page-relative address):
   ```c
   void arm64_adrp(arm64_assembler_t* asm_, arm64_register_t rd, int32_t page_offset);
   ```
   - Encoding: `1xx10000 [21-bit signed page offset] [5-bit rd]`
   - Loads the address of a 4KB page relative to PC

2. **ADD (immediate)** (if not already implemented):
   ```c
   void arm64_add_imm(arm64_assembler_t* asm_, arm64_register_t rd,
                      arm64_register_t rn, uint16_t imm12);
   ```
   - Encoding: `10010001 00 [12-bit imm] [5-bit rn] [5-bit rd]`
   - Adds a 12-bit immediate to a register

3. **BL** (Branch with link - function call):
   ```c
   void arm64_bl(arm64_assembler_t* asm_, int32_t offset);
   ```
   - Encoding: `100101 [26-bit signed offset]`
   - Calls a function and saves return address in X30 (LR)

**Reference**: ARM Architecture Reference Manual for A-profile architecture

### Step 5: Update Mach-O Object File Generation

**File**: `src/native/native_codegen.c` (or wherever object file creation happens)

**Tasks**:
1. Modify the object file creation to accept string literal data:
   ```c
   bool native_generate_object_file(
       const char* filename,
       uint8_t* code,
       size_t code_size,
       string_literal_t* string_literals,
       int string_literal_count,
       arm64_relocation_t* relocations,
       int reloc_count
   );
   ```

2. Create the `__cstring` section:
   - Concatenate all string literals
   - Calculate offsets for each string
   - Add section with `S_CSTRING_LITERALS` flag

3. Create symbols for string literals:
   - Add symbols pointing to each string's offset in the section
   - Mark as local symbols (N_SECT, not N_EXT)

4. Update section index references in relocations

### Step 6: Testing and Validation

**Test Cases**:

1. **Single String** (`test_single_string.sox`):
   ```
   print("Hello");
   ```
   Expected output: `Hello`

2. **Multiple Strings** (`test_multiple_strings.sox`):
   ```
   print("Hello");
   print("World");
   print("!");
   ```
   Expected output: `Hello\nWorld\n!`

3. **Duplicate Strings** (`test_duplicate_strings.sox`):
   ```
   print("test");
   print("test");
   ```
   Expected output: `test\ntest`
   (Verify deduplication works)

4. **Empty String** (`test_empty_string.sox`):
   ```
   print("");
   ```
   Expected output: (empty line)

5. **String with Special Characters** (`test_special_chars.sox`):
   ```
   print("Hello\nWorld\t!");
   ```
   Expected output: `Hello\nWorld\t!` (with actual newline and tab)

**Validation Commands**:
```bash
# Build and run test
./build/sox test.sox --native --native-out=/tmp/test_native
DYLD_LIBRARY_PATH=./build:$DYLD_LIBRARY_PATH /tmp/test_native

# Verify object file structure
otool -l test.tmp.o  # Check for __cstring section
otool -s __TEXT __cstring test.tmp.o  # View string data
nm test.tmp.o  # Check symbols
otool -r test.tmp.o  # Verify relocations
```

**Expected Object File Structure**:
```
Sections:
  Section __text, __TEXT
    - Contains machine code
    - Has ARM64_RELOC_CALL26 relocations for function calls
    - Has ARM64_RELOC_PAGE21/PAGEOFF12 for string addresses

  Section __cstring, __TEXT
    - Contains null-terminated string literals
    - Flags: S_CSTRING_LITERALS
    - Strings are concatenated with null terminators

Symbols:
  _main (external, defined in __text)
  _sox_main (external, defined in __text)
  _sox_native_print (undefined, external)
  _sox_native_alloc_string (undefined, external)
  .L.str.0 (local, defined in __cstring at offset 0)
  .L.str.1 (local, defined in __cstring at offset N)
```

## Files to Modify

1. **`src/native/macho_writer.h`** - Add string section support declarations
2. **`src/native/macho_writer.c`** - Implement string section creation
3. **`src/native/codegen_arm64.h`** - Add string literal tracking to context
4. **`src/native/codegen_arm64.c`** - Implement IR_CONST_STRING code generation
5. **`src/native/arm64_encoder.h`** - Add ADRP, ADD, BL declarations
6. **`src/native/arm64_encoder.c`** - Implement ADRP, ADD, BL instructions
7. **`src/native/native_codegen.c`** - Update object file creation
8. **`src/test/scripts/native/README.md`** - Update test status

## Reference Code Patterns

### Existing String Symbol in Runtime
See `src/runtime_lib/runtime_api.c:251-258` for string allocation:
```c
value_t sox_native_alloc_string(const char* chars, size_t length) {
    if (!chars) return NIL_VAL;

    runtime_obj_string_t* string = runtime_copy_string(chars, length);
    if (!string) return NIL_VAL;

    return OBJ_VAL(string);
}
```

### Existing Relocation Pattern
See `src/native/codegen_arm64.c` around line 700+ for function call pattern:
```c
// Example: Calling a runtime function
size_t call_offset = arm64_get_offset(ctx->asm_);
arm64_bl(ctx->asm_, 0);  // Placeholder
arm64_add_relocation(ctx->asm_, call_offset,
                    ARM64_RELOC_CALL26,
                    "sox_not",  // or other function name
                    0);
```

### Existing Section Creation
See `src/native/macho_writer.c:108-154` for section creation pattern:
```c
int macho_add_section(macho_builder_t* builder, const char* sectname,
                      const char* segname, uint32_t flags,
                      const uint8_t* data, size_t size, uint32_t align);
```

## Success Criteria

1. ✅ `strings.sox` test passes completely
2. ✅ Object file contains `__cstring` section with string data
3. ✅ Correct relocations for PC-relative string address loading
4. ✅ Runtime calls `sox_native_alloc_string` correctly
5. ✅ No memory leaks or crashes
6. ✅ All 7 native tests pass (100% pass rate)

## Potential Challenges

1. **Relocation Types**: ARM64 uses two-step PC-relative addressing (ADRP + ADD) which requires two separate relocations. Make sure both are generated correctly.

2. **Symbol Naming**: Local symbols for strings should not conflict. Use a consistent naming scheme like `.L.str.N` where N is incremented.

3. **String Deduplication**: The linker may deduplicate identical strings in `__cstring` section. This is expected behavior and should be tested.

4. **Null Termination**: Ensure all strings are null-terminated in the section, and the length passed to `sox_native_alloc_string` does NOT include the null terminator.

5. **Position-Independent Code**: ADRP/ADD pattern is required for PIC. Don't use absolute addresses.

## Additional Resources

- **ARM64 Instruction Encoding**: ARM Architecture Reference Manual
- **Mach-O Format**: [Mach-O File Format Reference](https://github.com/aidansteele/osx-abi-macho-file-format-reference)
- **ARM64 Relocations**: See `/usr/include/mach-o/arm64/reloc.h` on macOS
- **Runtime API**: `src/runtime_lib/runtime_api.h`

## Estimated Effort

- **Mach-O string section support**: 2-3 hours
- **ARM64 instruction encoding** (ADRP, ADD, BL): 1-2 hours
- **Code generation for IR_CONST_STRING**: 2-3 hours
- **Integration and testing**: 2-3 hours
- **Total**: 7-11 hours

## Next Steps After Completion

Once string constants work:
1. Consider implementing string operations (concatenation, substring, etc.)
2. Add support for other object types (arrays, tables)
3. Optimize string literal deduplication at compile time
4. Add string interning support for better memory efficiency
