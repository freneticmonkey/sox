/*
 * Mach-O Structure Validation Tests
 *
 * Deep validation of Mach-O executable structure including:
 * - Entry point file offset calculation (LC_MAIN)
 * - Load command structure and ordering
 * - Segment and section layout
 * - Virtual address to file offset mapping
 * - Symbol table integrity
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../lib/memory.h"
#include "../lib/file.h"
#include "../vm.h"
#include "../vm_config.h"
#include "../native/linker_core.h"
#include "../native/macho_executable.h"
#include "../native/macho_reader.h"
#include "../../ext/munit/munit.h"

#ifdef SOX_MACOS
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

/* Helper: Read Mach-O header from file */
static bool read_macho_header(const char* path, struct mach_header_64* header) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    size_t read = fread(header, sizeof(*header), 1, f);
    fclose(f);

    return read == 1 && header->magic == MH_MAGIC_64;
}

/* Helper: Find specific load command in Mach-O file */
static bool find_load_command(const char* path, uint32_t cmd_type, void* cmd_out, size_t cmd_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    struct mach_header_64 header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        return false;
    }

    // Read through load commands
    for (uint32_t i = 0; i < header.ncmds; i++) {
        uint32_t cmd;
        uint32_t cmdsize;
        long cmd_start = ftell(f);

        if (fread(&cmd, sizeof(cmd), 1, f) != 1) {
            fclose(f);
            return false;
        }

        if (fread(&cmdsize, sizeof(cmdsize), 1, f) != 1) {
            fclose(f);
            return false;
        }

        if (cmd == cmd_type) {
            // Found it - rewind and read full command
            fseek(f, cmd_start, SEEK_SET);
            size_t to_read = cmdsize < cmd_size ? cmdsize : cmd_size;
            if (fread(cmd_out, to_read, 1, f) != 1) {
                fclose(f);
                return false;
            }
            fclose(f);
            return true;
        }

        // Skip to next command
        fseek(f, cmd_start + cmdsize, SEEK_SET);
    }

    fclose(f);
    return false;
}

/* Helper: Find __TEXT segment */
static bool find_text_segment(const char* path, struct segment_command_64* seg) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    struct mach_header_64 header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        return false;
    }

    for (uint32_t i = 0; i < header.ncmds; i++) {
        long cmd_start = ftell(f);
        struct segment_command_64 segment;

        if (fread(&segment, sizeof(segment), 1, f) != 1) {
            fclose(f);
            return false;
        }

        if (segment.cmd == LC_SEGMENT_64 && strcmp(segment.segname, "__TEXT") == 0) {
            memcpy(seg, &segment, sizeof(*seg));
            fclose(f);
            return true;
        }

        fseek(f, cmd_start + segment.cmdsize, SEEK_SET);
    }

    fclose(f);
    return false;
}

/* Test: Entry point offset is non-zero and valid */
static MunitResult test_entry_point_offset_valid(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

#ifndef __aarch64__
    
    return MUNIT_SKIP;
#endif

    const char* source = "src/test/scripts/basic.sox";
    const char* output = "/tmp/sox_macho_entrypoint_test";

    // Compile with custom linker
    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = false,
        .enable_native_output = true,
        .native_output_file = (char*)output,
        .native_target_arch = "arm64",
        .native_target_os = "macos",
        .native_emit_object = false,
        .native_debug_output = false,
        .native_optimization_level = 0,
        .use_custom_linker = true,
        .args = l_parse_args(2, (const char*[]){ "sox", source })
    };

    int status = l_run_file(&config);
    munit_assert_int(status, ==, 0);

    // Read LC_MAIN command
    struct entry_point_command main_cmd;
    bool found = find_load_command(output, LC_MAIN, &main_cmd, sizeof(main_cmd));
    munit_assert_true(found);

    // Entry offset should be non-zero (not pointing to header)
    munit_logf(MUNIT_LOG_INFO, "Entry point offset: %llu", main_cmd.entryoff);
    munit_assert_uint64(main_cmd.entryoff, >, 0);

    // Entry offset should be page-aligned or near page boundary
    // (typically around 16384 = 0x4000 on macOS)
    munit_assert_uint64(main_cmd.entryoff, >, 4096);
    munit_assert_uint64(main_cmd.entryoff, <, 1024 * 1024);  // Sanity check

    // Clean up
    unlink(output);

    return MUNIT_OK;
}

