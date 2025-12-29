# Native Execution Runtime Debugging Report

## Summary
Implemented critical fixes for ARM64 native code generation register pair handling and variable loading. However, runtime errors persist due to deeper architectural issues with how 16-byte composite values (value_t) are marshaled through the calling convention.

## Key Findings

### 1. Register Pair Allocation Issue

**Problem**: Both constants and results being allocated to the same register pair (X10:X11), causing overwrites.

Example from `print(10 + 20)`:
```
ARM64 Register Allocation:
  v1 [1-2] (16byte): x10:x11  <- First constant (10.0)
  v2 [2-3] (16byte): x12:x13  <- Second constant (20.0)  
  v3 [4-5] (16byte): x10:x11  <- ADD result (overwrites v1!)
```

While registers CAN be reused if live ranges don't overlap temporally, the actual disassembly shows:
```asm
mov x10, #0x4024, lsl #48    ; Load 10.0 high bytes (incorrect!)
movk x11, #0x4034, lsl #48   ; Load 20.0 high bytes (incorrect!)
mov x2, x10                   ; Pass 10.0 as second ADD operand (WRONG!)
```

### 2. Value_t Structure Layout Issue

**Finding**: value_t is 16 bytes with padding:
```
Bytes [0-3]:    ValueType type (4 bytes)
Bytes [4-7]:    Padding (4 bytes) - UNINITIALIZED!
Bytes [8-15]:   Union as (8 bytes)
```

When split into 64-bit halves for register pairs:
- **Low 8 bytes**:  0x0000000000000002 (type=2, padding)
- **High 8 bytes**: 0x4024000000000000 (10.0 as IEEE-754 double)

But disassembly shows X10 getting the HIGH bytes (0x4024...), not the LOW bytes!

### 3. Constant Loading Code Generation Bug

**Issue**: The IR_CONST_FLOAT handler appears to be loading the WRONG bytes into the register pair.

```c
// Expected behavior:
uint64_t low_val = *(uint64_t*)(&v);      // bytes [0-7]: type + padding
uint64_t high_val = *((uint64_t*)(&v)+1); // bytes [8-15]: double value
arm64_mov_reg_imm(ctx->asm_, pair.low, low_val);    // Put in low register
arm64_mov_reg_imm(ctx->asm_, pair.high, high_val);  // Put in high register
```

**Actual behavior from disassembly**:
- pair.high register gets the double value (0x4024...)
- pair.low register may not be getting initialized correctly

### 4. Calling Convention Mismatch

**ARM64 ABI Requirement**: 16-byte composite types in register pairs must be:
- X0:X1 for first argument (low:high)
- X2:X3 for second argument (low:high)

**What's happening**:
- The code loads values but puts them in wrong registers
- Then marshals them to argument registers with wrong source registers
- Result: sox_add receives corrupted/garbage values

Example from `print(10 + 20)` disassembly:
```asm
mov x0, x9    ; X0 gets some value (should be low bytes of 10.0)
mov x1, x10   ; X1 gets 0x4024... (high bytes of 10.0, but in LOW position!)
mov x2, x10   ; X2 gets 0x4024... (WRONG! Should get low bytes of 20.0)
mov x3, x11   ; X3 gets 0x4034... (high bytes of 20.0)
bl sox_add
```

Result: sox_add receives:
- (X0:X1) = uninitialized + 10.0 data (WRONG SPLIT)
- (X2:X3) = 10.0 data + 20.0 data (COMPLETELY WRONG)

Type check in sox_add fails because X0/X2 don't contain proper type tags!

## Root Cause Analysis

### Likely Issues (in priority order):

1. **Binary/Endianness Issue**:
   - Possible byte-order mismatch when casting value_t to uint64_t
   - ARM64 is little-endian, need to verify bit layout matches expectations

2. **Register Pair Detection Bug**:
   - `get_register_pair_arm64()` may be incorrectly determining which is low vs high
   - The fix to use allocator API may not be working correctly
   - Need to verify `regalloc_arm64_get_high_register()` returns correct values

3. **mov_reg_imm Implementation**:
   - The arm64_mov_reg_imm() function may have bugs in how it encodes the constant
   - Need to verify it generates correct movk sequences

4. **Live Range Calculation**:
   - The live range analyzer may not be correctly capturing value usage
   - Causes incorrect register allocation decisions

5. **IR_LOAD_LOCAL/IR_STORE_LOCAL**:
   - Newly implemented handlers may have bugs in offset calculation
   - Stack frame layout may not match expectations

## Implemented Fixes Status

✅ **Committed**: All critical fixes for register pair handling
- Added `regalloc_arm64_get_high_register()` API
- Fixed `load_16byte_argument_x0x1()` register pair marshaling
- Implemented IR_LOAD_LOCAL and IR_STORE_LOCAL handlers
- Updated `get_register_pair_arm64()` to use allocator API

⚠️ **Testing Status**: Object file corruption on newer tests
- Simple constant tests may work but produce incorrect results
- Variable-based tests cause object file corruption during generation
- Linking failures indicate Mach-O generation issue

## Next Steps for Resolution

1. **Add Comprehensive Debug Output**:
   - Print register pair contents after constant loading
   - Log marshal operations in ADD handler
   - Verify stack offsets in LOAD_LOCAL

2. **Unit Test Register Splitting**:
   - Verify value_t byte layout matches assumptions
   - Test constant loading in isolation
   - Validate register pair marshaling

3. **Fix Object File Corruption**:
   - Appears related to variable-based test generation
   - May be in Mach-O writer or symbol table generation
   - Check LINKEDIT segment creation

4. **Verify ARM64 ABI Compliance**:
   - Double-check register pair ordering
   - Validate constant encoding in movk sequences
   - Test with objdump disassembly

5. **Integration Testing**:
   - Once basic constant loading works, test variable loading
   - Then test arithmetic operations
   - Finally test complete programs

## Files Affected

- src/native/codegen_arm64.c - Register pair handling and instruction generation
- src/native/regalloc_arm64.c/h - Register allocation queries
- src/native/ir_builder.c - IR generation (unchanged in this session)
- src/native/macho_writer.c - Object file generation (potential issues)

## Diagnostic Commands

```bash
# Test simple constant
/Users/scott/development/projects/sox/build/sox /tmp/test_simple_add.sox \
  --native --native-out=/tmp/test_native

# Disassemble generated code
otool -tv /tmp/test_native

# Check register allocation details
otool -l /tmp/test_native | grep -A 20 "Load command"

# Verify linking
clang /tmp/test_native.tmp.o -o /tmp/test_native -L./build -lsox_runtime -v
```
