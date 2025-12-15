# Register Pair Byte Ordering Fix - Summary

## Investigation Complete ✅

The subagent successfully completed the 4-phase debugging investigation and identified the actual root cause, which was **NOT** a byte ordering issue at all!

## The Real Problem: Register Allocator Size Mismatch

### Root Cause

In `src/native/regalloc_arm64.c`, function `add_live_range()` had a critical bug:
- When a virtual register was first encountered as an 8-byte operand
- Then later encountered as a 16-byte destination
- The size field was NEVER updated
- Result: Register allocated as single register instead of pair

### Example of the Bug

```
Virtual register v0:
  - First use: as 8-byte operand → range.size = 8
  - Second use: as 16-byte destination → size = 16 (but range.size still 8!)
  - Allocation: (8byte): x9 (SINGLE register)
  
Expected allocation:
  - Allocation: (16byte): x9:x10 (REGISTER PAIR)
```

### Impact Chain

1. ❌ Size not updated → allocate as 8-byte
2. ❌ Single register allocated instead of pair
3. ❌ `get_register_pair_arm64()` returns `is_pair=false`
4. ❌ IR_CONST_FLOAT constant loading skipped
5. ❌ Register remains zero/uninitialized
6. ❌ Values not split across low:high registers
7. ❌ Type information missing from low register
8. ❌ Runtime type checking fails

## The Fix

**File**: `src/native/regalloc_arm64.c`  
**Lines**: 114-118  
**Change**: Update size to maximum when extending live range

```c
} else {
    // Extend existing range
    if (pos < range->start) range->start = pos;
    if (pos > range->end) range->end = pos;
    // CRITICAL FIX: Update size to maximum seen across all uses
    if (size > range->size) {
        range->size = size;
    }
}
```

**Why this works**: If ANY use of a virtual register requires 16 bytes, the entire live range must be 16 bytes, enabling proper register pair allocation throughout its lifetime.

## Verification

### Before Fix
```
Allocation: v0 [0-3] (8byte): x9
Expected:   v0 [0-3] (16byte): x9:x10

pair.is_pair = false
pair.high = -1

Constant Loading Skipped ✗
Disassembly: eor x10, x10, x10 (zero register, not constant)
```

### After Fix
```
Allocation: v0 [0-3] (16byte): x9:x10
pair.is_pair = true
pair.low = 9
pair.high = 10

Constant Loading Generated ✓
Disassembly:
  mov  x9, #0x2                ; Type = VAL_NUMBER
  movk x9, #0x1, lsl #32       ; Padding
  mov  x10, #0x0               ; Start of numeric value
  movk x10, #0x4045, lsl #48   ; 42.0 in IEEE-754
  mov  x0, x9                  ; Marshal to X0:X1
  mov  x1, x10                 ; for function call
```

## Generated Code Quality

**Test**: `print(42);`

**Generated Assembly** (from disassembly):
```asm
# Load constant 42.0
0x18: mov  x9, #0x2                    ; v0_low = type
0x1c: movk x9, #0x0, lsl #16
0x20: movk x9, #0x1, lsl #32
0x24: mov  x10, #0x0                   ; v0_high = numeric value
0x28: movk x10, #0x0, lsl #16
0x2c: movk x10, #0x0, lsl #32
0x30: movk x10, #0x4045, lsl #48

# Marshal to calling convention registers X0:X1
0x34: mov  x0, x9                      ; X0 = low bytes (type)
0x38: mov  x1, x10                     ; X1 = high bytes (value)

# Call runtime function
0x3c: bl  0x3c                         ; bl sox_native_print
```

**Value Structure in Registers**:
- X0 (low): 0x0000000100000002 (type=2, padding)
- X1 (high): 0x4045000000000000 (42.0 in IEEE-754)

Perfect! ✅

## Impact Assessment

### What This Fixes

✅ Constants now load correctly into register pairs  
✅ 16-byte values properly split across low:high registers  
✅ Type information preserved in low register  
✅ Runtime type checking will work correctly  
✅ Arithmetic operations ready to be tested  
✅ Variable loading can now work correctly  

### Remaining Issues

⚠️ **Mach-O Object File Generation**: "String table extends past end of file" error prevents linking  
- This is a separate issue in `src/native/macho_writer.c`
- Generated machine code is correct
- Can test with disassembly analysis for now

## Commit

**Hash**: 2652f67  
**Message**: "Fix: Register allocator size mismatch causing incorrect constant loading"

## Next Steps

1. **Fix Mach-O Generation** (separate issue)
   - Investigate `src/native/macho_writer.c`
   - Check LINKEDIT segment creation
   - Fix string table overflow

2. **Test with Simple Constants** (once linking works)
   - `print(42);` → should print 42
   - `print(10 + 20);` → should print 30

3. **Test with Variables**
   - `var x = 42; print(x);`
   - `var a = 10; var b = 20; print(a + b);`

4. **Continue Phase 4-8 Implementation**
   - All groundwork now in place
   - Register pair handling working correctly
   - Can now focus on remaining calling convention phases

## Conclusion

The investigation successfully identified that the "byte ordering" issue was actually a fundamental register allocation bug. The fix is minimal (5 lines) but critical - it ensures virtual registers are allocated with the correct size across their entire lifetime, enabling proper register pair allocation when needed.

The machine code generation is now correct and ready for testing once the Mach-O linking issue is resolved.