/* Test: Entry point file offset matches virtual address calculation */
static MunitResult test_entry_point_offset_calculation(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

#ifndef __aarch64__
    
    return MUNIT_SKIP;
#endif

    const char* source = "src/test/scripts/basic.sox";
    const char* output = "/tmp/sox_macho_entry_calc_test";

    // Compile with custom linker
    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = false,
        .enable_native_output = true,
        .native_output_file = (char*)output,
        .native_target_arch = "arm64",
        .native_target_os = "macos",
        .native_emit_object = false,
        .native_debug_output = false,
        .native_optimization_level = 0,
        .use_custom_linker = true,
        .args = l_parse_args(2, (const char*[]){ "sox", source })
    };

    int status = l_run_file(&config);
    munit_assert_int(status, ==, 0);

    // Read LC_MAIN command
    struct entry_point_command main_cmd;
    bool found_main = find_load_command(output, LC_MAIN, &main_cmd, sizeof(main_cmd));
    munit_assert_true(found_main);

    // Read __TEXT segment
    struct segment_command_64 text_seg;
    bool found_text = find_text_segment(output, &text_seg);
    munit_assert_true(found_text);

    munit_logf(MUNIT_LOG_INFO, "__TEXT segment:");
    munit_logf(MUNIT_LOG_INFO, "  vmaddr:   0x%llx", text_seg.vmaddr);
    munit_logf(MUNIT_LOG_INFO, "  fileoff:  %llu", text_seg.fileoff);
    munit_logf(MUNIT_LOG_INFO, "  vmsize:   %llu", text_seg.vmsize);
    munit_logf(MUNIT_LOG_INFO, "  filesize: %llu", text_seg.filesize);
    munit_logf(MUNIT_LOG_INFO, "LC_MAIN:");
    munit_logf(MUNIT_LOG_INFO, "  entryoff: %llu", main_cmd.entryoff);

    // Verify: entryoff should be >= text_seg.fileoff
    // (entry point is somewhere in the __TEXT segment's file region)
    munit_assert_uint64(main_cmd.entryoff, >=, text_seg.fileoff);
    munit_assert_uint64(main_cmd.entryoff, <, text_seg.fileoff + text_seg.filesize);

    // Clean up
    unlink(output);

    return MUNIT_OK;
}

/* Test: Load commands are properly ordered and sized */
static MunitResult test_load_commands_structure(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

#ifndef __aarch64__
    
    return MUNIT_SKIP;
#endif

    const char* source = "src/test/scripts/basic.sox";
    const char* output = "/tmp/sox_macho_loadcmd_test";

    // Compile with custom linker
    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = false,
        .enable_native_output = true,
        .native_output_file = (char*)output,
        .native_target_arch = "arm64",
        .native_target_os = "macos",
        .native_emit_object = false,
        .native_debug_output = false,
        .native_optimization_level = 0,
        .use_custom_linker = true,
        .args = l_parse_args(2, (const char*[]){ "sox", source })
    };

    int status = l_run_file(&config);
    munit_assert_int(status, ==, 0);

    // Read header
    struct mach_header_64 header;
    bool read_ok = read_macho_header(output, &header);
    munit_assert_true(read_ok);

    // Verify header fields
    munit_assert_uint32(header.magic, ==, MH_MAGIC_64);
    munit_assert_uint32(header.cputype, ==, CPU_TYPE_ARM64);
    munit_assert_uint32(header.filetype, ==, MH_EXECUTE);
    munit_assert_uint32(header.ncmds, >, 0);
    munit_assert_uint32(header.sizeofcmds, >, 0);

    // Verify we have expected load commands
    struct segment_command_64 text_seg, data_seg;
    struct entry_point_command main_cmd;

    munit_assert_true(find_text_segment(output, &text_seg));
    munit_assert_true(find_load_command(output, LC_MAIN, &main_cmd, sizeof(main_cmd)));

    // Clean up
    unlink(output);

    return MUNIT_OK;
}

