#include "macho_executable.h"
#include "macho_writer.h"
#include "section_layout.h"
#include "symbol_resolver.h"
#include "relocation_processor.h"
#include "instruction_patcher.h"
#include "arm64_encoder.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#ifndef MH_HAS_TLV_DESCRIPTORS
#define MH_HAS_TLV_DESCRIPTORS 0x00800000
#endif

/*
 * Mach-O Executable Writer Implementation
 *
 * This module creates executable Mach-O binaries for macOS/ARM64.
 * It takes the linked sections and symbols from the linker context
 * and generates a complete runnable executable.
 *
 * Phase 5.2: Mach-O Executable Writer
 */

/* Helper: Round up to page boundary (16KB for Mach-O) */
static uint64_t round_up_to_page(uint64_t value, size_t page_size) {
    return align_to(value, page_size);
}

static bool macho_tlv_debug_enabled(void) {
    const char* env = getenv("SOX_MACHO_TLV_DEBUG");
    return env && env[0] != '\0';
}

static bool macho_got_debug_enabled(void) {
    const char* env = getenv("SOX_MACHO_GOT_DEBUG");
    return env && env[0] != '\0';
}

static void macho_dump_bytes(const char* label, const uint8_t* data, size_t size, size_t max) {
    if (!label || !data) {
        return;
    }
    size_t dump_size = size < max ? size : max;
    fprintf(stderr, "[TLV-DEBUG] %s size=%zu dump=%zu:", label, size, dump_size);
    for (size_t i = 0; i < dump_size; i++) {
        fprintf(stderr, " %02x", data[i]);
    }
    fprintf(stderr, "\n");
}

/* Helper: Write structure to file with error checking */
static bool write_struct(FILE* f, const void* data, size_t size) {
    if (fwrite(data, size, 1, f) != 1) {
        fprintf(stderr, "Mach-O error: Failed to write structure to file\n");
        return false;
    }
    return true;
}

/* Helper: Write padding bytes to file */
static bool write_padding(FILE* f, size_t count) {
    if (count == 0) {
        return true;
    }
    uint8_t* padding = calloc(count, 1);
    if (padding == NULL) {
        fprintf(stderr, "Mach-O error: Failed to allocate padding\n");
        return false;
    }
    bool success = (fwrite(padding, count, 1, f) == 1);
    free(padding);
    if (!success) {
        fprintf(stderr, "Mach-O error: Failed to write padding\n");
    }
    return success;
}

typedef struct {
    nlist_64_t* symbols;
    char** names;
    int count;
    int capacity;
    char* strtab;
    size_t strsize;
    size_t strcap;
} macho_symtab_t;

typedef struct {
    relocation_info_t* relocs;
    int count;
    int capacity;
} macho_extrel_t;

typedef struct {
    uint8_t text;
    uint8_t rodata;
    uint8_t data;
    uint8_t tlv;
    uint8_t tdata;
    uint8_t tbss;
    uint8_t bss;
} macho_section_index_map_t;

static int macho_symtab_get_index(macho_symtab_t* table, const char* name);
static bool macho_symtab_add_undef(macho_symtab_t* table, const char* name);

typedef struct {
    char* name;
    char* bind_name;
    int symtab_index;
} macho_stub_symbol_t;

typedef struct {
    macho_stub_symbol_t* items;
    int count;
    int capacity;
} macho_stub_list_t;

typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} macho_byte_buffer_t;

static void macho_stub_list_init(macho_stub_list_t* list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void macho_stub_list_free(macho_stub_list_t* list) {
    if (!list) {
        return;
    }
    for (int i = 0; i < list->count; i++) {
        free(list->items[i].name);
        free(list->items[i].bind_name);
    }
    free(list->items);
}

static const char* macho_strip_underscore(const char* name) {
    if (name && name[0] == '_') {
        return name + 1;
    }
    return name;
}

static bool macho_stub_list_add(macho_stub_list_t* list,
                                macho_symtab_t* symtab,
                                const char* symbol_name) {
    if (!list || !symtab || !symbol_name) {
        return false;
    }

    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].name, symbol_name) == 0) {
            return true;
        }
    }

    int full_len = (int)strlen(symbol_name);
    char* full_name = malloc((size_t)full_len + 2);
    if (!full_name) {
        return false;
    }
    full_name[0] = '_';
    memcpy(full_name + 1, symbol_name, (size_t)full_len + 1);

    int sym_index = macho_symtab_get_index(symtab, full_name);
    if (sym_index < 0) {
        if (!macho_symtab_add_undef(symtab, symbol_name)) {
            free(full_name);
            return false;
        }
        sym_index = macho_symtab_get_index(symtab, full_name);
    }
    if (sym_index < 0) {
        free(full_name);
        return false;
    }

    if (list->capacity < list->count + 1) {
        int new_capacity = list->capacity < 8 ? 8 : list->capacity * 2;
        macho_stub_symbol_t* new_items = realloc(list->items, new_capacity * sizeof(macho_stub_symbol_t));
        if (!new_items) {
            return false;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }

    char* name_copy = strdup(symbol_name);
    if (!name_copy) {
        return false;
    }
    char* bind_copy = strdup(full_name);
    if (!bind_copy) {
        free(full_name);
        free(name_copy);
        return false;
    }
    free(full_name);

    list->items[list->count].name = name_copy;
    list->items[list->count].bind_name = bind_copy;
    list->items[list->count].symtab_index = sym_index;
    list->count++;

    return true;
}

static void macho_buffer_init(macho_byte_buffer_t* buffer) {
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
}

static void macho_buffer_free(macho_byte_buffer_t* buffer) {
    free(buffer->data);
}

static bool macho_buffer_reserve(macho_byte_buffer_t* buffer, size_t needed) {
    if (buffer->capacity >= needed) {
        return true;
    }
    size_t new_capacity = buffer->capacity < 64 ? 64 : buffer->capacity * 2;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    uint8_t* new_data = realloc(buffer->data, new_capacity);
    if (!new_data) {
        return false;
    }
    buffer->data = new_data;
    buffer->capacity = new_capacity;
    return true;
}

static bool macho_buffer_append_byte(macho_byte_buffer_t* buffer, uint8_t value) {
    if (!macho_buffer_reserve(buffer, buffer->size + 1)) {
        return false;
    }
    buffer->data[buffer->size++] = value;
    return true;
}

static bool macho_buffer_append_bytes(macho_byte_buffer_t* buffer, const uint8_t* data, size_t size) {
    if (!macho_buffer_reserve(buffer, buffer->size + size)) {
        return false;
    }
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return true;
}

static bool macho_buffer_append_uleb(macho_byte_buffer_t* buffer, uint64_t value) {
    do {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        if (!macho_buffer_append_byte(buffer, byte)) {
            return false;
        }
    } while (value != 0);
    return true;
}

static bool macho_buffer_append_cstring(macho_byte_buffer_t* buffer, const char* str) {
    size_t len = strlen(str) + 1;
    return macho_buffer_append_bytes(buffer, (const uint8_t*)str, len);
}

static int macho_count_external_call_symbols(linker_context_t* context) {
    if (!context) {
        return 0;
    }
    int count = 0;
    int capacity = 0;
    const char** names = NULL;

    for (int obj_idx = 0; obj_idx < context->object_count; obj_idx++) {
        linker_object_t* obj = context->objects[obj_idx];
        if (!obj) {
            continue;
        }
        for (int reloc_idx = 0; reloc_idx < obj->relocation_count; reloc_idx++) {
            linker_relocation_t* reloc = &obj->relocations[reloc_idx];
            if (reloc->type != RELOC_ARM64_CALL26 && reloc->type != RELOC_ARM64_JUMP26) {
                continue;
            }
            if (reloc->symbol_index < 0 || reloc->symbol_index >= obj->symbol_count) {
                continue;
            }
            linker_symbol_t* symbol = &obj->symbols[reloc->symbol_index];
            if (!symbol->name || symbol->name[0] == '\0') {
                continue;
            }
            if (symbol->defining_object != -1) {
                continue;
            }

            bool exists = false;
            for (int i = 0; i < count; i++) {
                if (strcmp(names[i], symbol->name) == 0) {
                    exists = true;
                    break;
                }
            }
            if (exists) {
                continue;
            }
            if (capacity < count + 1) {
                int new_capacity = capacity < 8 ? 8 : capacity * 2;
                const char** new_names = realloc(names, new_capacity * sizeof(const char*));
                if (!new_names) {
                    free(names);
                    return count;
                }
                names = new_names;
                capacity = new_capacity;
            }
            names[count++] = symbol->name;
        }
    }

    free(names);
    return count;
}

static int macho_count_external_got_symbols(linker_context_t* context) {
    if (!context) {
        return 0;
    }
    int count = 0;
    int capacity = 0;
    const char** names = NULL;

    for (int obj_idx = 0; obj_idx < context->object_count; obj_idx++) {
        linker_object_t* obj = context->objects[obj_idx];
        if (!obj) {
            continue;
        }
        for (int reloc_idx = 0; reloc_idx < obj->relocation_count; reloc_idx++) {
            linker_relocation_t* reloc = &obj->relocations[reloc_idx];
            if (reloc->type != RELOC_ARM64_GOT_LOAD_PAGE21 &&
                reloc->type != RELOC_ARM64_GOT_LOAD_PAGEOFF12) {
                continue;
            }
            if (reloc->symbol_index < 0 || reloc->symbol_index >= obj->symbol_count) {
                continue;
            }
            linker_symbol_t* symbol = &obj->symbols[reloc->symbol_index];
            if (!symbol->name || symbol->name[0] == '\0') {
                continue;
            }
            if (symbol->defining_object != -1) {
                continue;
            }

            bool exists = false;
            for (int i = 0; i < count; i++) {
                if (strcmp(names[i], symbol->name) == 0) {
                    exists = true;
                    break;
                }
            }
            if (exists) {
                continue;
            }
            if (capacity < count + 1) {
                int new_capacity = capacity < 8 ? 8 : capacity * 2;
                const char** new_names = realloc(names, new_capacity * sizeof(const char*));
                if (!new_names) {
                    free(names);
                    return count;
                }
                names = new_names;
                capacity = new_capacity;
            }
            names[count++] = symbol->name;
        }
    }

    free(names);
    return count;
}

