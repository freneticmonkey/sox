#!/bin/bash
# Native code generation test script
# Tests all .sox files in src/test/scripts/native/ by compiling to native binaries
# and comparing output against expected .out files

set -e

# Get script directory and repository root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Change to repository root
cd "$REPO_ROOT"

# Detect platform and set library path variable
if [[ "$OSTYPE" == "darwin"* ]]; then
    LIB_PATH_VAR="DYLD_LIBRARY_PATH"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    LIB_PATH_VAR="LD_LIBRARY_PATH"
else
    echo "Unsupported platform: $OSTYPE"
    exit 1
fi

# Check if sox binary exists
if [ ! -f "./build/sox" ]; then
    echo "Error: ./build/sox not found. Please run 'make build' first."
    exit 1
fi

# Check if runtime library exists
if [ ! -f "./build/libsox_runtime.a" ]; then
    echo "Error: ./build/libsox_runtime.a not found. Please run 'make build-runtime' first."
    exit 1
fi

echo "Testing native code generation..."
echo "================================"
echo ""

pass=0
fail=0
total=0

for sox_file in src/test/scripts/native/*.sox; do
  # Skip if no .sox files found
  if [ ! -f "$sox_file" ]; then
    echo "No native test files found in src/test/scripts/native/"
    exit 1
  fi

  name=$(basename "$sox_file" .sox)
  total=$((total + 1))

  echo -n "Testing: $name ... "

  # Compile to native binary with timeout
  timeout 5 ./build/sox "$sox_file" --native --native-out="/tmp/native_test_$name" >/dev/null 2>&1
  compile_status=$?

  if [ $compile_status -eq 124 ]; then
    echo "FAIL (compilation timeout)"
    fail=$((fail + 1))
    rm -f "/tmp/native_test_$name" "/tmp/native_test_$name.tmp.o"
    continue
  elif [ $compile_status -ne 0 ]; then
    echo "FAIL (compilation error)"
    fail=$((fail + 1))
    rm -f "/tmp/native_test_$name" "/tmp/native_test_$name.tmp.o"
    continue
  fi

  # Run native binary with timeout and capture output
  actual=$(timeout 3 sh -c "$LIB_PATH_VAR=./build:\$$LIB_PATH_VAR /tmp/native_test_$name 2>&1 | grep -v '^\[RUNTIME\]'")
  run_status=$?

  if [ $run_status -eq 124 ]; then
    echo "FAIL (execution timeout)"
    fail=$((fail + 1))
    rm -f "/tmp/native_test_$name" "/tmp/native_test_$name.tmp.o"
    # Kill any hung process
    pkill -f "native_test_$name" 2>/dev/null || true
    continue
  elif [ $run_status -ne 0 ]; then
    echo "FAIL (execution error)"
    fail=$((fail + 1))
    rm -f "/tmp/native_test_$name" "/tmp/native_test_$name.tmp.o"
    continue
  fi

  # Compare with expected output
  if [ ! -f "${sox_file}.out" ]; then
    echo "FAIL (no expected output file)"
    fail=$((fail + 1))
    rm -f "/tmp/native_test_$name"
    continue
  fi

  expected=$(cat "${sox_file}.out")

  if [ "$actual" = "$expected" ]; then
    echo "PASS"
    pass=$((pass + 1))
  else
    echo "FAIL (output mismatch)"
    echo "  Expected:"
    echo "$expected" | sed 's/^/    /'
    echo "  Got:"
    echo "$actual" | sed 's/^/    /'
    fail=$((fail + 1))
  fi

  # Clean up temporary files
  rm -f "/tmp/native_test_$name" "/tmp/native_test_$name.tmp.o"
done

echo ""
echo "================================"
echo "Results: $pass/$total passed, $fail/$total failed"
echo ""

if [ $fail -gt 0 ]; then
  exit 1
else
  exit 0
fi
