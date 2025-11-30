package main

import (
	"encoding/json"
	"strings"
	"testing"
)

// TestHelloWorldCompilation tests basic "Hello World" graph compilation
func TestHelloWorldCompilation(t *testing.T) {
	nodes := []Node{
		{
			ID:   "entry",
			Type: "EntryPoint",
			Position: map[string]float64{
				"x": 100,
				"y": 100,
			},
			Data: map[string]interface{}{
				"label": "Start",
			},
		},
		{
			ID:   "str1",
			Type: "StringNode",
			Position: map[string]float64{
				"x": 100,
				"y": 200,
			},
			Data: map[string]interface{}{
				"label": "String",
				"value": "Hello, World!",
			},
		},
		{
			ID:   "print1",
			Type: "Print",
			Position: map[string]float64{
				"x": 100,
				"y": 300,
			},
			Data: map[string]interface{}{
				"label": "Print",
			},
		},
	}

	edges := []Edge{
		{
			ID:     "e1",
			Source: "entry",
			Target: "print1",
		},
		{
			ID:     "e2",
			Source: "str1",
			Target: "print1",
		},
	}

	compiler := NewGraphCompiler()
	source, sourceMap, err := compiler.CompileToSource(nodes, edges)

	if err != nil {
		t.Fatalf("Compilation failed: %v", err)
	}

	expectedSource := `print("Hello, World!")`
	if strings.TrimSpace(source) != expectedSource {
		t.Errorf("Expected source:\n%s\nGot:\n%s", expectedSource, source)
	}

	// Verify source map
	if sourceMap == nil {
		t.Fatal("Source map is nil")
	}

	if len(sourceMap.Mappings) == 0 {
		t.Error("Source map has no mappings")
	}

	// Verify mapping points to print node
	foundPrintMapping := false
	for _, mapping := range sourceMap.Mappings {
		if mapping.NodeID == "print1" {
			foundPrintMapping = true
			break
		}
	}

	if !foundPrintMapping {
		t.Error("Source map missing mapping for print node")
	}
}

// TestVariableDeclaration tests variable declaration compilation
func TestVariableDeclaration(t *testing.T) {
	nodes := []Node{
		{
			ID:   "entry",
			Type: "EntryPoint",
			Data: map[string]interface{}{"label": "Start"},
		},
		{
			ID:   "num1",
			Type: "NumberNode",
			Data: map[string]interface{}{
				"value": 42.0,
			},
		},
		{
			ID:   "var1",
			Type: "DeclareVar",
			Data: map[string]interface{}{
				"name": "x",
			},
		},
	}

	edges := []Edge{
		{ID: "e1", Source: "entry", Target: "var1"},
		{ID: "e2", Source: "num1", Target: "var1"},
	}

	compiler := NewGraphCompiler()
	source, _, err := compiler.CompileToSource(nodes, edges)

	if err != nil {
		t.Fatalf("Compilation failed: %v", err)
	}

	if !strings.Contains(source, "var x = 42") {
		t.Errorf("Expected 'var x = 42' in source, got:\n%s", source)
	}
}

// TestValidationMissingEntryPoint tests that validation catches missing entry point
func TestValidationMissingEntryPoint(t *testing.T) {
	nodes := []Node{
		{
			ID:   "print1",
			Type: "Print",
			Data: map[string]interface{}{"label": "Print"},
		},
	}

	edges := []Edge{}

	compiler := NewGraphCompiler()
	compiler.buildGraph(nodes, edges)
	result := compiler.validateGraph()

	if result.IsValid {
		t.Error("Expected validation to fail for missing entry point")
	}

	foundError := false
	for _, err := range result.Errors {
		if err.ErrorType == "missing_entry_point" {
			foundError = true
			break
		}
	}

	if !foundError {
		t.Error("Expected missing_entry_point error")
	}
}

// TestValidationCycleDetection tests cycle detection
func TestValidationCycleDetection(t *testing.T) {
	// Create a cycle: node1 -> node2 -> node1
	nodes := []Node{
		{
			ID:   "entry",
			Type: "EntryPoint",
			Data: map[string]interface{}{"label": "Start"},
		},
		{
			ID:   "node1",
			Type: "GetVar",
			Data: map[string]interface{}{"name": "x"},
		},
		{
			ID:   "node2",
			Type: "GetVar",
			Data: map[string]interface{}{"name": "y"},
		},
	}

	edges := []Edge{
		{ID: "e1", Source: "entry", Target: "node1"},
		{ID: "e2", Source: "node1", Target: "node2"},
		{ID: "e3", Source: "node2", Target: "node1"}, // Creates cycle
	}

	compiler := NewGraphCompiler()
	err := compiler.buildGraph(nodes, edges)

	if err != nil {
		t.Fatalf("Build graph failed: %v", err)
	}

	result := compiler.validateGraph()

	if result.IsValid {
		t.Error("Expected validation to fail for cycle")
	}

	foundCycleError := false
	for _, err := range result.Errors {
		if err.ErrorType == "cycle_detected" {
			foundCycleError = true
			break
		}
	}

	if !foundCycleError {
		t.Error("Expected cycle_detected error")
	}
}