static bool macho_stub_list_contains(const macho_stub_list_t* list, const char* name) {
    if (!list || !name) {
        return false;
    }
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static int macho_stub_list_index(const macho_stub_list_t* list, const char* name) {
    if (!list || !name) {
        return -1;
    }
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static linker_symbol_t* macho_resolve_defined_symbol(linker_context_t* context,
                                                     linker_symbol_t* symbol) {
    if (!context || !symbol) {
        return NULL;
    }
    if (symbol->is_defined) {
        return symbol;
    }
    if (symbol->defining_object < 0 || symbol->defining_object >= context->object_count) {
        return NULL;
    }
    linker_object_t* def_obj = context->objects[symbol->defining_object];
    if (!def_obj || !symbol->name) {
        return NULL;
    }
    for (int i = 0; i < def_obj->symbol_count; i++) {
        linker_symbol_t* candidate = &def_obj->symbols[i];
        if (!candidate->is_defined || !candidate->name) {
            continue;
        }
        if (strcmp(candidate->name, symbol->name) == 0) {
            return candidate;
        }
    }
    return NULL;
}

#define BIND_OPCODE_DONE 0x00
#define BIND_OPCODE_SET_DYLIB_ORDINAL_IMM 0x10
#define BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM 0x40
#define BIND_OPCODE_SET_TYPE_IMM 0x50
#define BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB 0x70
#define BIND_OPCODE_DO_BIND 0x90
#define BIND_TYPE_POINTER 1

static bool macho_build_bind_info(macho_byte_buffer_t* buffer,
                                  const macho_stub_list_t* stubs,
                                  uint8_t segment_index,
                                  uint64_t got_offset) {
    if (!buffer || !stubs || stubs->count == 0) {
        return true;
    }

    if (!macho_buffer_append_byte(buffer, BIND_OPCODE_SET_DYLIB_ORDINAL_IMM | 1)) {
        return false;
    }
    if (!macho_buffer_append_byte(buffer, BIND_OPCODE_SET_TYPE_IMM | BIND_TYPE_POINTER)) {
        return false;
    }

    for (int i = 0; i < stubs->count; i++) {
        if (!macho_buffer_append_byte(buffer, BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM)) {
            return false;
        }
        if (!macho_buffer_append_cstring(buffer, stubs->items[i].bind_name)) {
            return false;
        }
        if (!macho_buffer_append_byte(buffer, BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | segment_index)) {
            return false;
        }
        if (!macho_buffer_append_uleb(buffer, got_offset + (uint64_t)i * 8)) {
            return false;
        }
        if (!macho_buffer_append_byte(buffer, BIND_OPCODE_DO_BIND)) {
            return false;
        }
    }

    return true;
}

static bool macho_append_bind_start(macho_byte_buffer_t* buffer) {
    if (!buffer) {
        return false;
    }
    if (buffer->size > 0) {
        return true;
    }
    if (!macho_buffer_append_byte(buffer, BIND_OPCODE_SET_DYLIB_ORDINAL_IMM | 1)) {
        return false;
    }
    if (!macho_buffer_append_byte(buffer, BIND_OPCODE_SET_TYPE_IMM | BIND_TYPE_POINTER)) {
        return false;
    }
    return true;
}

static bool macho_append_bind_entry(macho_byte_buffer_t* buffer,
                                    const char* symbol_name,
                                    uint8_t segment_index,
                                    uint64_t segment_offset) {
    if (!buffer || !symbol_name) {
        return false;
    }
    if (!macho_buffer_append_byte(buffer, BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM)) {
        return false;
    }
    if (!macho_buffer_append_cstring(buffer, symbol_name)) {
        return false;
    }
    if (!macho_buffer_append_byte(buffer, BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | segment_index)) {
        return false;
    }
    if (!macho_buffer_append_uleb(buffer, segment_offset)) {
        return false;
    }
    if (!macho_buffer_append_byte(buffer, BIND_OPCODE_DO_BIND)) {
        return false;
    }
    return true;
}

static bool macho_append_bind_entry_with_type(macho_byte_buffer_t* buffer,
                                              const char* symbol_name,
                                              uint8_t segment_index,
                                              uint64_t segment_offset,
                                              uint8_t type) {
    if (!buffer || !symbol_name) {
        return false;
    }
    if (!macho_buffer_append_byte(buffer, BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM)) {
        return false;
    }
    if (!macho_buffer_append_cstring(buffer, symbol_name)) {
        return false;
    }
    if (!macho_buffer_append_byte(buffer, BIND_OPCODE_SET_TYPE_IMM | type)) {
        return false;
    }
    if (!macho_buffer_append_byte(buffer, BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | segment_index)) {
        return false;
    }
    if (!macho_buffer_append_uleb(buffer, segment_offset)) {
        return false;
    }
    if (!macho_buffer_append_byte(buffer, BIND_OPCODE_DO_BIND)) {
        return false;
    }
    return true;
}

static bool macho_append_tlv_binds(macho_byte_buffer_t* buffer,
                                   linker_context_t* context,
                                   uint8_t data_segment_index,
                                   uint64_t tlv_vm_addr) {
    if (!buffer || !context) {
        return true;
    }

    for (int obj_idx = 0; obj_idx < context->object_count; obj_idx++) {
        linker_object_t* obj = context->objects[obj_idx];
        if (!obj || !obj->section_base_addrs) {
            continue;
        }
        for (int reloc_idx = 0; reloc_idx < obj->relocation_count; reloc_idx++) {
            linker_relocation_t* reloc = &obj->relocations[reloc_idx];
            if (reloc->symbol_index < 0 || reloc->symbol_index >= obj->symbol_count) {
                continue;
            }
            if (reloc->section_index < 0 || reloc->section_index >= obj->section_count) {
                continue;
            }
            linker_symbol_t* symbol = &obj->symbols[reloc->symbol_index];
            if (!symbol->name || symbol->name[0] == '\0') {
                continue;
            }
            if (symbol->defining_object != -1) {
                continue;
            }
            const char* sym_name = symbol->name;
            if (strcmp(sym_name, "_tlv_bootstrap") != 0 && strcmp(sym_name, "tlv_bootstrap") != 0) {
                continue;
            }
            linker_section_t* obj_section = &obj->sections[reloc->section_index];
            if (obj_section->type != SECTION_TYPE_TLV) {
                continue;
            }

            uint64_t bind_addr = tlv_vm_addr + reloc->offset;
            uint64_t segment_offset = bind_addr - tlv_vm_addr;

            char name_buf[256];
            snprintf(name_buf, sizeof(name_buf), "_%s", sym_name);

            if (!macho_append_bind_start(buffer)) {
                return false;
            }
            if (!macho_append_bind_entry_with_type(buffer, name_buf, data_segment_index,
                                                   segment_offset, BIND_TYPE_POINTER)) {
                return false;
            }
        }
    }

    return true;
}

static void macho_emit_stub(uint8_t* dst,
                            uint64_t stub_addr,
                            uint64_t got_addr) {
    uint64_t stub_page = stub_addr & ~0xfffull;
    uint64_t got_page = got_addr & ~0xfffull;
    int64_t page_delta = (int64_t)((got_page - stub_page) >> 12);
    uint32_t imm = (uint32_t)page_delta & 0x1fffff;
    uint32_t immlo = imm & 0x3;
    uint32_t immhi = (imm >> 2) & 0x7ffff;
    uint32_t adrp = 0x90000000 | (immlo << 29) | (immhi << 5) | ARM64_X16;

    uint32_t page_off = (uint32_t)(got_addr & 0xfff);
    uint32_t ldr_off = (page_off / 8) & 0xfff;
    uint32_t ldr = 0xF9400000 | (ldr_off << 10) | (ARM64_X16 << 5) | ARM64_X16;
    uint32_t br = 0xD61F0000 | (ARM64_X16 << 5);

    memcpy(dst + 0, &adrp, sizeof(adrp));
    memcpy(dst + 4, &ldr, sizeof(ldr));
    memcpy(dst + 8, &br, sizeof(br));
}

static void macho_symtab_init(macho_symtab_t* table) {
    table->symbols = NULL;
    table->names = NULL;
    table->count = 0;
    table->capacity = 0;
    table->strtab = (char*)malloc(1);
    table->strsize = 1;
    table->strcap = 1;
    if (table->strtab) {
        table->strtab[0] = '\0';
    }
}

static void macho_symtab_free(macho_symtab_t* table) {
    if (!table) {
        return;
    }
    if (table->names) {
        for (int i = 0; i < table->count; i++) {
            free(table->names[i]);
        }
        free(table->names);
    }
    free(table->symbols);
    free(table->strtab);
}

static bool macho_symtab_has_name(macho_symtab_t* table, const char* name) {
    for (int i = 0; i < table->count; i++) {
        if (strcmp(table->names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static int macho_symtab_get_index(macho_symtab_t* table, const char* name) {
    for (int i = 0; i < table->count; i++) {
        if (strcmp(table->names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static bool macho_symtab_add_undef(macho_symtab_t* table, const char* name) {
    if (!table || !name || name[0] == '\0') {
        return false;
    }

    const char* base = name;
    size_t base_len = strlen(base);
    size_t full_len = base_len + 1;
    char* full_name = (char*)malloc(full_len + 1);
    if (!full_name) {
        return false;
    }

    full_name[0] = '_';
    memcpy(full_name + 1, base, base_len + 1);

    if (macho_symtab_has_name(table, full_name)) {
        free(full_name);
        return true;
    }

    if (table->capacity < table->count + 1) {
        int old_capacity = table->capacity;
        int new_capacity = old_capacity < 16 ? 16 : old_capacity * 2;
        nlist_64_t* new_syms = realloc(table->symbols, new_capacity * sizeof(nlist_64_t));
        char** new_names = realloc(table->names, new_capacity * sizeof(char*));
        if (!new_syms || !new_names) {
            free(new_syms);
            free(new_names);
            free(full_name);
            return false;
        }
        table->symbols = new_syms;
        table->names = new_names;
        table->capacity = new_capacity;
    }

    size_t needed = table->strsize + full_len + 1;
    if (needed > table->strcap) {
        size_t new_cap = table->strcap < 64 ? 64 : table->strcap * 2;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        char* new_strtab = realloc(table->strtab, new_cap);
        if (!new_strtab) {
            free(full_name);
            return false;
        }
        table->strtab = new_strtab;
        table->strcap = new_cap;
    }

    uint32_t strx = (uint32_t)table->strsize;
    memcpy(table->strtab + table->strsize, full_name, full_len + 1);
    table->strsize += full_len + 1;

    nlist_64_t* sym = &table->symbols[table->count];
    sym->n_strx = strx;
    sym->n_type = N_UNDF | N_EXT;
    sym->n_sect = 0;
    sym->n_desc = 0;
    sym->n_value = 0;

    table->names[table->count] = full_name;
    table->count++;
    return true;
}

static bool macho_symtab_add_defined(macho_symtab_t* table,
                                     const char* name,
                                     uint8_t n_sect,
                                     uint64_t value) {
    if (!table || !name || name[0] == '\0') {
        return false;
    }

    const char* base = name;
    size_t base_len = strlen(base);
    size_t full_len = base_len + 1;
    char* full_name = (char*)malloc(full_len + 1);
    if (!full_name) {
        return false;
    }

    full_name[0] = '_';
    memcpy(full_name + 1, base, base_len + 1);

    if (macho_symtab_has_name(table, full_name)) {
        free(full_name);
        return true;
    }

    if (table->capacity < table->count + 1) {
        int old_capacity = table->capacity;
        int new_capacity = old_capacity < 16 ? 16 : old_capacity * 2;
        nlist_64_t* new_syms = realloc(table->symbols, new_capacity * sizeof(nlist_64_t));
        char** new_names = realloc(table->names, new_capacity * sizeof(char*));
        if (!new_syms || !new_names) {
            free(new_syms);
            free(new_names);
            free(full_name);
            return false;
        }
        table->symbols = new_syms;
        table->names = new_names;
        table->capacity = new_capacity;
    }

    size_t needed = table->strsize + full_len + 1;
    if (needed > table->strcap) {
        size_t new_cap = table->strcap < 64 ? 64 : table->strcap * 2;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        char* new_strtab = realloc(table->strtab, new_cap);
        if (!new_strtab) {
            free(full_name);
            return false;
        }
        table->strtab = new_strtab;
        table->strcap = new_cap;
    }

    uint32_t strx = (uint32_t)table->strsize;
    memcpy(table->strtab + table->strsize, full_name, full_len + 1);
    table->strsize += full_len + 1;

    nlist_64_t* sym = &table->symbols[table->count];
    sym->n_strx = strx;
    sym->n_type = N_SECT | N_EXT;
    sym->n_sect = n_sect;
    sym->n_desc = 0;
    sym->n_value = value;

    table->names[table->count] = full_name;
    table->count++;
    return true;
}

static void macho_extrel_init(macho_extrel_t* table) {
    table->relocs = NULL;
    table->count = 0;
    table->capacity = 0;
}

static void macho_extrel_free(macho_extrel_t* table) {
    free(table->relocs);
}

static bool macho_extrel_add(macho_extrel_t* table, relocation_info_t reloc) {
    if (table->capacity < table->count + 1) {
        int old_capacity = table->capacity;
        int new_capacity = old_capacity < 32 ? 32 : old_capacity * 2;
        relocation_info_t* new_relocs = realloc(table->relocs, new_capacity * sizeof(relocation_info_t));
        if (!new_relocs) {
            return false;
        }
        table->relocs = new_relocs;
        table->capacity = new_capacity;
    }
    table->relocs[table->count++] = reloc;
    return true;
}

static linker_section_t* macho_find_merged_section(linker_context_t* context,
                                                   section_type_t type) {
    if (!context) {
        return NULL;
    }
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == type) {
            return &context->merged_sections[i];
        }
    }
    return NULL;
}

static macho_section_index_map_t macho_compute_section_indices(linker_context_t* context) {
    macho_section_index_map_t map = {0};
    uint8_t index = 1;

    if (macho_find_merged_section(context, SECTION_TYPE_TEXT)) {
        map.text = index++;
    }
    if (macho_find_merged_section(context, SECTION_TYPE_RODATA)) {
        map.rodata = index++;
    }
    if (macho_count_external_call_symbols(context) > 0) {
        index++;
    }
    if (macho_count_external_call_symbols(context) > 0) {
        index++;
    }
    if (macho_find_merged_section(context, SECTION_TYPE_DATA)) {
        map.data = index++;
    }
    if (macho_find_merged_section(context, SECTION_TYPE_TLV)) {
        map.tlv = index++;
    }
    if (macho_find_merged_section(context, SECTION_TYPE_TDATA)) {
        map.tdata = index++;
    }
    if (macho_find_merged_section(context, SECTION_TYPE_TBSS)) {
        map.tbss = index++;
    }
    if (macho_find_merged_section(context, SECTION_TYPE_BSS)) {
        map.bss = index++;
    }

    return map;
}

/**
 * Get section count for a segment
 */
uint32_t macho_get_segment_section_count(linker_context_t* context,
                                          const char* segment_name) {
    if (context == NULL || segment_name == NULL) {
        return 0;
    }

    uint32_t count = 0;
    int stub_count = macho_count_external_call_symbols(context);
    int got_symbol_count = macho_count_external_got_symbols(context);
    int got_count = stub_count + got_symbol_count;

    /* Check for __TEXT segment sections */
    if (strcmp(segment_name, SEG_TEXT) == 0) {
        /* Check for __text section */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_TEXT) {
                count++;
                break;
            }
        }
        /* Check for __const section (rodata) */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_RODATA) {
                count++;
                break;
            }
        }
        if (stub_count > 0) {
            count++;
        }
    }
    /* Check for __DATA segment sections */
    else if (strcmp(segment_name, SEG_DATA) == 0) {
        /* Check for __data section */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_DATA) {
                count++;
                break;
            }
        }
        /* Check for __thread_vars section */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_TLV) {
                count++;
                break;
            }
        }
        /* Check for __thread_data section */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_TDATA) {
                count++;
                break;
            }
        }
        /* Check for __thread_bss section */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_TBSS) {
                count++;
                break;
            }
        }
        /* Check for __bss section */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_BSS) {
                count++;
                break;
            }
        }
    }

    /* Check for __DATA_CONST segment sections */
    else if (strcmp(segment_name, SEG_DATA_CONST) == 0) {
        if (got_count > 0) {
            count = 1;
        }
    }

    return count;
}

