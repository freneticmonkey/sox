# Phase 5.1 ELF Executable Writer - Demonstration

## Quick Test

To verify the implementation works correctly, run the unit tests:

```bash
make test 2>&1 | grep -A 15 "elf_executable"
```

Expected output:
```
sox//elf_executable/get_default_entry_options[ OK    ]
sox//elf_executable/find_section_by_type[ OK    ]
sox//elf_executable/calculate_layout [ OK    ]
sox//elf_executable/calculate_layout_multi_section[ OK    ]
sox//elf_executable/generate_entry_point_x64[ OK    ]
sox//elf_executable/generate_entry_point_arm64[ OK    ]
sox//elf_executable/write_executable [ OK    ]
sox//elf_executable/write_executable_with_data[ OK    ]
sox//elf_executable/error_invalid_params[ OK    ]
sox//elf_executable/error_no_main    [ OK    ]
sox//elf_executable/error_no_text    [ OK    ]
```

## Manual Verification

The `test_write_executable` test creates a temporary executable at `/tmp/test_executable`.
To inspect it during test execution, add a sleep or pause after the test.

For manual testing, create a simple program:

```c
#include "native/elf_executable.h"
#include "native/linker_core.h"
#include "lib/memory.h"

int main() {
    // Create context
    linker_context_t* ctx = linker_context_new();
    ctx->target_format = PLATFORM_FORMAT_ELF;
    ctx->base_address = 0x400000;

    // Create .text section
    ctx->merged_section_count = 1;
    ctx->merged_sections = l_mem_alloc(sizeof(linker_section_t));
    linker_section_init(&ctx->merged_sections[0]);

    ctx->merged_sections[0].name = "text";
    ctx->merged_sections[0].type = SECTION_TYPE_TEXT;
    ctx->merged_sections[0].alignment = 16;

    // Simple code: mov rax, 42; ret
    uint8_t code[] = {0x48, 0xC7, 0xC0, 0x2A, 0x00, 0x00, 0x00, 0xC3};
    ctx->merged_sections[0].size = sizeof(code);
    ctx->merged_sections[0].data = l_mem_alloc(sizeof(code));
    memcpy(ctx->merged_sections[0].data, code, sizeof(code));

    // Create main symbol
    ctx->global_symbol_count = 1;
    ctx->global_symbols = l_mem_alloc(sizeof(linker_symbol_t));
    linker_symbol_init(&ctx->global_symbols[0]);

    ctx->global_symbols[0].name = "main";
    ctx->global_symbols[0].type = SYMBOL_TYPE_FUNC;
    ctx->global_symbols[0].binding = SYMBOL_BINDING_GLOBAL;
    ctx->global_symbols[0].is_defined = true;
    ctx->global_symbols[0].section_index = 0;
    ctx->global_symbols[0].value = 0;

    // Calculate layout
    elf_calculate_layout(ctx);

    // Generate entry point
    ctx->global_symbols[0].final_address = ctx->merged_sections[0].vaddr;
    entry_point_options_t opts = elf_get_default_entry_options(EM_X86_64);
    elf_generate_entry_point(ctx, &opts);

    // Write executable
    elf_write_executable("/tmp/demo_program", ctx);

    printf("Executable created: /tmp/demo_program\n");

    linker_context_free(ctx);
    return 0;
}
```

## Verification Commands

Once an executable is created, verify it:

```bash
# Check ELF header
readelf -h /tmp/demo_program

# Expected output:
# ELF Header:
#   Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00
#   Class:                             ELF64
#   Data:                              2's complement, little endian
#   Version:                           1 (current)
#   OS/ABI:                            UNIX - System V
#   ABI Version:                       0
#   Type:                              EXEC (Executable file)
#   Machine:                           Advanced Micro Devices X86-64
#   Entry point address:               0x400000

# Check program headers
readelf -l /tmp/demo_program

# Expected output:
# Program Headers:
#   Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
#   LOAD           0x001000 0x0000000000400000 0x0000000000400000 0x00001a 0x00001a R E 0x1000
#   LOAD           0x002000 0x0000000000600000 0x0000000000600000 0x000000 0x000000 RW  0x1000

# Disassemble the code
objdump -d /tmp/demo_program

# Expected output:
# 0000000000400000 <.text>:
#   400000:       48 31 ed                xor    %rbp,%rbp
#   400003:       e8 XX XX XX XX          call   <main>
#   400008:       48 89 c7                mov    %rax,%rdi
#   40000b:       b8 3c 00 00 00          mov    $0x3c,%eax
#   400010:       0f 05                   syscall
#   400012:       48 c7 c0 2a 00 00 00    mov    $0x2a,%rax
#   400019:       c3                      ret

# Run the program
/tmp/demo_program

# Check exit code
echo $?
# Expected: 42
```

## Architecture-Specific Notes

### x86-64
- Entry point: 18 bytes
- Syscall for exit: interrupt 0x80 or syscall instruction
- Syscall number 60 (sys_exit)
- Return value passed in RAX

### ARM64
- Entry point: 16 bytes
- Syscall: svc #0 instruction
- Syscall number 93 (sys_exit) in X8
- Return value passed in X0

## Test Coverage

The implementation includes tests for:

1. **Default Configuration**
   - Architecture-specific defaults
   - Entry point options

2. **Section Management**
   - Finding sections by type
   - Single section layout
   - Multiple section layout with alignment

3. **Entry Point Generation**
   - x86-64 code generation and patching
   - ARM64 code generation and patching
   - Proper call offset calculation

4. **Executable Writing**
   - Basic executable with .text only
   - Executable with .text and .data
   - ELF header correctness
   - Program header correctness
   - File permissions (0755)

5. **Error Handling**
   - NULL parameter validation
   - Missing main symbol
   - Missing .text section

All tests pass with 100% success rate.