// TestCompileGraphJSON tests the App.CompileGraph method with JSON input
func TestCompileGraphJSON(t *testing.T) {
	app := NewApp()

	nodes := []Node{
		{
			ID:   "entry",
			Type: "EntryPoint",
			Data: map[string]interface{}{"label": "Start"},
		},
		{
			ID:   "str1",
			Type: "StringNode",
			Data: map[string]interface{}{
				"value": "Test",
			},
		},
		{
			ID:   "print1",
			Type: "Print",
			Data: map[string]interface{}{"label": "Print"},
		},
	}

	edges := []Edge{
		{ID: "e1", Source: "entry", Target: "print1"},
		{ID: "e2", Source: "str1", Target: "print1"},
	}

	nodesJSON, _ := json.Marshal(nodes)
	edgesJSON, _ := json.Marshal(edges)

	resultJSON := app.CompileGraph(string(nodesJSON), string(edgesJSON))

	var result CompileGraphResult
	if err := json.Unmarshal([]byte(resultJSON), &result); err != nil {
		t.Fatalf("Failed to parse result: %v", err)
	}

	if !result.Success {
		t.Errorf("Compilation failed: %s", result.ErrorMessage)
	}

	if !strings.Contains(result.SourceCode, `print("Test")`) {
		t.Errorf("Expected print(\"Test\") in source, got:\n%s", result.SourceCode)
	}
}

// TestSourceMapErrorMapping tests error mapping functionality
func TestSourceMapErrorMapping(t *testing.T) {
	nodes := []Node{
		{
			ID:   "entry",
			Type: "EntryPoint",
			Data: map[string]interface{}{"label": "Start"},
		},
		{
			ID:   "print1",
			Type: "Print",
			Data: map[string]interface{}{"label": "Print"},
		},
	}

	edges := []Edge{
		{ID: "e1", Source: "entry", Target: "print1"},
	}

	compiler := NewGraphCompiler()
	_, sourceMap, err := compiler.CompileToSource(nodes, edges)

	if err != nil {
		t.Fatalf("Compilation failed: %v", err)
	}

	// Simulate a compiler error on line 1
	mappedError := compiler.MapCompilerError(1, "Test error message")

	if mappedError.NodeID != "print1" {
		t.Errorf("Expected error to map to print1, got: %s", mappedError.NodeID)
	}

	if mappedError.Message != "Test error message" {
		t.Errorf("Expected message 'Test error message', got: %s", mappedError.Message)
	}

	// Test FindMapping
	mapping := sourceMap.FindMapping(1, 0)
	if mapping == nil {
		t.Fatal("FindMapping returned nil for line 1")
	}

	if mapping.NodeID != "print1" {
		t.Errorf("Expected mapping to print1, got: %s", mapping.NodeID)
	}
}

// TestTopologicalSorting tests execution order
func TestTopologicalSorting(t *testing.T) {
	// Create a graph: entry -> var1 -> print1
	nodes := []Node{
		{
			ID:   "entry",
			Type: "EntryPoint",
			Data: map[string]interface{}{"label": "Start"},
		},
		{
			ID:   "var1",
			Type: "DeclareVar",
			Data: map[string]interface{}{"name": "x"},
		},
		{
			ID:   "num1",
			Type: "NumberNode",
			Data: map[string]interface{}{"value": 5.0},
		},
		{
			ID:   "print1",
			Type: "Print",
			Data: map[string]interface{}{"label": "Print"},
		},
		{
			ID:   "getvar1",
			Type: "GetVar",
			Data: map[string]interface{}{"name": "x"},
		},
	}

	edges := []Edge{
		{ID: "e1", Source: "entry", Target: "var1"},
		{ID: "e2", Source: "num1", Target: "var1"},
		{ID: "e3", Source: "var1", Target: "print1"},
		{ID: "e4", Source: "getvar1", Target: "print1"},
	}

	compiler := NewGraphCompiler()
	_, _, err := compiler.CompileToSource(nodes, edges)

	if err != nil {
		t.Fatalf("Compilation failed: %v", err)
	}

	// Verify execution order
	if len(compiler.executionOrder) == 0 {
		t.Fatal("Execution order is empty")
	}

	// Entry point should be first
	if compiler.executionOrder[0].Node.Type != "EntryPoint" {
		t.Errorf("Expected EntryPoint first, got: %s", compiler.executionOrder[0].Node.Type)
	}

	// Verify var1 comes before print1
	var1Index := -1
	print1Index := -1
	for i, node := range compiler.executionOrder {
		if node.Node.ID == "var1" {
			var1Index = i
		}
		if node.Node.ID == "print1" {
			print1Index = i
		}
	}

	if var1Index == -1 || print1Index == -1 {
		t.Fatal("Missing var1 or print1 in execution order")
	}

	if var1Index >= print1Index {
		t.Error("var1 should come before print1 in execution order")
	}
}

