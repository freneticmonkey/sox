package main

/*
#include <stdlib.h>
*/
import "C"
import (
	"context"
	"fmt"
	"os"
	"strings"
	"sync"
	"unsafe"

	"github.com/tetratelabs/wazero"
	"github.com/tetratelabs/wazero/api"
	"github.com/tetratelabs/wazero/imports/wasi_snapshot_preview1"
)

// OutputCapture holds captured output from print_f64 calls
type OutputCapture struct {
	values []float64
	mu     sync.Mutex
}

// Global output capture for the current execution
var (
	currentOutput *OutputCapture
	outputMutex   sync.Mutex
)

// VerifyResult contains the result of WASM verification
type VerifyResult struct {
	Success bool
	Error   string
	Output  []float64
}

// print_f64 is the host function that Sox WASM modules import
func print_f64(_ context.Context, m api.Module, value float64) {
	outputMutex.Lock()
	defer outputMutex.Unlock()

	if currentOutput != nil {
		currentOutput.mu.Lock()
		defer currentOutput.mu.Unlock()
		currentOutput.values = append(currentOutput.values, value)
	}
}

// LoadAndExecuteWASM loads a WASM file and executes its main function
func LoadAndExecuteWASM(wasmPath string) (*VerifyResult, error) {
	result := &VerifyResult{
		Success: false,
		Output:  []float64{},
	}

	// Read WASM file
	wasmBytes, err := os.ReadFile(wasmPath)
	if err != nil {
		result.Error = fmt.Sprintf("failed to read WASM file: %v", err)
		return result, err
	}

	// Validate WASM magic number
	if len(wasmBytes) < 8 {
		result.Error = "invalid WASM file: too short"
		return result, fmt.Errorf(result.Error)
	}
	if wasmBytes[0] != 0x00 || wasmBytes[1] != 0x61 || wasmBytes[2] != 0x73 || wasmBytes[3] != 0x6d {
		result.Error = "invalid WASM file: missing magic number"
		return result, fmt.Errorf(result.Error)
	}

	// Create wazero runtime
	ctx := context.Background()
	r := wazero.NewRuntime(ctx)
	defer r.Close(ctx)

	// Instantiate WASI to provide basic functionality
	wasi_snapshot_preview1.MustInstantiate(ctx, r)

	// Initialize output capture with thread safety
	outputMutex.Lock()
	currentOutput = &OutputCapture{
		values: []float64{},
	}
	outputMutex.Unlock()

	defer func() {
		outputMutex.Lock()
		currentOutput = nil
		outputMutex.Unlock()
	}()

	// Create host module "env" with print_f64 function
	_, err = r.NewHostModuleBuilder("env").
		NewFunctionBuilder().
		WithFunc(print_f64).
		Export("print_f64").
		Instantiate(ctx)
	if err != nil {
		result.Error = fmt.Sprintf("failed to instantiate host module: %v", err)
		return result, err
	}

	// Instantiate the WASM module
	mod, err := r.InstantiateWithConfig(ctx, wasmBytes,
		wazero.NewModuleConfig().WithName("sox_module"))
	if err != nil {
		result.Error = fmt.Sprintf("failed to instantiate WASM module: %v", err)
		return result, err
	}
	defer mod.Close(ctx)

	// Execute the main function
	mainFunc := mod.ExportedFunction("main")
	if mainFunc == nil {
		result.Error = "WASM module does not export 'main' function"
		return result, fmt.Errorf(result.Error)
	}

	_, err = mainFunc.Call(ctx)
	if err != nil {
		result.Error = fmt.Sprintf("failed to execute main function: %v", err)
		return result, err
	}

	// Success! Capture the output with thread safety
	result.Success = true
	outputMutex.Lock()
	if currentOutput != nil {
		currentOutput.mu.Lock()
		result.Output = make([]float64, len(currentOutput.values))
		copy(result.Output, currentOutput.values)
		currentOutput.mu.Unlock()
	}
	outputMutex.Unlock()
	return result, nil
}

// FormatOutput converts output values to a string representation
func FormatOutput(values []float64) string {
	if len(values) == 0 {
		return ""
	}
	parts := make([]string, len(values))
	for i, v := range values {
		parts[i] = fmt.Sprintf("%.6f", v)
	}
	return strings.Join(parts, ",")
}

//export VerifyWASMFile
func VerifyWASMFile(path *C.char, output **C.char, errorMsg **C.char) C.int {
	goPath := C.GoString(path)

	result, err := LoadAndExecuteWASM(goPath)

	// Always set output (empty string if no output)
	outputStr := FormatOutput(result.Output)
	*output = C.CString(outputStr)

	if err != nil || !result.Success {
		if result.Error != "" {
			*errorMsg = C.CString(result.Error)
		} else {
			*errorMsg = C.CString("unknown error")
		}
		return 0 // failure
	}

	*errorMsg = nil
	return 1 // success
}

//export FreeString
func FreeString(s *C.char) {
	if s != nil {
		C.free(unsafe.Pointer(s))
	}
}

//export GetWASMOutput
func GetWASMOutput(path *C.char, output **C.double, count *C.int, errorMsg **C.char) C.int {
	goPath := C.GoString(path)

	result, err := LoadAndExecuteWASM(goPath)

	if err != nil || !result.Success {
		if result.Error != "" {
			*errorMsg = C.CString(result.Error)
		} else {
			*errorMsg = C.CString("unknown error")
		}
		*count = 0
		return 0 // failure
	}

	// Allocate C array for output values
	if len(result.Output) > 0 {
		*count = C.int(len(result.Output))
		// Allocate memory that C can use
		cArray := (*C.double)(C.malloc(C.size_t(len(result.Output)) * C.size_t(unsafe.Sizeof(C.double(0)))))
		// Convert Go slice to C array
		goSlice := unsafe.Slice((*float64)(unsafe.Pointer(cArray)), len(result.Output))
		copy(goSlice, result.Output)
		*output = cArray
	} else {
		*count = 0
		*output = nil
	}

	*errorMsg = nil
	return 1 // success
}

//export FreeDoubleArray
func FreeDoubleArray(arr *C.double) {
	if arr != nil {
		C.free(unsafe.Pointer(arr))
	}
}

func main() {
	// This is required for CGO to build a shared library
	// The actual functionality is exposed via exported functions
}
