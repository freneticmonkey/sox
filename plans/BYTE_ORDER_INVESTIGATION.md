# Register Pair Byte Ordering Investigation

## Problem Statement

When loading 16-byte value_t constants into ARM64 register pairs, the bytes are being distributed incorrectly across the pair, causing runtime type checking failures in arithmetic operations.

## Test Case: `print(10 + 20)`

Expected behavior: Should print 30
Actual behavior: Prints 20 with incorrect type information

## Key Findings

### 1. Disassembly Analysis

Both register pairs receive ONLY the high 8 bytes of their respective constants:

```asm
# Loading first constant (10.0)
mov  x10, #0x4024, lsl #48    ; x10 = 0x4024000000000000 (HIGH BYTES)
# Should be: x10 = 0x0000000000000002 (LOW BYTES - type + padding)

# Loading second constant (20.0)
mov  x12, #0x4024, lsl #48    ; x12 = 0x4024000000000000 (HIGH BYTES)
# Should be: x12 = 0x0000000000000002 (LOW BYTES)
```

### 2. Expected vs Actual Value_t Structure Layout

**value_t structure** (16 bytes):
```
Bytes [0-3]:    ValueType type (4 bytes)
Bytes [4-7]:    Padding (4 bytes)
Bytes [8-15]:   Union as (8 bytes, contains double)
```

**For value 10.0 in two 64-bit halves**:
- Low 8 bytes [0-7]:    0x0000000000000002 (type=VAL_NUMBER, padding)
- High 8 bytes [8-15]:  0x4024000000000000 (10.0 as IEEE-754)

**What ARM64 ABI expects in register pairs** (for function arguments):
- X0:X1 pair for first 16-byte argument:
  - X0 (low register):  bytes [0-7]   = type + padding
  - X1 (high register): bytes [8-15]  = numeric value
- X2:X3 pair for second 16-byte argument (same structure)

### 3. Current Code Behavior

The IR_CONST_FLOAT handler does:
```c
arm64_reg_pair_t pair = get_register_pair_arm64(ctx, instr->dest);

uint64_t low_val = *(uint64_t*)(&v);         // bytes [0-7]
arm64_mov_reg_imm(ctx->asm_, pair.low, low_val);

uint64_t high_val = *((uint64_t*)(&v) + 1);  // bytes [8-15]
arm64_mov_reg_imm(ctx->asm_, pair.high, high_val);
```

This SHOULD correctly:
- Extract bytes [0-7] as low_val = 0x0000000000000002
- Extract bytes [8-15] as high_val = 0x4024000000000000
- Load low_val into pair.low
- Load high_val into pair.high

### 4. Actual Observation vs Code

**Disassembly shows**:
- pair.low register (e.g., X10) receives 0x4024000000000000
- pair.high register (e.g., X11) receives 0x4034000000000000

**This means**:
- The LOW register is getting the HIGH bytes
- Both registers are getting numeric values, not the split structure

## Root Cause Hypotheses

### Hypothesis A: pair.low and pair.high are Inverted

If `get_register_pair_arm64()` returns the pair with low and high reversed:
- `result.low = X11` (should be X10)
- `result.high = X10` (should be X11)

Then `arm64_mov_reg_imm(pair.low, low_val)` would load to X11, and
`arm64_mov_reg_imm(pair.high, high_val)` would load to X10.

**Status**: Need to verify through debug output

### Hypothesis B: Bytes are Extracted in Reverse Order

If the memory layout of value_t is somehow different than expected:
- Perhaps the double comes before the type in memory?
- Perhaps there's alignment padding that shifts everything?

**Status**: Verified structure layout with unit test - appears correct

### Hypothesis C: Constants Stored Reversed in IR

The IR might be storing constants with bytes in a different order than expected.

**Status**: Need to check ir_value_constant() implementation

### Hypothesis D: arm64_mov_reg_imm() Issue

The function that encodes the immediate value might have a bug:
- Maybe it's using only high bytes for values that span multiple mov/movk instructions
- Maybe there's a sign extension issue

**Status**: Need to examine arm64_encoder.c implementation

## Attempted Fixes

### Attempt 1: Swap Byte Extraction Order

Modified code to:
```c
uint64_t low_val = *((uint64_t*)(&v) + 1);  // bytes [8-15]
uint64_t high_val = *(uint64_t*)(&v);       // bytes [0-7]
```

**Result**: Object file corruption in some tests, suggesting deeper issue

### Attempt 2: Verify Register Allocation

Checked that:
- Register allocator assigns consecutive pairs (e.g., X10:X11)
- x10 + 1 == x11 (consecutive)
- regalloc_arm64_get_high_register() correctly returns high register

**Result**: Allocation appears correct

## Key Observations

1. **Both registers in a pair receive high bytes**: This rules out a simple byte reversal - the problem is that BOTH registers get the same type of bytes.

2. **Type information is missing**: When sox_native_print receives values, it shows the type correctly (type=2), but bits contain only high bytes of the number, not the split.

3. **Register pair detection works**: Disassembly shows X10:X11 being used, which matches the allocation.

4. **Simple constants cause the issue**: Even just `print(42)` would show incorrect bytes in X1.

## Debugging Strategy

### Phase 1: Add Debug Output (CRITICAL)

Add logging to IR_CONST_FLOAT:
```c
fprintf(stderr, "[DEBUG] IR_CONST_FLOAT: vtype=%d\n", v.type);
fprintf(stderr, "[DEBUG]   low_val=0x%016llx\n", (unsigned long long)low_val);
fprintf(stderr, "[DEBUG]   high_val=0x%016llx\n", (unsigned long long)high_val);
fprintf(stderr, "[DEBUG]   pair.low=%d, pair.high=%d\n", pair.low, pair.high);
```

### Phase 2: Verify Register Names

Print actual ARM64 register names:
```c
fprintf(stderr, "[DEBUG]   pair.low_name=%s, pair.high_name=%s\n",
        arm64_register_name(pair.low), arm64_register_name(pair.high));
```

### Phase 3: Check Disassembly

Examine object file disassembly with debug symbols to verify register assignments match expectations.

### Phase 4: Trace arm64_mov_reg_imm()

Verify that the function correctly encodes the constant:
- For 0x0000000000000002: Should generate `mov xX, #2`
- For 0x4024000000000000: Should generate `mov xX, #0; movk xX, #0x4024, lsl #48`

## Files Involved

- `src/native/codegen_arm64.c`: IR_CONST_FLOAT handler (lines 270-290)
- `src/native/regalloc_arm64.c`: get_register_pair_arm64() function
- `src/native/arm64_encoder.c`: arm64_mov_reg_imm() implementation (need to examine)
- `src/native/ir_builder.c`: Constant creation (ir_value_constant)

## Impact on Implementation

Once byte ordering is fixed:
1. Constants will be correctly split across register pairs
2. Type checking in runtime functions will work correctly
3. Arithmetic operations will receive properly-formed value_t structures
4. All variable-based tests can be enabled and tested

## Related Commits

- f55f73c: Investigate register pair byte ordering
- c32e17a: Add comprehensive debugging findings
- 8a07937: Fix critical ARM64 codegen issues

## Next Session Priorities

1. **IMMEDIATELY**: Add debug output to IR_CONST_FLOAT and trace one constant through the system
2. **CRITICAL**: Determine whether register assignment or byte extraction is wrong
3. **HIGH**: Fix the identified issue and test with simple constant print
4. **FOLLOW-UP**: Test arithmetic operations once constants work