// TestArithmeticOperators tests arithmetic operator node compilation
func TestArithmeticOperators(t *testing.T) {
	// Create graph: 2 + 3
	nodes := []Node{
		{
			ID:   "entry",
			Type: "EntryPoint",
			Data: map[string]interface{}{"label": "Start"},
		},
		{
			ID:   "num1",
			Type: "NumberNode",
			Data: map[string]interface{}{"value": 2.0},
		},
		{
			ID:   "num2",
			Type: "NumberNode",
			Data: map[string]interface{}{"value": 3.0},
		},
		{
			ID:   "add1",
			Type: "Add",
			Data: map[string]interface{}{"label": "Add"},
		},
		{
			ID:   "print1",
			Type: "Print",
			Data: map[string]interface{}{"label": "Print"},
		},
	}

	edges := []Edge{
		{ID: "e1", Source: "entry", Target: "print1"},
		{ID: "e2", Source: "num1", Target: "add1"}, // left input
		{ID: "e3", Source: "num2", Target: "add1"}, // right input
		{ID: "e4", Source: "add1", Target: "print1"},
	}

	// Manually set up inputs for the Add node since port names matter
	compiler := NewGraphCompiler()
	compiler.buildGraph(nodes, edges)

	// Fix the Add node inputs to have both left and right
	addNode := compiler.nodes["add1"]
	addNode.Inputs["left"] = NodePort{NodeID: "num1", PortName: "value"}
	addNode.Inputs["right"] = NodePort{NodeID: "num2", PortName: "value"}

	source, _, err := compiler.CompileToSource(nodes, edges)

	if err != nil {
		t.Fatalf("Compilation failed: %v", err)
	}

	if !strings.Contains(source, "print((2 + 3))") {
		t.Errorf("Expected 'print((2 + 3))' in source, got:\n%s", source)
	}
}

// TestMultipleOperators tests nested arithmetic operations
func TestMultipleOperators(t *testing.T) {
	// Create graph: (2 * 3) + 4
	nodes := []Node{
		{
			ID:   "entry",
			Type: "EntryPoint",
			Data: map[string]interface{}{"label": "Start"},
		},
		{
			ID:   "num1",
			Type: "NumberNode",
			Data: map[string]interface{}{"value": 2.0},
		},
		{
			ID:   "num2",
			Type: "NumberNode",
			Data: map[string]interface{}{"value": 3.0},
		},
		{
			ID:   "num3",
			Type: "NumberNode",
			Data: map[string]interface{}{"value": 4.0},
		},
		{
			ID:   "mul1",
			Type: "Multiply",
			Data: map[string]interface{}{"label": "Multiply"},
		},
		{
			ID:   "add1",
			Type: "Add",
			Data: map[string]interface{}{"label": "Add"},
		},
		{
			ID:   "print1",
			Type: "Print",
			Data: map[string]interface{}{"label": "Print"},
		},
	}

	edges := []Edge{
		{ID: "e1", Source: "entry", Target: "print1"},
		{ID: "e2", Source: "num1", Target: "mul1"},
		{ID: "e3", Source: "num2", Target: "mul1"},
		{ID: "e4", Source: "mul1", Target: "add1"},
		{ID: "e5", Source: "num3", Target: "add1"},
		{ID: "e6", Source: "add1", Target: "print1"},
	}

	compiler := NewGraphCompiler()
	compiler.buildGraph(nodes, edges)

	// Manually set up the binary operator inputs
	mulNode := compiler.nodes["mul1"]
	mulNode.Inputs["left"] = NodePort{NodeID: "num1", PortName: "value"}
	mulNode.Inputs["right"] = NodePort{NodeID: "num2", PortName: "value"}

	addNode := compiler.nodes["add1"]
	addNode.Inputs["left"] = NodePort{NodeID: "mul1", PortName: "result"}
	addNode.Inputs["right"] = NodePort{NodeID: "num3", PortName: "value"}

	source, _, err := compiler.CompileToSource(nodes, edges)

	if err != nil {
		t.Fatalf("Compilation failed: %v", err)
	}

	// Should contain nested expression
	if !strings.Contains(source, "((2 * 3) + 4)") {
		t.Errorf("Expected '((2 * 3) + 4)' in source, got:\n%s", source)
	}
}