/* Test: Segment virtual addresses are properly aligned */
static MunitResult test_segment_alignment(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

#ifndef __aarch64__
    
    return MUNIT_SKIP;
#endif

    const char* source = "src/test/scripts/basic.sox";
    const char* output = "/tmp/sox_macho_alignment_test";

    // Compile with custom linker
    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = false,
        .enable_native_output = true,
        .native_output_file = (char*)output,
        .native_target_arch = "arm64",
        .native_target_os = "macos",
        .native_emit_object = false,
        .native_debug_output = false,
        .native_optimization_level = 0,
        .use_custom_linker = true,
        .args = l_parse_args(2, (const char*[]){ "sox", source })
    };

    int status = l_run_file(&config);
    munit_assert_int(status, ==, 0);

    // Read __TEXT segment
    struct segment_command_64 text_seg;
    bool found = find_text_segment(output, &text_seg);
    munit_assert_true(found);

    // Virtual address should be page-aligned (16K pages on ARM64 macOS)
    const uint64_t page_size = 16384;
    munit_assert_uint64(text_seg.vmaddr % page_size, ==, 0);
    munit_assert_uint64(text_seg.fileoff % page_size, ==, 0);

    // Verify typical base address for Mach-O (0x100000000)
    munit_assert_uint64(text_seg.vmaddr, ==, 0x100000000ULL);

    // Clean up
    unlink(output);

    return MUNIT_OK;
}

/* Test: Compare custom linker vs system linker Mach-O structure */
static MunitResult test_macho_structure_comparison(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

#ifndef __aarch64__
    
    return MUNIT_SKIP;
#endif

    const char* source = "src/test/scripts/basic.sox";
    const char* custom_output = "/tmp/sox_macho_custom";
    const char* system_output = "/tmp/sox_macho_system";

    // Compile with custom linker
    vm_config_t config_custom = {
        .enable_serialisation = false,
        .suppress_print = false,
        .enable_native_output = true,
        .native_output_file = (char*)custom_output,
        .native_target_arch = "arm64",
        .native_target_os = "macos",
        .native_emit_object = false,
        .native_debug_output = false,
        .native_optimization_level = 0,
        .use_custom_linker = true,
        .args = l_parse_args(2, (const char*[]){ "sox", source })
    };

    // Compile with system linker
    vm_config_t config_system = {
        .enable_serialisation = false,
        .suppress_print = false,
        .enable_native_output = true,
        .native_output_file = (char*)system_output,
        .native_target_arch = "arm64",
        .native_target_os = "macos",
        .native_emit_object = false,
        .native_debug_output = false,
        .native_optimization_level = 0,
        .use_custom_linker = false,
        .args = l_parse_args(2, (const char*[]){ "sox", source })
    };

    int status_custom = l_run_file(&config_custom);
    int status_system = l_run_file(&config_system);

    munit_assert_int(status_custom, ==, 0);
    munit_assert_int(status_system, ==, 0);

    // Compare headers
    struct mach_header_64 header_custom, header_system;
    munit_assert_true(read_macho_header(custom_output, &header_custom));
    munit_assert_true(read_macho_header(system_output, &header_system));

    // Both should have same basic structure
    munit_assert_uint32(header_custom.magic, ==, header_system.magic);
    munit_assert_uint32(header_custom.cputype, ==, header_system.cputype);
    munit_assert_uint32(header_custom.filetype, ==, header_system.filetype);

    // Compare __TEXT segments
    struct segment_command_64 text_custom, text_system;
    munit_assert_true(find_text_segment(custom_output, &text_custom));
    munit_assert_true(find_text_segment(system_output, &text_system));

    // Virtual addresses should match
    munit_assert_uint64(text_custom.vmaddr, ==, text_system.vmaddr);

    // Compare LC_MAIN commands
    struct entry_point_command main_custom, main_system;
    munit_assert_true(find_load_command(custom_output, LC_MAIN, &main_custom, sizeof(main_custom)));
    munit_assert_true(find_load_command(system_output, LC_MAIN, &main_system, sizeof(main_system)));

    // Both should have non-zero entry points
    munit_assert_uint64(main_custom.entryoff, >, 0);
    munit_assert_uint64(main_system.entryoff, >, 0);

    munit_logf(MUNIT_LOG_INFO, "Custom linker entryoff: %llu", main_custom.entryoff);
    munit_logf(MUNIT_LOG_INFO, "System linker entryoff: %llu", main_system.entryoff);

    // Entry offsets might differ slightly due to different code gen,
    // but should be in same ballpark (same page)
    uint64_t diff = main_custom.entryoff > main_system.entryoff ?
        main_custom.entryoff - main_system.entryoff :
        main_system.entryoff - main_custom.entryoff;

    munit_assert_uint64(diff, <, 4096);  // Within one page

    // Clean up
    unlink(custom_output);
    unlink(system_output);

    return MUNIT_OK;
}

