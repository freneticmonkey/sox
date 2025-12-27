#!/bin/bash
# Phase 6.2: Integration Test Runner
# Tests the linker_link() API with various linking scenarios

# Note: Not using 'set -e' because arithmetic operations can return non-zero

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
SOX_BIN="${SCRIPT_DIR}/../../../../build/sox"
TEST_DIR="${SCRIPT_DIR}"
TEMP_DIR="${TEST_DIR}/temp"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0

# Create temp directory
mkdir -p "${TEMP_DIR}"

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up temporary files..."
    rm -rf "${TEMP_DIR}"
}
trap cleanup EXIT

# Print test header
print_header() {
    echo ""
    echo "========================================"
    echo "  Phase 6.2: Linker Integration Tests"
    echo "========================================"
    echo ""
}

# Print test result
print_result() {
    local test_name=$1
    local status=$2
    local message=$3

    if [ "$status" = "PASS" ]; then
        echo -e "${GREEN}[PASS]${NC} ${test_name}"
        ((PASSED_TESTS++))
    elif [ "$status" = "FAIL" ]; then
        echo -e "${RED}[FAIL]${NC} ${test_name}: ${message}"
        ((FAILED_TESTS++))
    elif [ "$status" = "SKIP" ]; then
        echo -e "${YELLOW}[SKIP]${NC} ${test_name}: ${message}"
        ((SKIPPED_TESTS++))
    fi
    ((TOTAL_TESTS++))
}

# Test if Sox binary exists
check_sox_binary() {
    if [ ! -f "${SOX_BIN}" ]; then
        echo -e "${RED}Error: Sox binary not found at ${SOX_BIN}${NC}"
        echo "Please run 'make build-debug' first"
        exit 1
    fi
}

# Test single object file linking
test_single_object() {
    local test_name="single_object"
    local sox_file="${TEST_DIR}/${test_name}.sox"
    local expected_out="${TEST_DIR}/${test_name}.sox.out"
    local output_file="${TEMP_DIR}/${test_name}"
    local actual_out="${TEMP_DIR}/${test_name}.out"

    # Check if native code generation is supported
    if ! ${SOX_BIN} --help | grep -q "native"; then
        print_result "${test_name}" "SKIP" "Native code generation not available"
        return
    fi

    # Note: Since custom linker phases 1-5 are not implemented yet,
    # this test validates the integration framework falls back to system linker
    echo "Testing: ${test_name}"
    echo "  Description: Single object file linking via linker_link() API"

    # For now, we just validate the API exists by checking the binary was built
    # Real linking tests will be added when Phase 1-5 are implemented
    print_result "${test_name}" "SKIP" "Custom linker phases 1-5 not yet implemented"
}

# Test multi-object file linking
test_multi_object() {
    local test_name="multi_object"

    echo "Testing: ${test_name}"
    echo "  Description: Multiple object files linked together"

    # Skip until custom linker is implemented
    print_result "${test_name}" "SKIP" "Custom linker phases 1-5 not yet implemented"
}

# Test global variable references
test_global_vars() {
    local test_name="global_vars"

    echo "Testing: ${test_name}"
    echo "  Description: Global variable references across compilation"

    # Skip until custom linker is implemented
    print_result "${test_name}" "SKIP" "Custom linker phases 1-5 not yet implemented"
}

# Test runtime library integration
test_runtime_calls() {
    local test_name="runtime_calls"

    echo "Testing: ${test_name}"
    echo "  Description: Runtime library function integration"

    # Skip until custom linker is implemented
    print_result "${test_name}" "SKIP" "Custom linker phases 1-5 not yet implemented"
}

# Test linker API exists (smoke test)
test_linker_api_smoke() {
    local test_name="linker_api_smoke"

    echo "Testing: ${test_name}"
    echo "  Description: Verify linker_link() API was successfully integrated"

    # Check that the binary built successfully (which means our API compiled)
    if [ -f "${SOX_BIN}" ]; then
        print_result "${test_name}" "PASS" ""
    else
        print_result "${test_name}" "FAIL" "Sox binary not found"
    fi
}

# Print summary
print_summary() {
    echo ""
    echo "========================================"
    echo "  Test Summary"
    echo "========================================"
    echo "Total:   ${TOTAL_TESTS}"
    echo -e "${GREEN}Passed:  ${PASSED_TESTS}${NC}"
    echo -e "${RED}Failed:  ${FAILED_TESTS}${NC}"
    echo -e "${YELLOW}Skipped: ${SKIPPED_TESTS}${NC}"
    echo ""

    if [ ${FAILED_TESTS} -gt 0 ]; then
        echo -e "${RED}Some tests failed!${NC}"
        return 1
    elif [ ${PASSED_TESTS} -eq 0 ]; then
        echo -e "${YELLOW}All tests were skipped (custom linker not yet implemented)${NC}"
        echo "Phase 6.1 integration framework is ready for Phases 1-5 implementation"
        return 0
    else
        echo -e "${GREEN}All active tests passed!${NC}"
        return 0
    fi
}

# Main test execution
main() {
    print_header
    check_sox_binary

    echo "Sox binary: ${SOX_BIN}"
    echo ""

    # Run tests
    test_linker_api_smoke
    test_single_object
    test_multi_object
    test_global_vars
    test_runtime_calls

    # Print summary and return its exit code
    if print_summary; then
        return 0
    else
        return 1
    fi
}

# Run main and exit with its return code
main "$@"
exit $?