/**
 * Calculate segment size
 */
uint64_t macho_calculate_segment_size(linker_context_t* context,
                                       const char* segment_name) {
    if (context == NULL || segment_name == NULL) {
        return 0;
    }

    uint64_t size = 0;
    int stub_count = macho_count_external_call_symbols(context);
    int got_symbol_count = macho_count_external_got_symbols(context);
    int got_count = stub_count + got_symbol_count;
    uint64_t stubs_size = (uint64_t)stub_count * 12;

    /* Calculate size for __TEXT segment */
    if (strcmp(segment_name, SEG_TEXT) == 0) {
        /* Add __text section size */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_TEXT) {
                size += context->merged_sections[i].size;
                break;
            }
        }
        /* Add __const section size (rodata) */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_RODATA) {
                /* Align to 8-byte boundary */
                size = align_to(size, 8);
                size += context->merged_sections[i].size;
                break;
            }
        }
        if (stub_count > 0) {
            size = align_to(size, 4);
            size += stubs_size;
        }
    }
    /* Calculate size for __DATA segment */
    else if (strcmp(segment_name, SEG_DATA) == 0) {
        /* Add __data section size */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_DATA) {
                size += context->merged_sections[i].size;
                break;
            }
        }
        /* Add __thread_vars section size */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_TLV) {
                size = align_to(size, 8);
                size += context->merged_sections[i].size;
                break;
            }
        }
        /* Add __thread_data section size */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_TDATA) {
                size = align_to(size, 8);
                size += context->merged_sections[i].size;
                break;
            }
        }
        /* Add __thread_bss section size */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_TBSS) {
                size = align_to(size, 8);
                size += context->merged_sections[i].size;
                break;
            }
        }
        /* Add __bss section size */
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_BSS) {
                /* Align to 8-byte boundary */
                size = align_to(size, 8);
                size += context->merged_sections[i].size;
                break;
            }
        }
    }
    /* Calculate size for __DATA_CONST segment */
    else if (strcmp(segment_name, SEG_DATA_CONST) == 0) {
        if (got_count > 0) {
            size = align_to((uint64_t)got_count * 8, 8);
        }
    }

    return size;
}

/**
 * Calculate total size of load commands
 */
uint32_t macho_calculate_load_commands_size(linker_context_t* context) {
    if (context == NULL) {
        return 0;
    }

    uint32_t size = 0;
    int stub_count = macho_count_external_call_symbols(context);
    int got_symbol_count = macho_count_external_got_symbols(context);
    int got_count = stub_count + got_symbol_count;

    /* LC_SEGMENT_64 for __TEXT */
    uint32_t text_section_count = macho_get_segment_section_count(context, SEG_TEXT);
    size += sizeof(segment_command_64_t);
    size += text_section_count * sizeof(section_64_t);

    /* LC_SEGMENT_64 for __DATA_CONST (GOT) */
    if (got_count > 0) {
        uint32_t data_const_sections = macho_get_segment_section_count(context, SEG_DATA_CONST);
        size += sizeof(segment_command_64_t);
        size += data_const_sections * sizeof(section_64_t);
    }

    /* LC_SEGMENT_64 for __DATA */
    uint32_t data_section_count = macho_get_segment_section_count(context, SEG_DATA);
    size += sizeof(segment_command_64_t);
    size += data_section_count * sizeof(section_64_t);

    /* LC_MAIN */
    size += sizeof(entry_point_command_t);

    /* LC_LOAD_DYLINKER */
    size_t dyld_path_len = strlen(DYLD_PATH) + 1;
    size_t dyld_cmd_size = sizeof(dylinker_command_t) + dyld_path_len;
    size += align_to(dyld_cmd_size, 8);

    /* LC_LOAD_DYLIB */
    size_t lib_path_len = strlen(LIBSYSTEM_PATH) + 1;
    size_t lib_cmd_size = sizeof(dylib_command_t) + lib_path_len;
    size += align_to(lib_cmd_size, 8);

    /* LC_SYMTAB */
    size += sizeof(symtab_command_t);

    /* LC_DYSYMTAB */
    size += sizeof(dysymtab_command_t);

    /* LC_UUID */
    size += sizeof(uuid_command_t);

    /* LC_BUILD_VERSION */
    size += sizeof(build_version_command_t);

    /* LC_DYLD_INFO_ONLY */
    if (got_count > 0) {
        size += sizeof(dyld_info_command_t);
    }

    /* LC_SEGMENT_64 for __LINKEDIT (no section headers) */
    size += sizeof(segment_command_64_t);

    /* LC_SEGMENT_64 for __PAGEZERO (no section headers) */
    size += sizeof(segment_command_64_t);

    return size;
}

/**
 * Set entry point to _main symbol address
 */
bool macho_set_entry_point(linker_context_t* context) {
    if (context == NULL) {
        fprintf(stderr, "Mach-O error: NULL context passed to macho_set_entry_point\n");
        return false;
    }

    /* Look for _main symbol in global symbols */
    linker_symbol_t* main_sym = NULL;
    for (int i = 0; i < context->global_symbol_count; i++) {
        if (context->global_symbols[i].name != NULL &&
            strcmp(context->global_symbols[i].name, "_main") == 0) {
            main_sym = &context->global_symbols[i];
            break;
        }
    }

    if (main_sym == NULL) {
        fprintf(stderr, "Mach-O error: _main symbol not found\n");
        return false;
    }

    if (!main_sym->is_defined) {
        fprintf(stderr, "Mach-O error: _main symbol is not defined\n");
        return false;
    }

    /* Set entry point to _main's final address */
    context->entry_point = main_sym->final_address;

    return true;
}

/**
 * Write a Mach-O executable file from linker context
 */