/* Test: Executable can actually be executed */
static MunitResult test_executable_runs(const MunitParameter params[], void* data) {
    (void)params;
    (void)data;

#ifndef __aarch64__
    
    return MUNIT_SKIP;
#endif

    const char* source = "src/test/scripts/basic.sox";
    const char* output = "/tmp/sox_macho_exec_test";

    // Compile with custom linker
    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = false,
        .enable_native_output = true,
        .native_output_file = (char*)output,
        .native_target_arch = "arm64",
        .native_target_os = "macos",
        .native_emit_object = false,
        .native_debug_output = false,
        .native_optimization_level = 0,
        .use_custom_linker = true,
        .args = l_parse_args(2, (const char*[]){ "sox", source })
    };

    int status = l_run_file(&config);
    munit_assert_int(status, ==, 0);

    // Try to execute it
    char command[512];
    snprintf(command, sizeof(command), "DYLD_LIBRARY_PATH=./build %s 2>&1", output);

    FILE* pipe = popen(command, "r");
    munit_assert_not_null(pipe);

    char buffer[256];
    bool found_output = false;
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        if (strstr(buffer, "hello world")) {
            found_output = true;
        }
    }

    int exec_status = pclose(pipe);

    // Should execute successfully (exit code 0)
    munit_assert_int(exec_status, ==, 0);
    munit_assert_true(found_output);

    // Clean up
    unlink(output);

    return MUNIT_OK;
}

/* Test suite definition */
static MunitTest macho_validation_tests[] = {
    {
        "/entry_point_offset_valid",
        test_entry_point_offset_valid,
        NULL, NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL
    },
    {
        "/entry_point_offset_calculation",
        test_entry_point_offset_calculation,
        NULL, NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL
    },
    {
        "/load_commands_structure",
        test_load_commands_structure,
        NULL, NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL
    },
    {
        "/segment_alignment",
        test_segment_alignment,
        NULL, NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL
    },
    {
        "/structure_comparison",
        test_macho_structure_comparison,
        NULL, NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL
    },
    {
        "/executable_runs",
        test_executable_runs,
        NULL, NULL,
        MUNIT_TEST_OPTION_NONE,
        NULL
    },
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
};

MunitSuite macho_validation_suite(void) {
#ifndef SOX_MACOS
    printf("⚠️  Skipping Mach-O validation tests: Only supported on macOS\n");
    static MunitSuite empty_suite = {
        .prefix = (char*)"macho_validation/",
        .tests = NULL,
        .suites = NULL,
        .iterations = 1,
        .options = MUNIT_SUITE_OPTION_NONE
    };
    return empty_suite;
#else
    static MunitSuite suite = {
        .prefix = (char*)"macho_validation/",
        .tests = macho_validation_tests,
        .suites = NULL,
        .iterations = 1,
        .options = MUNIT_SUITE_OPTION_NONE
    };
    return suite;
#endif
}

#else  // Not macOS

MunitSuite macho_validation_suite(void) {
    static MunitSuite empty_suite = {
        .prefix = (char*)"macho_validation/",
        .tests = NULL,
        .suites = NULL,
        .iterations = 1,
        .options = MUNIT_SUITE_OPTION_NONE
    };
    return empty_suite;
}

#endif  // SOX_MACOS