bool macho_write_executable(const char* output_path,
                             linker_context_t* context) {
    if (output_path == NULL || context == NULL) {
        fprintf(stderr, "Mach-O error: NULL parameter passed to macho_write_executable\n");
        return false;
    }

    if (context->merged_section_count == 0) {
        fprintf(stderr, "Mach-O error: No sections to write\n");
        return false;
    }

    /* Calculate sizes */
    size_t page_size = get_page_size(PLATFORM_FORMAT_MACH_O);
    int stub_count = macho_count_external_call_symbols(context);
    int got_symbol_count = macho_count_external_got_symbols(context);
    int got_entry_count = stub_count + got_symbol_count;
    uint64_t stubs_size = (uint64_t)stub_count * 12;
    uint64_t got_size = (uint64_t)got_entry_count * 8;
    bool has_stubs = (stub_count > 0);
    bool has_got = (got_entry_count > 0);

    uint32_t load_cmds_size = macho_calculate_load_commands_size(context);
    uint32_t text_section_count = macho_get_segment_section_count(context, SEG_TEXT);
    uint32_t data_const_section_count = macho_get_segment_section_count(context, SEG_DATA_CONST);
    uint32_t data_section_count = macho_get_segment_section_count(context, SEG_DATA);
    uint64_t text_size = macho_calculate_segment_size(context, SEG_TEXT);
    uint64_t data_const_vmsize = macho_calculate_segment_size(context, SEG_DATA_CONST);
    uint64_t data_vmsize = macho_calculate_segment_size(context, SEG_DATA);

    /* Calculate DATA segment file size (exclude BSS/thread BSS which have no file content) */
    uint64_t data_filesize = 0;
    uint64_t data_file_cursor = 0;
    linker_section_t* data_section = macho_find_merged_section(context, SECTION_TYPE_DATA);
    linker_section_t* tlv_section = macho_find_merged_section(context, SECTION_TYPE_TLV);
    linker_section_t* tdata_section = macho_find_merged_section(context, SECTION_TYPE_TDATA);

    if (data_section) {
        data_file_cursor = align_to(data_file_cursor, 8);
        data_file_cursor += data_section->size;
    }
    if (tlv_section) {
        data_file_cursor = align_to(data_file_cursor, 8);
        data_file_cursor += tlv_section->size;
    }
    if (tdata_section) {
        data_file_cursor = align_to(data_file_cursor, 8);
        data_file_cursor += tdata_section->size;
    }
    data_filesize = data_file_cursor;
    uint64_t data_const_filesize = has_got ? align_to(got_size, 8) : 0;

    uint64_t header_size = sizeof(mach_header_64_t);
    uint64_t text_file_offset = round_up_to_page(header_size + load_cmds_size, page_size);
    macho_section_index_map_t section_indices = macho_compute_section_indices(context);

    macho_symtab_t symtab;
    macho_symtab_init(&symtab);

    for (int obj_idx = 0; obj_idx < context->object_count; obj_idx++) {
        linker_object_t* obj = context->objects[obj_idx];
        if (!obj) {
            continue;
        }
        for (int sym_idx = 0; sym_idx < obj->symbol_count; sym_idx++) {
            linker_symbol_t* symbol = &obj->symbols[sym_idx];
            if (symbol->binding == SYMBOL_BINDING_LOCAL) {
                continue;
            }
            if (symbol->name == NULL || symbol->name[0] == '\0') {
                continue;
            }
            if (!symbol->is_defined || symbol->defining_object == -1) {
                continue;
            }
            if (symbol->section_index < 0 || symbol->section_index >= obj->section_count) {
                continue;
            }

            section_type_t type = obj->sections[symbol->section_index].type;
            uint8_t n_sect = 0;
            switch (type) {
                case SECTION_TYPE_TEXT:   n_sect = section_indices.text; break;
                case SECTION_TYPE_RODATA: n_sect = section_indices.rodata; break;
                case SECTION_TYPE_DATA:   n_sect = section_indices.data; break;
                case SECTION_TYPE_TLV:    n_sect = section_indices.tlv; break;
                case SECTION_TYPE_TDATA:  n_sect = section_indices.tdata; break;
                case SECTION_TYPE_TBSS:   n_sect = section_indices.tbss; break;
                case SECTION_TYPE_BSS:    n_sect = section_indices.bss; break;
                default:                  n_sect = 0; break;
            }

            if (n_sect == 0) {
                continue;
            }

            uint64_t value = symbol->final_address;
            if (type == SECTION_TYPE_TEXT || type == SECTION_TYPE_RODATA) {
                value += text_file_offset;
            }

            if (!macho_symtab_add_defined(&symtab, symbol->name, n_sect, value)) {
                fprintf(stderr, "Mach-O error: Failed to record defined symbol\n");
                macho_symtab_free(&symtab);
                return false;
            }
        }
    }

    for (int obj_idx = 0; obj_idx < context->object_count; obj_idx++) {
        linker_object_t* obj = context->objects[obj_idx];
        if (!obj) {
            continue;
        }
        for (int sym_idx = 0; sym_idx < obj->symbol_count; sym_idx++) {
            linker_symbol_t* symbol = &obj->symbols[sym_idx];
            if (symbol->binding == SYMBOL_BINDING_LOCAL) {
                continue;
            }
            if (symbol->name == NULL || symbol->name[0] == '\0') {
                continue;
            }
            if (symbol->defining_object != -1) {
                continue;
            }
            if (!macho_symtab_add_undef(&symtab, symbol->name)) {
                fprintf(stderr, "Mach-O error: Failed to record undefined symbol\n");
                macho_symtab_free(&symtab);
                return false;
            }
        }
    }

    macho_stub_list_t stubs;
    macho_stub_list_init(&stubs);
    macho_stub_list_t got_entries;
    macho_stub_list_init(&got_entries);

    if (stub_count > 0) {
        for (int obj_idx = 0; obj_idx < context->object_count; obj_idx++) {
            linker_object_t* obj = context->objects[obj_idx];
            if (!obj) {
                continue;
            }
            for (int reloc_idx = 0; reloc_idx < obj->relocation_count; reloc_idx++) {
                linker_relocation_t* reloc = &obj->relocations[reloc_idx];
                if (reloc->type != RELOC_ARM64_CALL26 &&
                    reloc->type != RELOC_ARM64_JUMP26) {
                    continue;
                }
                if (reloc->symbol_index < 0 || reloc->symbol_index >= obj->symbol_count) {
                    continue;
                }
                linker_symbol_t* symbol = &obj->symbols[reloc->symbol_index];
                if (!symbol->name || symbol->name[0] == '\0') {
                    continue;
                }
                if (symbol->defining_object != -1) {
                    continue;
                }
                if (!macho_stub_list_add(&stubs, &symtab, symbol->name)) {
                    fprintf(stderr, "Mach-O error: Failed to add stub for %s\n", symbol->name);
                    macho_symtab_free(&symtab);
                    macho_stub_list_free(&stubs);
                    macho_stub_list_free(&got_entries);
                    return false;
                }
            }
        }
    }

    for (int obj_idx = 0; obj_idx < context->object_count; obj_idx++) {
        linker_object_t* obj = context->objects[obj_idx];
        if (!obj) {
            continue;
        }
        for (int reloc_idx = 0; reloc_idx < obj->relocation_count; reloc_idx++) {
            linker_relocation_t* reloc = &obj->relocations[reloc_idx];
            if (reloc->type != RELOC_ARM64_GOT_LOAD_PAGE21 &&
                reloc->type != RELOC_ARM64_GOT_LOAD_PAGEOFF12) {
                continue;
            }
            if (reloc->symbol_index < 0 || reloc->symbol_index >= obj->symbol_count) {
                continue;
            }
            linker_symbol_t* symbol = &obj->symbols[reloc->symbol_index];
            if (!symbol->name || symbol->name[0] == '\0') {
                continue;
            }
            if (symbol->defining_object != -1) {
                continue;
            }
            if (!macho_stub_list_add(&got_entries, &symtab, symbol->name)) {
                fprintf(stderr, "Mach-O error: Failed to add GOT entry for %s\n", symbol->name);
                macho_symtab_free(&symtab);
                macho_stub_list_free(&stubs);
                macho_stub_list_free(&got_entries);
                return false;
            }
        }
    }

    uint32_t stubs_indirect_index = 0;
    uint32_t got_indirect_index = 0;
    uint32_t indirect_count = 0;
    uint32_t got_entry_total = (uint32_t)stubs.count + (uint32_t)got_entries.count;
    if (got_entry_total > 0) {
        stubs_indirect_index = 0;
        got_indirect_index = has_stubs ? (uint32_t)stubs.count : 0;
        indirect_count = (has_stubs ? (uint32_t)stubs.count : 0) + got_entry_total;
    }
    int defined_count = 0;
    int undef_count = 0;

    macho_extrel_t extrel;
    macho_extrel_init(&extrel);

    for (int obj_idx = 0; obj_idx < context->object_count; obj_idx++) {
        linker_object_t* obj = context->objects[obj_idx];
        if (!obj) {
            continue;
        }
        for (int reloc_idx = 0; reloc_idx < obj->relocation_count; reloc_idx++) {
            linker_relocation_t* reloc = &obj->relocations[reloc_idx];
            if (reloc->symbol_index < 0 || reloc->symbol_index >= obj->symbol_count) {
                continue;
            }
            linker_symbol_t* symbol = &obj->symbols[reloc->symbol_index];
            if (symbol->binding == SYMBOL_BINDING_LOCAL) {
                continue;
            }
            if (symbol->name == NULL || symbol->name[0] == '\0') {
                continue;
            }
            if ((reloc->type == RELOC_ARM64_CALL26 || reloc->type == RELOC_ARM64_JUMP26) &&
                symbol->defining_object == -1 &&
                macho_stub_list_contains(&stubs, symbol->name)) {
                continue;
            }
            if (symbol->defining_object != -1) {
                continue;
            }
            if (reloc->section_index < 0 || reloc->section_index >= obj->section_count) {
                continue;
            }
            linker_section_t* obj_section = &obj->sections[reloc->section_index];
            linker_section_t* merged = NULL;
            for (int i = 0; i < context->merged_section_count; i++) {
                if (context->merged_sections[i].type == obj_section->type) {
                    merged = &context->merged_sections[i];
                    break;
                }
            }
            if (!merged || !obj->section_base_addrs) {
                continue;
            }

            const char* base_name = symbol->name ? symbol->name : "";
            size_t base_len = strlen(base_name);
            size_t full_len = base_len + (base_name[0] == '_' ? 0 : 1);
            char* full_name = (char*)malloc(full_len + 1);
            if (!full_name) {
                macho_symtab_free(&symtab);
                macho_extrel_free(&extrel);
                return false;
            }
            if (base_name[0] == '_') {
                memcpy(full_name, base_name, base_len + 1);
            } else {
                full_name[0] = '_';
                memcpy(full_name + 1, base_name, base_len + 1);
            }
            int sym_index = macho_symtab_get_index(&symtab, full_name);
            if (sym_index < 0 && symbol->is_defined &&
                symbol->section_index >= 0 &&
                symbol->section_index < obj->section_count) {
                section_type_t type = obj->sections[symbol->section_index].type;
                uint8_t n_sect = 0;
                switch (type) {
                    case SECTION_TYPE_TEXT:   n_sect = section_indices.text; break;
                    case SECTION_TYPE_RODATA: n_sect = section_indices.rodata; break;
                    case SECTION_TYPE_DATA:   n_sect = section_indices.data; break;
                    case SECTION_TYPE_TLV:    n_sect = section_indices.tlv; break;
                    case SECTION_TYPE_TDATA:  n_sect = section_indices.tdata; break;
                    case SECTION_TYPE_TBSS:   n_sect = section_indices.tbss; break;
                    case SECTION_TYPE_BSS:    n_sect = section_indices.bss; break;
                    default:                  n_sect = 0; break;
                }
                if (n_sect != 0) {
                    uint64_t value = symbol->final_address;
                    if (type == SECTION_TYPE_TEXT || type == SECTION_TYPE_RODATA) {
                        value += text_file_offset;
                    }
                    if (macho_symtab_add_defined(&symtab, symbol->name, n_sect, value)) {
                        sym_index = macho_symtab_get_index(&symtab, full_name);
                    }
                }
            }
            free(full_name);
            if (sym_index < 0) {
                continue;
            }

            uint64_t section_base = obj->section_base_addrs[reloc->section_index];
            uint64_t section_offset = section_base - merged->vaddr;
            uint64_t reloc_offset = section_offset + reloc->offset;

            uint32_t macho_type = 0;
            bool is_pcrel = false;
            uint32_t length = 2;
            switch (reloc->type) {
                case RELOC_ARM64_ABS64:
                    macho_type = 0;
                    is_pcrel = false;
                    length = 3;
                    break;
                case RELOC_ARM64_CALL26:
                case RELOC_ARM64_JUMP26:
                    macho_type = 2;
                    is_pcrel = true;
                    length = 2;
                    break;
                case RELOC_ARM64_ADR_PREL_PG_HI21:
                    macho_type = 3;
                    is_pcrel = true;
                    length = 2;
                    break;
                case RELOC_ARM64_ADD_ABS_LO12_NC:
                    macho_type = 4;
                    is_pcrel = false;
                    length = 2;
                    break;
                case RELOC_ARM64_GOT_LOAD_PAGE21:
                    macho_type = 5;
                    is_pcrel = true;
                    length = 2;
                    break;
                case RELOC_ARM64_GOT_LOAD_PAGEOFF12:
                    macho_type = 6;
                    is_pcrel = false;
                    length = 2;
                    break;
                case RELOC_ARM64_TLVP_LOAD_PAGE21:
                    macho_type = 8;
                    is_pcrel = true;
                    length = 2;
                    break;
                case RELOC_ARM64_TLVP_LOAD_PAGEOFF12:
                    macho_type = 9;
                    is_pcrel = false;
                    length = 2;
                    break;
                default:
                    continue;
            }

            relocation_info_t info;
            info.r_address = (int32_t)reloc_offset;
            info.r_info = (sym_index & 0x00ffffff) |
                          ((is_pcrel ? 1u : 0u) << 24) |
                          ((length & 0x3u) << 25) |
                          (1u << 27) |
                          ((macho_type & 0xfu) << 28);

            if (!macho_extrel_add(&extrel, info)) {
                macho_symtab_free(&symtab);
                macho_extrel_free(&extrel);
                return false;
            }
        }
    }

    for (int i = 0; i < symtab.count; i++) {
        if ((symtab.symbols[i].n_type & N_SECT) == N_SECT) {
            defined_count++;
        } else {
            undef_count++;
        }
    }

    bool has_got_entries = (stubs.count + got_entries.count) > 0;
    uint8_t data_const_segment_index = has_got_entries ? 2 : 0;

    macho_byte_buffer_t bind_info;
    macho_buffer_init(&bind_info);
    if (has_got_entries) {
        if (has_stubs &&
            !macho_build_bind_info(&bind_info, &stubs, data_const_segment_index, 0)) {
            fprintf(stderr, "Mach-O error: Failed to build bind info\n");
            macho_buffer_free(&bind_info);
            macho_symtab_free(&symtab);
            macho_extrel_free(&extrel);
            macho_stub_list_free(&stubs);
            macho_stub_list_free(&got_entries);
            return false;
        }
        if (got_entries.count > 0) {
            uint64_t got_offset = (uint64_t)stubs.count * 8;
            if (!macho_build_bind_info(&bind_info, &got_entries, data_const_segment_index, got_offset)) {
                fprintf(stderr, "Mach-O error: Failed to build GOT bind info\n");
                macho_buffer_free(&bind_info);
                macho_symtab_free(&symtab);
                macho_extrel_free(&extrel);
                macho_stub_list_free(&stubs);
                macho_stub_list_free(&got_entries);
                return false;
            }
        }
    }

    macho_byte_buffer_t export_info;
    macho_buffer_init(&export_info);
    if (has_got_entries) {
        if (!macho_buffer_append_byte(&export_info, 0x00)) {
            fprintf(stderr, "Mach-O error: Failed to build export info\n");
            macho_buffer_free(&export_info);
            macho_buffer_free(&bind_info);
            macho_symtab_free(&symtab);
            macho_extrel_free(&extrel);
            macho_stub_list_free(&stubs);
            macho_stub_list_free(&got_entries);
            return false;
        }
    }

    uint32_t* indirect_symbols = NULL;
    if (indirect_count > 0) {
        indirect_symbols = calloc(indirect_count, sizeof(uint32_t));
        if (!indirect_symbols) {
            fprintf(stderr, "Mach-O error: Failed to allocate indirect symbol table\n");
            macho_buffer_free(&export_info);
            macho_buffer_free(&bind_info);
            macho_symtab_free(&symtab);
            macho_extrel_free(&extrel);
            macho_stub_list_free(&stubs);
            macho_stub_list_free(&got_entries);
            return false;
        }
        uint32_t cursor = 0;
        if (has_stubs) {
            for (int i = 0; i < stubs.count; i++) {
                indirect_symbols[cursor++] = (uint32_t)stubs.items[i].symtab_index;
            }
        }
        for (int i = 0; i < stubs.count; i++) {
            indirect_symbols[cursor++] = (uint32_t)stubs.items[i].symtab_index;
        }
        for (int i = 0; i < got_entries.count; i++) {
            indirect_symbols[cursor++] = (uint32_t)got_entries.items[i].symtab_index;
        }
    }

    /* Calculate file offsets */

    /* Fix section virtual addresses to account for __TEXT segment headers
     * The __TEXT segment starts at base_address in VM, but the actual code sections
     * are offset by text_file_offset to account for Mach-O header and load commands */
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_TEXT ||
            context->merged_sections[i].type == SECTION_TYPE_RODATA) {
            /* Adjust vaddr to account for headers before code in __TEXT segment */
            context->merged_sections[i].vaddr += text_file_offset;
        }
    }

    /* Also adjust entry point to match the adjusted section addresses */
    if (context->entry_point != 0) {
        context->entry_point += text_file_offset;
    }

    /* Calculate virtual addresses */
    uint64_t base_addr = context->base_address;
    if (base_addr == 0) {
        base_addr = get_default_base_address(PLATFORM_FORMAT_MACH_O);
    }
    uint64_t text_vm_addr = base_addr;

    uint64_t stubs_offset = 0;
    uint64_t stubs_addr = 0;
    uint64_t got_addr = 0;
    uint8_t* stubs_data = NULL;

    if (has_stubs) {
        uint64_t cursor = 0;
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_TEXT) {
                cursor += context->merged_sections[i].size;
                break;
            }
        }
        for (int i = 0; i < context->merged_section_count; i++) {
            if (context->merged_sections[i].type == SECTION_TYPE_RODATA) {
                cursor = align_to(cursor, 8);
                cursor += context->merged_sections[i].size;
                break;
            }
        }
        cursor = align_to(cursor, 4);
        stubs_offset = cursor;
        stubs_addr = text_vm_addr + text_file_offset + stubs_offset;
    }

    /* Compute actual __TEXT segment filesize based on section addresses */
    uint64_t text_filesize = 0;
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_TEXT ||
            context->merged_sections[i].type == SECTION_TYPE_RODATA) {
            uint64_t end = (context->merged_sections[i].vaddr - text_vm_addr) +
                           context->merged_sections[i].size;
            if (end > text_filesize) {
                text_filesize = end;
            }
        }
    }
    if (has_stubs) {
        uint64_t stubs_end = text_file_offset + stubs_offset + stubs_size;
        if (stubs_end > text_filesize) {
            text_filesize = stubs_end;
        }
    }

    uint64_t data_const_file_offset = round_up_to_page(text_filesize, page_size);
    uint64_t data_file_offset = data_const_file_offset + round_up_to_page(data_const_filesize, page_size);
    uint64_t linkedit_file_offset = data_file_offset + round_up_to_page(data_filesize, page_size);

    uint64_t data_const_vm_addr = text_vm_addr + data_const_file_offset;
    uint64_t data_vm_addr = text_vm_addr + data_file_offset;
    uint64_t linkedit_vm_addr = data_vm_addr + round_up_to_page(data_vmsize, page_size);

    if (has_got) {
        got_addr = data_const_vm_addr;
    }

    if (has_stubs) {
        stubs_data = calloc(stubs_size, 1);
        if (!stubs_data) {
            fprintf(stderr, "Mach-O error: Failed to allocate stub section\n");
            free(indirect_symbols);
            macho_buffer_free(&export_info);
            macho_buffer_free(&bind_info);
            macho_symtab_free(&symtab);
            macho_extrel_free(&extrel);
            macho_stub_list_free(&stubs);
            macho_stub_list_free(&got_entries);
            return false;
        }
        for (int i = 0; i < stubs.count; i++) {
            uint64_t stub_addr = stubs_addr + (uint64_t)i * 12;
            uint64_t got_entry_addr = got_addr + (uint64_t)i * 8;
            macho_emit_stub(stubs_data + (size_t)i * 12, stub_addr, got_entry_addr);
        }

        for (int obj_idx = 0; obj_idx < context->object_count; obj_idx++) {
            linker_object_t* obj = context->objects[obj_idx];
            if (!obj || !obj->section_base_addrs) {
                continue;
            }
            for (int reloc_idx = 0; reloc_idx < obj->relocation_count; reloc_idx++) {
                linker_relocation_t* reloc = &obj->relocations[reloc_idx];
                if (reloc->type != RELOC_ARM64_CALL26 &&
                    reloc->type != RELOC_ARM64_JUMP26) {
                    continue;
                }
                if (reloc->symbol_index < 0 || reloc->symbol_index >= obj->symbol_count) {
                    continue;
                }
                linker_symbol_t* symbol = &obj->symbols[reloc->symbol_index];
                if (!symbol->name || symbol->name[0] == '\0') {
                    continue;
                }
                if (symbol->defining_object != -1) {
                    continue;
                }
                int stub_index = macho_stub_list_index(&stubs, symbol->name);
                if (stub_index < 0) {
                    continue;
                }

                if (reloc->section_index < 0 || reloc->section_index >= obj->section_count) {
                    continue;
                }
                linker_section_t* obj_section = &obj->sections[reloc->section_index];
                linker_section_t* merged = macho_find_merged_section(context, obj_section->type);
                if (!merged || !merged->data) {
                    continue;
                }

                uint64_t section_base = obj->section_base_addrs[reloc->section_index];
                if (obj_section->type == SECTION_TYPE_TEXT ||
                    obj_section->type == SECTION_TYPE_RODATA) {
                    section_base += text_file_offset;
                }
                uint64_t P = section_base + reloc->offset;
                uint64_t offset_in_merged = P - merged->vaddr;
                if (offset_in_merged >= merged->size) {
                    continue;
                }

                uint64_t stub_addr = stubs_addr + (uint64_t)stub_index * 12;
                int64_t value = (int64_t)((stub_addr + (uint64_t)reloc->addend) - P);
                if (!patch_instruction(merged->data, merged->size, offset_in_merged,
                                       value, reloc->type, P)) {
                    fprintf(stderr, "Mach-O warning: Failed to patch call to stub for %s\n",
                            symbol->name);
                }
            }
        }
    }

    if (has_got) {
        for (int obj_idx = 0; obj_idx < context->object_count; obj_idx++) {
            linker_object_t* obj = context->objects[obj_idx];
            if (!obj || !obj->section_base_addrs) {
                continue;
            }
            for (int reloc_idx = 0; reloc_idx < obj->relocation_count; reloc_idx++) {
                linker_relocation_t* reloc = &obj->relocations[reloc_idx];
                if (reloc->type != RELOC_ARM64_GOT_LOAD_PAGE21 &&
                    reloc->type != RELOC_ARM64_GOT_LOAD_PAGEOFF12) {
                    continue;
                }
                if (reloc->symbol_index < 0 || reloc->symbol_index >= obj->symbol_count) {
                    continue;
                }
                linker_symbol_t* symbol = &obj->symbols[reloc->symbol_index];
                if (!symbol->name || symbol->name[0] == '\0') {
                    continue;
                }
                if (symbol->defining_object != -1) {
                    continue;
                }

                int got_index = -1;
                int stub_index = macho_stub_list_index(&stubs, symbol->name);
                if (stub_index >= 0) {
                    got_index = stub_index;
                } else {
                    int data_index = macho_stub_list_index(&got_entries, symbol->name);
                    if (data_index >= 0) {
                        got_index = stubs.count + data_index;
                    }
                }
                if (got_index < 0) {
                    if (macho_got_debug_enabled()) {
                        fprintf(stderr,
                                "[GOT-DEBUG] Missing GOT entry for %s (obj=%d reloc=%d)\n",
                                symbol->name ? symbol->name : "<unknown>",
                                obj_idx, reloc_idx);
                    }
                    continue;
                }

                if (reloc->section_index < 0 || reloc->section_index >= obj->section_count) {
                    continue;
                }
                linker_section_t* obj_section = &obj->sections[reloc->section_index];
                linker_section_t* merged = macho_find_merged_section(context, obj_section->type);
                if (!merged || !merged->data) {
                    continue;
                }

                uint64_t section_base = obj->section_base_addrs[reloc->section_index];
                if (obj_section->type == SECTION_TYPE_TEXT ||
                    obj_section->type == SECTION_TYPE_RODATA) {
                    section_base += text_file_offset;
                }
                uint64_t P = section_base + reloc->offset;
                uint64_t offset_in_merged = P - merged->vaddr;
                if (offset_in_merged >= merged->size) {
                    continue;
                }

                uint64_t got_entry_addr = got_addr + (uint64_t)got_index * 8;
                int64_t value = relocation_calculate_value(reloc->type, got_entry_addr,
                                                           reloc->addend, P);
                if (macho_got_debug_enabled()) {
                    fprintf(stderr,
                            "[GOT-DEBUG] sym=%s type=%s obj=%d reloc=%d P=0x%llx got=0x%llx addend=0x%llx value=0x%llx\n",
                            symbol->name ? symbol->name : "<unknown>",
                            relocation_type_name(reloc->type),
                            obj_idx,
                            reloc_idx,
                            (unsigned long long)P,
                            (unsigned long long)got_entry_addr,
                            (unsigned long long)reloc->addend,
                            (unsigned long long)value);
                }
                if (!patch_instruction(merged->data, merged->size, offset_in_merged,
                                       value, reloc->type, P)) {
                    fprintf(stderr, "Mach-O warning: Failed to patch GOT relocation for %s\n",
                            symbol->name);
                }
            }
        }
    }

    linker_section_t* data_sections[] = {
        macho_find_merged_section(context, SECTION_TYPE_DATA),
        macho_find_merged_section(context, SECTION_TYPE_TLV),
        macho_find_merged_section(context, SECTION_TYPE_TDATA),
        macho_find_merged_section(context, SECTION_TYPE_TBSS),
        macho_find_merged_section(context, SECTION_TYPE_BSS)
    };

    uint64_t data_offsets[SECTION_TYPE_TBSS + 1] = {0};
    bool data_present[SECTION_TYPE_TBSS + 1] = {0};
    uint64_t data_cursor = 0;

    for (size_t i = 0; i < sizeof(data_sections) / sizeof(data_sections[0]); i++) {
        linker_section_t* section = data_sections[i];
        if (!section || section->size == 0) {
            continue;
        }
        data_cursor = align_to(data_cursor, 8);
        data_offsets[section->type] = data_cursor;
        data_present[section->type] = true;
        data_cursor += section->size;
    }

    uint64_t data_deltas[SECTION_TYPE_TBSS + 1] = {0};
    for (size_t i = 0; i < sizeof(data_sections) / sizeof(data_sections[0]); i++) {
        linker_section_t* section = data_sections[i];
        if (!section || !data_present[section->type]) {
            continue;
        }
        uint64_t final_addr = data_vm_addr + data_offsets[section->type];
        if (final_addr >= section->vaddr) {
            data_deltas[section->type] = final_addr - section->vaddr;
        }
    }

    uint64_t tlv_vm_addr = 0;
    uint64_t tlv_layout_addr = 0;
    uint64_t tlv_delta = 0;
    if (data_present[SECTION_TYPE_TLV]) {
        tlv_vm_addr = data_vm_addr + data_offsets[SECTION_TYPE_TLV];
        linker_section_t* tlv_section_layout = macho_find_merged_section(context, SECTION_TYPE_TLV);
        if (tlv_section_layout) {
            tlv_layout_addr = tlv_section_layout->vaddr;
        }
        if (tlv_vm_addr != 0 && tlv_layout_addr != 0 && tlv_vm_addr != tlv_layout_addr) {
            tlv_delta = tlv_vm_addr - tlv_layout_addr;
        }
    }

    uint64_t tls_block_base = 0;
    if (data_present[SECTION_TYPE_TDATA]) {
        tls_block_base = data_vm_addr + data_offsets[SECTION_TYPE_TDATA];
    } else if (data_present[SECTION_TYPE_TBSS]) {
        tls_block_base = data_vm_addr + data_offsets[SECTION_TYPE_TBSS];
    }

    if (tlv_vm_addr != 0) {
        if (!macho_append_tlv_binds(&bind_info, context,
                                    has_got ? 3 : 2, tlv_vm_addr)) {
            fprintf(stderr, "Mach-O error: Failed to build TLV bind info\n");
            macho_buffer_free(&bind_info);
            macho_buffer_free(&export_info);
            macho_symtab_free(&symtab);
            macho_extrel_free(&extrel);
            macho_stub_list_free(&stubs);
            macho_stub_list_free(&got_entries);
            free(indirect_symbols);
            return false;
        }
    }

    if (macho_tlv_debug_enabled()) {
        linker_section_t* tlv_debug_section = macho_find_merged_section(context, SECTION_TYPE_TLV);
        if (tlv_debug_section) {
            fprintf(stderr,
                    "[TLV-DEBUG] __thread_vars vaddr=0x%llx vm_addr=0x%llx size=%zu delta=0x%llx\n",
                    (unsigned long long)tlv_debug_section->vaddr,
                    (unsigned long long)tlv_vm_addr,
                    tlv_debug_section->size,
                    (unsigned long long)tlv_delta);
            if (tlv_debug_section->data && tlv_debug_section->size > 0) {
                macho_dump_bytes("__thread_vars", tlv_debug_section->data,
                                 tlv_debug_section->size, 32);
            }
        }
        if (bind_info.size > 0) {
            macho_dump_bytes("bind_info", bind_info.data, bind_info.size, 64);
        }
    }

    if (tlv_delta != 0) {
        for (int i = 0; i < symtab.count; i++) {
            if (symtab.symbols[i].n_sect == section_indices.tlv) {
                symtab.symbols[i].n_value += tlv_delta;
            }
        }
    }

    for (int obj_idx = 0; obj_idx < context->object_count; obj_idx++) {
        linker_object_t* obj = context->objects[obj_idx];
        if (!obj || !obj->section_base_addrs) {
            continue;
        }
        for (int reloc_idx = 0; reloc_idx < obj->relocation_count; reloc_idx++) {
            linker_relocation_t* reloc = &obj->relocations[reloc_idx];
            if (reloc->symbol_index < 0 || reloc->symbol_index >= obj->symbol_count) {
                continue;
            }
            linker_symbol_t* symbol = &obj->symbols[reloc->symbol_index];
            linker_symbol_t* def_sym = macho_resolve_defined_symbol(context, symbol);
            if (!def_sym || !def_sym->is_defined) {
                continue;
            }
            if (def_sym->section_index < 0 ||
                def_sym->defining_object < 0 ||
                def_sym->defining_object >= context->object_count) {
                continue;
            }
            linker_object_t* def_obj = context->objects[def_sym->defining_object];
            if (!def_obj || def_sym->section_index >= def_obj->section_count) {
                continue;
            }
            section_type_t sym_type = def_obj->sections[def_sym->section_index].type;
            if (sym_type > SECTION_TYPE_TBSS || !data_present[sym_type]) {
                continue;
            }
            if (reloc->section_index < 0 || reloc->section_index >= obj->section_count) {
                continue;
            }
            if (reloc->type != RELOC_ARM64_ABS64 &&
                reloc->type != RELOC_ARM64_TLVP_LOAD_PAGE21 &&
                reloc->type != RELOC_ARM64_TLVP_LOAD_PAGEOFF12) {
                continue;
            }

            linker_section_t* src_section = &obj->sections[reloc->section_index];
            linker_section_t* merged = macho_find_merged_section(context, src_section->type);
            if (!merged || !merged->data) {
                continue;
            }
            uint64_t section_base = obj->section_base_addrs[reloc->section_index];
            if (src_section->type == SECTION_TYPE_TEXT || src_section->type == SECTION_TYPE_RODATA) {
                section_base += text_file_offset;
            }
            uint64_t P = section_base + reloc->offset;
            uint64_t offset_in_merged = P - merged->vaddr;
            if (offset_in_merged >= merged->size) {
                continue;
            }

            uint64_t delta = data_deltas[sym_type];
            if (sym_type == SECTION_TYPE_TLV &&
                (reloc->type == RELOC_ARM64_TLVP_LOAD_PAGE21 ||
                 reloc->type == RELOC_ARM64_TLVP_LOAD_PAGEOFF12) &&
                tlv_delta != 0) {
                delta = tlv_delta;
            }
            if (delta == 0) {
                continue;
            }

            uint64_t S = def_sym->final_address + delta;
            int64_t value = 0;
            if (reloc->type == RELOC_ARM64_ABS64 &&
                src_section->type == SECTION_TYPE_TLV &&
                (sym_type == SECTION_TYPE_TDATA || sym_type == SECTION_TYPE_TBSS) &&
                tls_block_base != 0) {
                uint64_t abs_addr = S;
                uint64_t offset = abs_addr - tls_block_base;
                value = (int64_t)(offset + (uint64_t)reloc->addend);
                if (macho_tlv_debug_enabled()) {
                    fprintf(stderr,
                            "[TLV-DEBUG] TLS OFFSET sym=%s abs=0x%llx base=0x%llx offset=0x%llx addend=0x%llx\n",
                            def_sym->name ? def_sym->name : "<unknown>",
                            (unsigned long long)abs_addr,
                            (unsigned long long)tls_block_base,
                            (unsigned long long)offset,
                            (unsigned long long)reloc->addend);
                }
            } else {
                value = relocation_calculate_value(reloc->type, S, reloc->addend, P);
            }
            if (tlv_delta != 0 &&
                (reloc->type == RELOC_ARM64_TLVP_LOAD_PAGE21 ||
                 reloc->type == RELOC_ARM64_TLVP_LOAD_PAGEOFF12)) {
                const char* debug_env = getenv("SOX_MACHO_TLV_DEBUG");
                if (debug_env && debug_env[0] != '\0') {
                    fprintf(stderr,
                            "[TLV-REPATCH] sym=%s type=%d P=0x%llx S=0x%llx delta=0x%llx addend=0x%llx value=0x%llx\n",
                            def_sym->name ? def_sym->name : "<unknown>",
                            reloc->type,
                            (unsigned long long)P,
                            (unsigned long long)S,
                            (unsigned long long)delta,
                            (unsigned long long)reloc->addend,
                            (unsigned long long)value);
                }
            }
            if (!patch_instruction(merged->data, merged->size, offset_in_merged,
                                   value, reloc->type, P)) {
                fprintf(stderr, "Mach-O warning: Failed to patch data relocation for %s\n",
                        def_sym->name ? def_sym->name : "<unknown>");
            }
        }
    }

    if (bind_info.size > 0) {
        if (!macho_buffer_append_byte(&bind_info, BIND_OPCODE_DONE)) {
            fprintf(stderr, "Mach-O error: Failed to finalize bind info\n");
            macho_buffer_free(&bind_info);
            macho_buffer_free(&export_info);
            macho_symtab_free(&symtab);
            macho_extrel_free(&extrel);
            macho_stub_list_free(&stubs);
            macho_stub_list_free(&got_entries);
            free(indirect_symbols);
            return false;
        }
    }

    uint64_t rebase_size = 0;
    uint64_t bind_size = bind_info.size;
    uint64_t lazy_bind_size = 0;
    uint64_t export_size = export_info.size;

    uint64_t extrel_size = (uint64_t)extrel.count * sizeof(relocation_info_t);
    uint64_t symtab_size = (uint64_t)symtab.count * sizeof(nlist_64_t);
    uint64_t strtab_size = (uint64_t)symtab.strsize;
    uint64_t indirect_size = (uint64_t)indirect_count * sizeof(uint32_t);
    uint64_t linkedit_size = align_to(rebase_size + bind_size + lazy_bind_size + export_size +
                                      extrel_size + symtab_size + indirect_size + strtab_size, 8);

    uint64_t rebase_off = linkedit_file_offset;
    uint64_t bind_off = align_to(rebase_off + rebase_size, 8);
    uint64_t lazy_bind_off = align_to(bind_off + bind_size, 8);
    uint64_t export_off = align_to(lazy_bind_off + lazy_bind_size, 8);
    uint64_t linkedit_cursor = align_to(export_off + export_size, 8);
    uint64_t extrel_off = linkedit_cursor;
    linkedit_cursor = align_to(extrel_off + extrel_size, 8);
    uint64_t symtab_off = linkedit_cursor;
    linkedit_cursor = align_to(symtab_off + symtab_size, 8);
    uint64_t indirect_off = linkedit_cursor;
    linkedit_cursor = align_to(indirect_off + indirect_size, 8);
    uint64_t strtab_off = linkedit_cursor;
    linkedit_cursor = align_to(strtab_off + strtab_size, 8);
    linkedit_size = align_to(linkedit_cursor - linkedit_file_offset, 8);

    /* Create Mach-O header */
    mach_header_64_t header = {0};
    header.magic = MH_MAGIC_64;
    header.cputype = CPU_TYPE_ARM64;
    header.cpusubtype = CPU_SUBTYPE_ARM64_ALL;
    header.filetype = MH_EXECUTE;
    header.ncmds = has_got ? 13 : 11;  /* Add __DATA_CONST + LC_DYLD_INFO_ONLY when GOT is present */
    header.sizeofcmds = load_cmds_size;
    header.flags = MH_DYLDLINK | MH_TWOLEVEL | MH_PIE;
    if (macho_find_merged_section(context, SECTION_TYPE_TLV)) {
        header.flags |= MH_HAS_TLV_DESCRIPTORS;
    }
    header.reserved = 0;

    /* Open output file */
    FILE* f = fopen(output_path, "wb");
    if (f == NULL) {
        fprintf(stderr, "Mach-O error: Failed to open output file: %s\n", output_path);
        return false;
    }

    /* Write Mach-O header */
    if (!write_struct(f, &header, sizeof(header))) {
        fclose(f);
        return false;
    }

    /* Create and write LC_SEGMENT_64 for __PAGEZERO */
    segment_command_64_t pagezero_segment = {0};
    pagezero_segment.cmd = LC_SEGMENT_64;
    pagezero_segment.cmdsize = sizeof(segment_command_64_t);  /* No section headers */
    strncpy(pagezero_segment.segname, "__PAGEZERO", 16);
    pagezero_segment.vmaddr = 0x0;
    pagezero_segment.vmsize = 0x100000000ULL;  /* 4GB on 64-bit */
    pagezero_segment.fileoff = 0;
    pagezero_segment.filesize = 0;
    pagezero_segment.maxprot = VM_PROT_NONE;
    pagezero_segment.initprot = VM_PROT_NONE;
    pagezero_segment.nsects = 0;  /* No section headers */
    pagezero_segment.flags = 0;

    if (!write_struct(f, &pagezero_segment, sizeof(pagezero_segment))) {
        fclose(f);
        return false;
    }

    /* Create and write LC_SEGMENT_64 for __TEXT */
    segment_command_64_t text_segment = {0};
    text_segment.cmd = LC_SEGMENT_64;
    text_segment.cmdsize = sizeof(segment_command_64_t) +
                           text_section_count * sizeof(section_64_t);
    strncpy(text_segment.segname, SEG_TEXT, 16);
    text_segment.vmaddr = text_vm_addr;
    text_segment.vmsize = round_up_to_page(text_filesize, page_size);
    /* __TEXT segment starts at file offset 0 and includes headers + load commands */
    text_segment.fileoff = 0;
    text_segment.filesize = text_filesize;
    text_segment.maxprot = VM_PROT_READ | VM_PROT_EXECUTE;
    text_segment.initprot = VM_PROT_READ | VM_PROT_EXECUTE;
    text_segment.nsects = text_section_count;
    text_segment.flags = 0;

    if (!write_struct(f, &text_segment, sizeof(text_segment))) {
        fclose(f);
        return false;
    }

    /* Write __text section header */
    uint64_t current_offset = 0;
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_TEXT) {
            section_64_t text_sect = {0};
            strncpy(text_sect.sectname, SECT_TEXT, 16);
            strncpy(text_sect.segname, SEG_TEXT, 16);
            /* Use vaddr from merged section (already adjusted for headers) */
            text_sect.addr = context->merged_sections[i].vaddr;
            text_sect.size = context->merged_sections[i].size;
            text_sect.offset = (uint32_t)(text_sect.addr - text_vm_addr);
            text_sect.align = 4;  /* 2^4 = 16 bytes for ARM64 */
            text_sect.reloff = 0;
            text_sect.nreloc = 0;
            text_sect.flags = S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS;
            text_sect.reserved1 = 0;
            text_sect.reserved2 = 0;
            text_sect.reserved3 = 0;

            if (!write_struct(f, &text_sect, sizeof(text_sect))) {
                fclose(f);
                return false;
            }

            current_offset += context->merged_sections[i].size;
            break;
        }
    }

    /* Write __const section header (rodata) */
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_RODATA) {
            current_offset = align_to(current_offset, 8);
            section_64_t const_sect = {0};
            strncpy(const_sect.sectname, SECT_CONST, 16);
            strncpy(const_sect.segname, SEG_TEXT, 16);
            /* Use vaddr from merged section (already adjusted for headers) */
            const_sect.addr = context->merged_sections[i].vaddr;
            const_sect.size = context->merged_sections[i].size;
            const_sect.offset = (uint32_t)(const_sect.addr - text_vm_addr);
            const_sect.align = 3;  /* 2^3 = 8 bytes */
            const_sect.reloff = 0;
            const_sect.nreloc = 0;
            const_sect.flags = S_REGULAR;
            const_sect.reserved1 = 0;
            const_sect.reserved2 = 0;
            const_sect.reserved3 = 0;

            if (!write_struct(f, &const_sect, sizeof(const_sect))) {
                fclose(f);
                return false;
            }
            current_offset += context->merged_sections[i].size;
            break;
        }
    }

    /* Write __stubs section header */
    if (has_stubs) {
        current_offset = align_to(current_offset, 4);
        section_64_t stubs_sect = {0};
        strncpy(stubs_sect.sectname, SECT_STUBS, 16);
        strncpy(stubs_sect.segname, SEG_TEXT, 16);
        stubs_sect.addr = text_vm_addr + text_file_offset + stubs_offset;
        stubs_sect.size = stubs_size;
        stubs_sect.offset = (uint32_t)(stubs_sect.addr - text_vm_addr);
        stubs_sect.align = 2;  /* 2^2 = 4 bytes */
        stubs_sect.reloff = 0;
        stubs_sect.nreloc = 0;
        stubs_sect.flags = S_SYMBOL_STUBS | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS;
        stubs_sect.reserved1 = stubs_indirect_index;
        stubs_sect.reserved2 = 12;
        stubs_sect.reserved3 = 0;

        if (!write_struct(f, &stubs_sect, sizeof(stubs_sect))) {
            fclose(f);
            return false;
        }
        current_offset += stubs_size;
    }

    /* Create and write LC_SEGMENT_64 for __DATA_CONST (GOT) */
    if (has_got) {
        segment_command_64_t data_const_segment = {0};
        data_const_segment.cmd = LC_SEGMENT_64;
        data_const_segment.cmdsize = sizeof(segment_command_64_t) +
                                     data_const_section_count * sizeof(section_64_t);
        strncpy(data_const_segment.segname, SEG_DATA_CONST, 16);
        data_const_segment.vmaddr = data_const_vm_addr;
        data_const_segment.vmsize = round_up_to_page(data_const_vmsize, page_size);
        data_const_segment.fileoff = data_const_file_offset;
        data_const_segment.filesize = data_const_filesize;
        data_const_segment.maxprot = VM_PROT_READ | VM_PROT_WRITE;
        data_const_segment.initprot = VM_PROT_READ | VM_PROT_WRITE;
        data_const_segment.nsects = data_const_section_count;
        data_const_segment.flags = SG_READ_ONLY;

        if (!write_struct(f, &data_const_segment, sizeof(data_const_segment))) {
            fclose(f);
            return false;
        }

        section_64_t got_sect = {0};
        strncpy(got_sect.sectname, SECT_GOT, 16);
        strncpy(got_sect.segname, SEG_DATA_CONST, 16);
        got_sect.addr = data_const_vm_addr;
        got_sect.size = got_size;
        got_sect.offset = (uint32_t)data_const_file_offset;
        got_sect.align = 3;  /* 2^3 = 8 bytes */
        got_sect.reloff = 0;
        got_sect.nreloc = 0;
        got_sect.flags = S_NON_LAZY_SYMBOL_POINTERS;
        got_sect.reserved1 = got_indirect_index;
        got_sect.reserved2 = 0;
        got_sect.reserved3 = 0;

        if (!write_struct(f, &got_sect, sizeof(got_sect))) {
            fclose(f);
            return false;
        }
    }

    /* Create and write LC_SEGMENT_64 for __DATA */
    segment_command_64_t data_segment = {0};
    data_segment.cmd = LC_SEGMENT_64;
    data_segment.cmdsize = sizeof(segment_command_64_t) +
                           data_section_count * sizeof(section_64_t);
    strncpy(data_segment.segname, SEG_DATA, 16);
    data_segment.vmaddr = data_vm_addr;
    data_segment.vmsize = round_up_to_page(data_vmsize, page_size);
    data_segment.fileoff = data_file_offset;
    data_segment.filesize = data_filesize;  /* Exclude BSS from file size */
    data_segment.maxprot = VM_PROT_READ | VM_PROT_WRITE;
    data_segment.initprot = VM_PROT_READ | VM_PROT_WRITE;
    data_segment.nsects = data_section_count;
    data_segment.flags = 0;

    if (!write_struct(f, &data_segment, sizeof(data_segment))) {
        fclose(f);
        return false;
    }

    /* Write __data section header */
    uint64_t data_vm_offset = 0;
    uint64_t data_file_cursor_hdr = 0;
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_DATA) {
            data_vm_offset = align_to(data_vm_offset, 8);
            data_file_cursor_hdr = align_to(data_file_cursor_hdr, 8);
            section_64_t data_sect = {0};
            strncpy(data_sect.sectname, SECT_DATA, 16);
            strncpy(data_sect.segname, SEG_DATA, 16);
            data_sect.addr = data_vm_addr + data_vm_offset;
            data_sect.size = context->merged_sections[i].size;
            data_sect.offset = (uint32_t)(data_file_offset + data_file_cursor_hdr);
            data_sect.align = 3;  /* 2^3 = 8 bytes */
            data_sect.reloff = 0;
            data_sect.nreloc = 0;
            data_sect.flags = S_REGULAR;
            data_sect.reserved1 = 0;
            data_sect.reserved2 = 0;
            data_sect.reserved3 = 0;

            if (!write_struct(f, &data_sect, sizeof(data_sect))) {
                fclose(f);
                return false;
            }

            data_vm_offset += context->merged_sections[i].size;
            data_file_cursor_hdr += context->merged_sections[i].size;
            break;
        }
    }

    /* Write __thread_vars section header */
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_TLV) {
            data_vm_offset = align_to(data_vm_offset, 8);
            data_file_cursor_hdr = align_to(data_file_cursor_hdr, 8);
            section_64_t tlv_sect = {0};
            strncpy(tlv_sect.sectname, SECT_TLV, 16);
            strncpy(tlv_sect.segname, SEG_DATA, 16);
            tlv_sect.addr = data_vm_addr + data_vm_offset;
            tlv_sect.size = context->merged_sections[i].size;
            tlv_sect.offset = (uint32_t)(data_file_offset + data_file_cursor_hdr);
            tlv_sect.align = 3;  /* 2^3 = 8 bytes */
            tlv_sect.reloff = 0;
            tlv_sect.nreloc = 0;
            tlv_sect.flags = S_THREAD_LOCAL_VARIABLES;
            tlv_sect.reserved1 = 0;
            tlv_sect.reserved2 = 0;
            tlv_sect.reserved3 = 0;

            if (!write_struct(f, &tlv_sect, sizeof(tlv_sect))) {
                fclose(f);
                return false;
            }

            data_vm_offset += context->merged_sections[i].size;
            data_file_cursor_hdr += context->merged_sections[i].size;
            break;
        }
    }

    /* Write __thread_data section header */
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_TDATA) {
            data_vm_offset = align_to(data_vm_offset, 8);
            data_file_cursor_hdr = align_to(data_file_cursor_hdr, 8);
            section_64_t tdata_sect = {0};
            strncpy(tdata_sect.sectname, SECT_TDATA, 16);
            strncpy(tdata_sect.segname, SEG_DATA, 16);
            tdata_sect.addr = data_vm_addr + data_vm_offset;
            tdata_sect.size = context->merged_sections[i].size;
            tdata_sect.offset = (uint32_t)(data_file_offset + data_file_cursor_hdr);
            tdata_sect.align = 3;  /* 2^3 = 8 bytes */
            tdata_sect.reloff = 0;
            tdata_sect.nreloc = 0;
            tdata_sect.flags = S_THREAD_LOCAL_REGULAR;
            tdata_sect.reserved1 = 0;
            tdata_sect.reserved2 = 0;
            tdata_sect.reserved3 = 0;

            if (!write_struct(f, &tdata_sect, sizeof(tdata_sect))) {
                fclose(f);
                return false;
            }

            data_vm_offset += context->merged_sections[i].size;
            data_file_cursor_hdr += context->merged_sections[i].size;
            break;
        }
    }

    /* Write __thread_bss section header */
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_TBSS) {
            data_vm_offset = align_to(data_vm_offset, 8);
            section_64_t tbss_sect = {0};
            strncpy(tbss_sect.sectname, SECT_TBSS, 16);
            strncpy(tbss_sect.segname, SEG_DATA, 16);
            tbss_sect.addr = data_vm_addr + data_vm_offset;
            tbss_sect.size = context->merged_sections[i].size;
            tbss_sect.offset = 0;  /* Thread BSS has no file content */
            tbss_sect.align = 3;  /* 2^3 = 8 bytes */
            tbss_sect.reloff = 0;
            tbss_sect.nreloc = 0;
            tbss_sect.flags = S_THREAD_LOCAL_ZEROFILL;
            tbss_sect.reserved1 = 0;
            tbss_sect.reserved2 = 0;
            tbss_sect.reserved3 = 0;

            if (!write_struct(f, &tbss_sect, sizeof(tbss_sect))) {
                fclose(f);
                return false;
            }

            data_vm_offset += context->merged_sections[i].size;
            break;
        }
    }

    /* Write __bss section header */
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_BSS) {
            data_vm_offset = align_to(data_vm_offset, 8);
            section_64_t bss_sect = {0};
            strncpy(bss_sect.sectname, SECT_BSS, 16);
            strncpy(bss_sect.segname, SEG_DATA, 16);
            bss_sect.addr = data_vm_addr + data_vm_offset;
            bss_sect.size = context->merged_sections[i].size;
            bss_sect.offset = 0;  /* BSS has no file content */
            bss_sect.align = 3;  /* 2^3 = 8 bytes */
            bss_sect.reloff = 0;
            bss_sect.nreloc = 0;
            bss_sect.flags = S_ZEROFILL;
            bss_sect.reserved1 = 0;
            bss_sect.reserved2 = 0;
            bss_sect.reserved3 = 0;

            if (!write_struct(f, &bss_sect, sizeof(bss_sect))) {
                fclose(f);
                return false;
            }
            break;
        }
    }

    /* Create and write LC_SEGMENT_64 for __LINKEDIT */
    segment_command_64_t linkedit_segment = {0};
    linkedit_segment.cmd = LC_SEGMENT_64;
    linkedit_segment.cmdsize = sizeof(segment_command_64_t);  /* No section headers */
    strncpy(linkedit_segment.segname, "__LINKEDIT", 16);
    linkedit_segment.vmaddr = linkedit_vm_addr;
    linkedit_segment.vmsize = round_up_to_page(linkedit_size, page_size);
    linkedit_segment.fileoff = linkedit_file_offset;
    linkedit_segment.filesize = linkedit_size;
    linkedit_segment.maxprot = VM_PROT_READ;
    linkedit_segment.initprot = VM_PROT_READ;
    linkedit_segment.nsects = 0;  /* No section headers */
    linkedit_segment.flags = 0;

    if (!write_struct(f, &linkedit_segment, sizeof(linkedit_segment))) {
        fclose(f);
        return false;
    }

    /* Create and write LC_MAIN */
    entry_point_command_t main_cmd = {0};
    main_cmd.cmd = LC_MAIN;
    main_cmd.cmdsize = sizeof(entry_point_command_t);
    /* Calculate entry point as file offset from __TEXT segment start (file offset 0)
     * entry_point is virtual address, need to convert to file offset */
    uint64_t entry_virt_offset = context->entry_point - text_vm_addr;
    /* Entry offset is relative to __TEXT segment start (fileoff=0), not section start */
    main_cmd.entryoff = entry_virt_offset;
    main_cmd.stacksize = 0;  /* Use default */

    if (!write_struct(f, &main_cmd, sizeof(main_cmd))) {
        fclose(f);
        return false;
    }

    /* Create and write LC_LOAD_DYLINKER */
    dylinker_command_t dyld_cmd = {0};
    dyld_cmd.cmd = LC_LOAD_DYLINKER;
    size_t dyld_path_len = strlen(DYLD_PATH) + 1;
    size_t dyld_cmd_size = sizeof(dylinker_command_t) + dyld_path_len;
    dyld_cmd.cmdsize = (uint32_t)align_to(dyld_cmd_size, 8);
    dyld_cmd.name_offset = sizeof(dylinker_command_t);

    if (!write_struct(f, &dyld_cmd, sizeof(dyld_cmd))) {
        fclose(f);
        return false;
    }

    /* Write dylinker path */
    if (fwrite(DYLD_PATH, dyld_path_len, 1, f) != 1) {
        fprintf(stderr, "Mach-O error: Failed to write dylinker path\n");
        fclose(f);
        return false;
    }

    /* Write padding to align dylinker command */
    size_t dyld_padding = dyld_cmd.cmdsize - sizeof(dylinker_command_t) - dyld_path_len;
    if (!write_padding(f, dyld_padding)) {
        fclose(f);
        macho_symtab_free(&symtab);
        return false;
    }

    /* Create and write LC_LOAD_DYLIB */
    dylib_command_t dylib_cmd = {0};
    dylib_cmd.cmd = LC_LOAD_DYLIB;
    size_t lib_path_len = strlen(LIBSYSTEM_PATH) + 1;
    size_t dylib_cmd_size = sizeof(dylib_command_t) + lib_path_len;
    dylib_cmd.cmdsize = (uint32_t)align_to(dylib_cmd_size, 8);
    dylib_cmd.dylib.name_offset = sizeof(dylib_command_t);
    dylib_cmd.dylib.timestamp = 0;
    dylib_cmd.dylib.current_version = 0;
    dylib_cmd.dylib.compatibility_version = 0;

    if (!write_struct(f, &dylib_cmd, sizeof(dylib_cmd))) {
        fclose(f);
        macho_symtab_free(&symtab);
        return false;
    }

    if (fwrite(LIBSYSTEM_PATH, lib_path_len, 1, f) != 1) {
        fprintf(stderr, "Mach-O error: Failed to write dylib path\n");
        fclose(f);
        macho_symtab_free(&symtab);
        return false;
    }

    size_t lib_padding = dylib_cmd.cmdsize - sizeof(dylib_command_t) - lib_path_len;
    if (!write_padding(f, lib_padding)) {
        fclose(f);
        macho_symtab_free(&symtab);
        return false;
    }

    /* Create and write LC_DYLD_INFO_ONLY */
    if (has_got) {
        dyld_info_command_t dyld_info = {0};
        dyld_info.cmd = LC_DYLD_INFO_ONLY;
        dyld_info.cmdsize = sizeof(dyld_info_command_t);
        dyld_info.rebase_off = rebase_size ? (uint32_t)rebase_off : 0;
        dyld_info.rebase_size = (uint32_t)rebase_size;
        dyld_info.bind_off = bind_size ? (uint32_t)bind_off : 0;
        dyld_info.bind_size = (uint32_t)bind_size;
        dyld_info.weak_bind_off = 0;
        dyld_info.weak_bind_size = 0;
        dyld_info.lazy_bind_off = lazy_bind_size ? (uint32_t)lazy_bind_off : 0;
        dyld_info.lazy_bind_size = (uint32_t)lazy_bind_size;
        dyld_info.export_off = export_size ? (uint32_t)export_off : 0;
        dyld_info.export_size = (uint32_t)export_size;

        if (!write_struct(f, &dyld_info, sizeof(dyld_info))) {
            fclose(f);
            macho_symtab_free(&symtab);
            return false;
        }
    }

    /* Create and write LC_SYMTAB */
    symtab_command_t symtab_cmd = {0};
    symtab_cmd.cmd = LC_SYMTAB;
    symtab_cmd.cmdsize = sizeof(symtab_command_t);
    symtab_cmd.symoff = (uint32_t)symtab_off;
    symtab_cmd.nsyms = (uint32_t)symtab.count;
    symtab_cmd.stroff = (uint32_t)strtab_off;
    symtab_cmd.strsize = (uint32_t)strtab_size;

    if (!write_struct(f, &symtab_cmd, sizeof(symtab_cmd))) {
        fclose(f);
        macho_symtab_free(&symtab);
        return false;
    }

    /* Create and write LC_DYSYMTAB (empty dynamic symbol table) */
    dysymtab_command_t dysymtab_cmd = {0};
    dysymtab_cmd.cmd = LC_DYSYMTAB;
    dysymtab_cmd.cmdsize = sizeof(dysymtab_command_t);
    dysymtab_cmd.ilocalsym = 0;
    dysymtab_cmd.nlocalsym = 0;
    dysymtab_cmd.iextdefsym = 0;
    dysymtab_cmd.nextdefsym = (uint32_t)defined_count;
    dysymtab_cmd.iundefsym = (uint32_t)defined_count;
    dysymtab_cmd.nundefsym = (uint32_t)undef_count;
    dysymtab_cmd.tocoff = 0;
    dysymtab_cmd.ntoc = 0;
    dysymtab_cmd.modtaboff = 0;
    dysymtab_cmd.nmodtab = 0;
    dysymtab_cmd.extrefsymoff = 0;
    dysymtab_cmd.nextrefsyms = 0;
    dysymtab_cmd.indirectsymoff = indirect_count > 0 ? (uint32_t)indirect_off : 0;
    dysymtab_cmd.nindirectsyms = indirect_count;
    dysymtab_cmd.extreloff = extrel.count > 0 ? (uint32_t)extrel_off : 0;
    dysymtab_cmd.nextrel = (uint32_t)extrel.count;
    dysymtab_cmd.locreloff = 0;
    dysymtab_cmd.nlocrel = 0;

    if (!write_struct(f, &dysymtab_cmd, sizeof(dysymtab_cmd))) {
        fclose(f);
        macho_symtab_free(&symtab);
        return false;
    }

    /* Create and write LC_UUID */
    uuid_command_t uuid_cmd = {0};
    uuid_cmd.cmd = LC_UUID;
    uuid_cmd.cmdsize = sizeof(uuid_command_t);
    /* Generate a simple UUID from the entry point and text size */
    for (int i = 0; i < 16; i++) {
        if (i < 8) {
            uuid_cmd.uuid[i] = (context->entry_point >> (i * 8)) & 0xFF;
        } else {
            uuid_cmd.uuid[i] = (text_size >> ((i - 8) * 8)) & 0xFF;
        }
    }

    if (!write_struct(f, &uuid_cmd, sizeof(uuid_cmd))) {
        fclose(f);
        return false;
    }

    /* Create and write LC_BUILD_VERSION */
    build_version_command_t build_cmd = {0};
    build_cmd.cmd = LC_BUILD_VERSION;
    build_cmd.cmdsize = sizeof(build_version_command_t);
    build_cmd.platform = 1;     /* 1 = macOS */
    build_cmd.minos = 0x000b0000;   /* macOS 11.0.0 minimum */
    build_cmd.sdk = 0x000e0000;     /* macOS 14.0.0 SDK */
    build_cmd.ntools = 0;       /* No tool entries */

    if (!write_struct(f, &build_cmd, sizeof(build_cmd))) {
        fclose(f);
        macho_symtab_free(&symtab);
        return false;
    }

    /* Pad to page boundary before __TEXT segment data */
    uint64_t current_pos = header_size + load_cmds_size;
    size_t padding_before_text = text_file_offset - current_pos;
    if (!write_padding(f, padding_before_text)) {
        fclose(f);
        macho_symtab_free(&symtab);
        return false;
    }

    /* Write __TEXT segment data */
    /* Write __text section data */
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_TEXT) {
            if (context->merged_sections[i].data != NULL &&
                context->merged_sections[i].size > 0) {
                if (fwrite(context->merged_sections[i].data,
                          context->merged_sections[i].size, 1, f) != 1) {
                    fprintf(stderr, "Mach-O error: Failed to write __text section data\n");
                    fclose(f);
                    macho_symtab_free(&symtab);
                    return false;
                }
            }
            break;
        }
    }

    uint64_t current_text_pos = text_file_offset;
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_TEXT) {
            current_text_pos += context->merged_sections[i].size;
            break;
        }
    }

    if (has_stubs && stubs_data && stubs_size > 0) {
        uint64_t stubs_file_offset = text_file_offset + stubs_offset;
        if (stubs_file_offset > current_text_pos) {
            if (!write_padding(f, stubs_file_offset - current_text_pos)) {
                fclose(f);
                macho_symtab_free(&symtab);
                return false;
            }
            current_text_pos = stubs_file_offset;
        }
        if (fwrite(stubs_data, stubs_size, 1, f) != 1) {
            fprintf(stderr, "Mach-O error: Failed to write __stubs section data\n");
            fclose(f);
            macho_symtab_free(&symtab);
            return false;
        }
        current_text_pos += stubs_size;
    }

    /* Align to 8-byte boundary before __const section */
    uint64_t const_offset = 0;
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_RODATA) {
            const_offset = (context->merged_sections[i].vaddr - text_vm_addr);
            break;
        }
    }

    if (const_offset > 0) {
        if (const_offset > current_text_pos) {
            if (!write_padding(f, const_offset - current_text_pos)) {
                fclose(f);
                macho_symtab_free(&symtab);
                return false;
            }
            current_text_pos = const_offset;
        }
    }

    /* Write __const section data (rodata) */
    for (int i = 0; i < context->merged_section_count; i++) {
        if (context->merged_sections[i].type == SECTION_TYPE_RODATA) {
            if (context->merged_sections[i].data != NULL &&
                context->merged_sections[i].size > 0) {
                if (fwrite(context->merged_sections[i].data,
                          context->merged_sections[i].size, 1, f) != 1) {
                    fprintf(stderr, "Mach-O error: Failed to write __const section data\n");
                    fclose(f);
                    macho_symtab_free(&symtab);
                    return false;
                }
            }
            break;
        }
    }

    /* Pad to page boundary before __DATA_CONST segment */
    current_pos = text_filesize;
    uint64_t aligned_pos = round_up_to_page(current_pos, page_size);
    if (!write_padding(f, aligned_pos - current_pos)) {
        fclose(f);
        macho_symtab_free(&symtab);
        return false;
    }

    /* Write __DATA_CONST segment data (GOT) */
    if (has_got && got_size > 0) {
        if (!write_padding(f, got_size)) {
            fclose(f);
            macho_symtab_free(&symtab);
            return false;
        }
    }

    /* Pad to page boundary before __DATA segment */
    current_pos = data_const_file_offset + data_const_filesize;
    aligned_pos = round_up_to_page(current_pos, page_size);
    if (!write_padding(f, aligned_pos - current_pos)) {
        fclose(f);
        macho_symtab_free(&symtab);
        return false;
    }

    /* Write __DATA segment data */
    uint64_t data_written = 0;
    linker_section_t* data_payloads[] = {
        macho_find_merged_section(context, SECTION_TYPE_DATA),
        macho_find_merged_section(context, SECTION_TYPE_TLV),
        macho_find_merged_section(context, SECTION_TYPE_TDATA)
    };

    for (size_t i = 0; i < sizeof(data_payloads) / sizeof(data_payloads[0]); i++) {
        linker_section_t* section = data_payloads[i];
        if (!section || section->size == 0) {
            continue;
        }
        uint64_t aligned = align_to(data_written, 8);
        if (aligned > data_written) {
            if (!write_padding(f, aligned - data_written)) {
                fclose(f);
                macho_symtab_free(&symtab);
                return false;
            }
            data_written = aligned;
        }
        if (section->data != NULL) {
            if (fwrite(section->data, section->size, 1, f) != 1) {
                fprintf(stderr, "Mach-O error: Failed to write __DATA section data\n");
                fclose(f);
                macho_symtab_free(&symtab);
                return false;
            }
        }
        data_written += section->size;
    }

    current_pos = data_file_offset + data_filesize;
    if (linkedit_file_offset > current_pos) {
        if (!write_padding(f, linkedit_file_offset - current_pos)) {
            fclose(f);
            macho_symtab_free(&symtab);
            return false;
        }
    }

    uint64_t linkedit_pos = linkedit_file_offset;
    if (bind_size > 0) {
        if (bind_off > linkedit_pos) {
            if (!write_padding(f, bind_off - linkedit_pos)) {
                fclose(f);
                macho_symtab_free(&symtab);
                return false;
            }
            linkedit_pos = bind_off;
        }
        if (fwrite(bind_info.data, bind_info.size, 1, f) != 1) {
            fprintf(stderr, "Mach-O error: Failed to write bind info\n");
            fclose(f);
            macho_symtab_free(&symtab);
            return false;
        }
        linkedit_pos += bind_info.size;
    }

    if (export_size > 0) {
        if (export_off > linkedit_pos) {
            if (!write_padding(f, export_off - linkedit_pos)) {
                fclose(f);
                macho_symtab_free(&symtab);
                return false;
            }
            linkedit_pos = export_off;
        }
        if (fwrite(export_info.data, export_info.size, 1, f) != 1) {
            fprintf(stderr, "Mach-O error: Failed to write export info\n");
            fclose(f);
            macho_symtab_free(&symtab);
            return false;
        }
        linkedit_pos += export_info.size;
    }

    if (extrel.count > 0) {
        if (extrel_off > linkedit_pos) {
            if (!write_padding(f, extrel_off - linkedit_pos)) {
                fclose(f);
                macho_symtab_free(&symtab);
                macho_extrel_free(&extrel);
                return false;
            }
            linkedit_pos = extrel_off;
        }
        if (fwrite(extrel.relocs, extrel_size, 1, f) != 1) {
            fprintf(stderr, "Mach-O error: Failed to write external relocations\n");
            fclose(f);
            macho_symtab_free(&symtab);
            macho_extrel_free(&extrel);
            return false;
        }
        linkedit_pos += extrel_size;
    }

    if (symtab.count > 0) {
        if (symtab_off > linkedit_pos) {
            if (!write_padding(f, symtab_off - linkedit_pos)) {
                fclose(f);
                macho_symtab_free(&symtab);
                macho_extrel_free(&extrel);
                return false;
            }
            linkedit_pos = symtab_off;
        }
        if (fwrite(symtab.symbols, symtab_size, 1, f) != 1) {
            fprintf(stderr, "Mach-O error: Failed to write symbol table\n");
            fclose(f);
            macho_symtab_free(&symtab);
            macho_extrel_free(&extrel);
            return false;
        }
        linkedit_pos += symtab_size;
    }

    if (indirect_count > 0 && indirect_symbols != NULL) {
        if (indirect_off > linkedit_pos) {
            if (!write_padding(f, indirect_off - linkedit_pos)) {
                fclose(f);
                macho_symtab_free(&symtab);
                macho_extrel_free(&extrel);
                return false;
            }
            linkedit_pos = indirect_off;
        }
        if (fwrite(indirect_symbols, indirect_size, 1, f) != 1) {
            fprintf(stderr, "Mach-O error: Failed to write indirect symbol table\n");
            fclose(f);
            macho_symtab_free(&symtab);
            macho_extrel_free(&extrel);
            return false;
        }
        linkedit_pos += indirect_size;
    }

    if (symtab.strsize > 0) {
        if (strtab_off > linkedit_pos) {
            if (!write_padding(f, strtab_off - linkedit_pos)) {
                fclose(f);
                macho_symtab_free(&symtab);
                macho_extrel_free(&extrel);
                return false;
            }
            linkedit_pos = strtab_off;
        }
        if (fwrite(symtab.strtab, symtab.strsize, 1, f) != 1) {
            fprintf(stderr, "Mach-O error: Failed to write string table\n");
            fclose(f);
            macho_symtab_free(&symtab);
            macho_extrel_free(&extrel);
            return false;
        }
        linkedit_pos += symtab.strsize;
    }

    uint64_t linkedit_written = linkedit_pos - linkedit_file_offset;
    if (linkedit_size > linkedit_written) {
        if (!write_padding(f, linkedit_size - linkedit_written)) {
            fclose(f);
            macho_symtab_free(&symtab);
            macho_extrel_free(&extrel);
            return false;
        }
    }

    /* Note: __bss section has no file content (zero-initialized at runtime) */

    free(stubs_data);
    free(indirect_symbols);
    macho_buffer_free(&export_info);
    macho_buffer_free(&bind_info);
    macho_stub_list_free(&stubs);
    macho_stub_list_free(&got_entries);
    macho_symtab_free(&symtab);
    macho_extrel_free(&extrel);
    fclose(f);

    /* Set executable permissions (0755) */
    if (chmod(output_path, 0755) != 0) {
        fprintf(stderr, "Mach-O warning: Failed to set executable permissions\n");
        /* Not a fatal error, continue */
    }

    return true;
}
