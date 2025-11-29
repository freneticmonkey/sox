package main

import (
	"fmt"
	"strings"
)

// GraphCompiler compiles visual node graphs to Sox source code
type GraphCompiler struct {
	nodes          map[string]*CompiledNode
	edges          []Edge
	symbolTable    *SymbolTable
	errors         []CompilationError
	sourceMap      *SourceMap
	executionOrder []*CompiledNode
}

// CompiledNode wraps a node with compilation metadata
type CompiledNode struct {
	Node    Node
	Inputs  map[string]NodePort   // Port name -> source port
	Outputs map[string][]NodePort // Port name -> target ports
	Order   int                   // Execution order
	Visited bool                  // For topological sort
	InStack bool                  // For cycle detection
}

// NodePort identifies a specific port on a node
type NodePort struct {
	NodeID   string
	PortName string
}

// SymbolTable tracks variables and their scopes
type SymbolTable struct {
	Scopes       []*Scope
	CurrentDepth int
}

// Scope represents a lexical scope
type Scope struct {
	Variables map[string]*Variable
	Parent    *Scope
	Depth     int
}

// Variable tracks a variable's metadata
type Variable struct {
	Name       string
	Index      int
	Depth      int
	IsCaptured bool
	IsGlobal   bool
}

// SourceMap maps generated source lines to original nodes
type SourceMap struct {
	GeneratedSource string
	Mappings        []SourceMapping
}

// SourceMapping maps a source line to a node
type SourceMapping struct {
	SourceLine   int
	SourceColumn int
	NodeID       string
	NodePort     string
}

// CompilationError represents an error during compilation
type CompilationError struct {
	NodeID     string
	Message    string
	Suggestion string
	ErrorType  string
}

// ValidationResult contains validation results
type ValidationResult struct {
	IsValid bool
	Errors  []CompilationError
}

// NewGraphCompiler creates a new graph compiler
func NewGraphCompiler() *GraphCompiler {
	return &GraphCompiler{
		nodes:       make(map[string]*CompiledNode),
		symbolTable: NewSymbolTable(),
		sourceMap:   &SourceMap{Mappings: []SourceMapping{}},
		errors:      []CompilationError{},
	}
}

// NewSymbolTable creates a new symbol table
func NewSymbolTable() *SymbolTable {
	globalScope := &Scope{
		Variables: make(map[string]*Variable),
		Parent:    nil,
		Depth:     0,
	}

	return &SymbolTable{
		Scopes:       []*Scope{globalScope},
		CurrentDepth: 0,
	}
}

// CompileToSource compiles a graph to Sox source code
func (gc *GraphCompiler) CompileToSource(nodes []Node, edges []Edge) (string, *SourceMap, error) {
	// Phase 1: Build graph structure
	if err := gc.buildGraph(nodes, edges); err != nil {
		return "", nil, err
	}

	// Phase 2: Validate graph
	if result := gc.validateGraph(); !result.IsValid {
		return "", nil, fmt.Errorf("validation failed: %v", result.Errors)
	}

	// Phase 3: Topological sort
	if err := gc.topologicalSort(); err != nil {
		return "", nil, err
	}

	// Phase 4: Generate source code
	source, err := gc.generateSource()
	if err != nil {
		return "", nil, err
	}

	gc.sourceMap.GeneratedSource = source
	return source, gc.sourceMap, nil
}

// buildGraph constructs the internal graph representation
func (gc *GraphCompiler) buildGraph(nodes []Node, edges []Edge) error {
	gc.edges = edges

	// Create compiled nodes
	for _, node := range nodes {
		compiledNode := &CompiledNode{
			Node:    node,
			Inputs:  make(map[string]NodePort),
			Outputs: make(map[string][]NodePort),
			Order:   -1,
		}
		gc.nodes[node.ID] = compiledNode
	}

	// Build connections
	for _, edge := range edges {
		sourceNode, sourceExists := gc.nodes[edge.Source]
		targetNode, targetExists := gc.nodes[edge.Target]

		if !sourceExists {
			return fmt.Errorf("edge references non-existent source node: %s", edge.Source)
		}
		if !targetExists {
			return fmt.Errorf("edge references non-existent target node: %s", edge.Target)
		}

		// Determine port names from edge data (or use defaults)
		sourcePort := "output" // Default output port
		targetPort := "input"  // Default input port

		// Add to outputs of source node
		nodePort := NodePort{NodeID: edge.Target, PortName: targetPort}
		sourceNode.Outputs[sourcePort] = append(sourceNode.Outputs[sourcePort], nodePort)

		// Add to inputs of target node
		targetNode.Inputs[targetPort] = NodePort{NodeID: edge.Source, PortName: sourcePort}
	}

	return nil
}

// validateGraph performs validation checks
func (gc *GraphCompiler) validateGraph() ValidationResult {
	result := ValidationResult{IsValid: true, Errors: []CompilationError{}}

	// Check for entry point
	hasEntryPoint := false
	for _, node := range gc.nodes {
		if node.Node.Type == "EntryPoint" {
			hasEntryPoint = true
			break
		}
	}

	if !hasEntryPoint {
		result.IsValid = false
		result.Errors = append(result.Errors, CompilationError{
			NodeID:     "",
			Message:    "Graph must have an EntryPoint node",
			Suggestion: "Add an EntryPoint node to mark where execution begins",
			ErrorType:  "missing_entry_point",
		})
	}

	// Check for cycles in data flow (control flow cycles are allowed)
	if err := gc.detectDataCycles(); err != nil {
		result.IsValid = false
		result.Errors = append(result.Errors, CompilationError{
			NodeID:     "",
			Message:    err.Error(),
			Suggestion: "Remove circular data dependencies",
			ErrorType:  "cycle_detected",
		})
	}

	// Validate each node type
	for _, node := range gc.nodes {
		if err := gc.validateNodeType(node); err != nil {
			result.IsValid = false
			result.Errors = append(result.Errors, CompilationError{
				NodeID:    node.Node.ID,
				Message:   err.Error(),
				ErrorType: "invalid_node",
			})
		}
	}

	return result
}

// detectDataCycles detects cycles in data dependencies
func (gc *GraphCompiler) detectDataCycles() error {
	// Reset visited flags
	for _, node := range gc.nodes {
		node.Visited = false
		node.InStack = false
	}

	// DFS from each unvisited node
	for _, node := range gc.nodes {
		if !node.Visited {
			if err := gc.detectCycleDFS(node); err != nil {
				return err
			}
		}
	}

	return nil
}

// detectCycleDFS performs DFS for cycle detection
func (gc *GraphCompiler) detectCycleDFS(node *CompiledNode) error {
	node.Visited = true
	node.InStack = true

	// Visit all output nodes
	for _, outputs := range node.Outputs {
		for _, output := range outputs {
			targetNode := gc.nodes[output.NodeID]

			if !targetNode.Visited {
				if err := gc.detectCycleDFS(targetNode); err != nil {
					return err
				}
			} else if targetNode.InStack {
				return fmt.Errorf("cycle detected involving node %s", node.Node.ID)
			}
		}
	}

	node.InStack = false
	return nil
}

// validateNodeType validates a node's configuration
func (gc *GraphCompiler) validateNodeType(node *CompiledNode) error {
	// Basic validation - check required data fields
	nodeType := node.Node.Type

	switch nodeType {
	case "NumberNode", "StringNode":
		if _, ok := node.Node.Data["value"]; !ok {
			return fmt.Errorf("node %s: %s requires 'value' field", node.Node.ID, nodeType)
		}
	case "DeclareVar", "GetVar", "SetVar":
		if _, ok := node.Node.Data["name"]; !ok {
			return fmt.Errorf("node %s: %s requires 'name' field", node.Node.ID, nodeType)
		}
	}

	return nil
}

// topologicalSort orders nodes for execution
func (gc *GraphCompiler) topologicalSort() error {
	// Reset visited flags
	for _, node := range gc.nodes {
		node.Visited = false
	}

	gc.executionOrder = []*CompiledNode{}

	// Find entry point
	var entryPoint *CompiledNode
	for _, node := range gc.nodes {
		if node.Node.Type == "EntryPoint" {
			entryPoint = node
			break
		}
	}

	if entryPoint == nil {
		return fmt.Errorf("no entry point found")
	}

	// DFS from entry point
	if err := gc.topologicalSortDFS(entryPoint); err != nil {
		return err
	}

	// Assign order indices
	for i, node := range gc.executionOrder {
		node.Order = i
	}

	return nil
}

// topologicalSortDFS performs DFS for topological sorting
func (gc *GraphCompiler) topologicalSortDFS(node *CompiledNode) error {
	if node.Visited {
		return nil
	}

	node.Visited = true

	// Visit dependencies first (inputs)
	for _, input := range node.Inputs {
		inputNode := gc.nodes[input.NodeID]
		if err := gc.topologicalSortDFS(inputNode); err != nil {
			return err
		}
	}

	// Add to execution order
	gc.executionOrder = append(gc.executionOrder, node)

	// Visit outputs
	for _, outputs := range node.Outputs {
		for _, output := range outputs {
			outputNode := gc.nodes[output.NodeID]
			if err := gc.topologicalSortDFS(outputNode); err != nil {
				return err
			}
		}
	}

	return nil
}

// generateSource generates Sox source code from the ordered nodes
func (gc *GraphCompiler) generateSource() (string, error) {
	var source strings.Builder
	currentLine := 1

	// Generate code for each node in execution order
	for _, node := range gc.executionOrder {
		// Skip entry point (it's just a marker)
		if node.Node.Type == "EntryPoint" {
			continue
		}

		// Track source mapping
		gc.sourceMap.Mappings = append(gc.sourceMap.Mappings, SourceMapping{
			SourceLine: currentLine,
			NodeID:     node.Node.ID,
		})

		// Generate code based on node type
		code, err := gc.generateNodeSource(node)
		if err != nil {
			return "", fmt.Errorf("error generating code for node %s: %w", node.Node.ID, err)
		}

		if code != "" {
			source.WriteString(code)
			source.WriteString("\n")
			currentLine++
		}
	}

	return source.String(), nil
}

// generateNodeSource generates source code for a single node
func (gc *GraphCompiler) generateNodeSource(node *CompiledNode) (string, error) {
	switch node.Node.Type {
	case "NumberNode":
		// Number literals will be used inline in expressions
		return "", nil

	case "StringNode":
		// String literals will be used inline in expressions
		return "", nil

	case "BooleanNode":
		// Boolean literals will be used inline
		return "", nil

	case "Print":
		// Get the input value
		if input, ok := node.Inputs["value"]; ok {
			inputNode := gc.nodes[input.NodeID]
			value := gc.getNodeValue(inputNode)
			return fmt.Sprintf("print(%s)", value), nil
		}
		return "", fmt.Errorf("Print node missing value input")

	case "DeclareVar":
		name, _ := node.Node.Data["name"].(string)
		// Check if there's an initial value input
		if input, ok := node.Inputs["value"]; ok {
			inputNode := gc.nodes[input.NodeID]
			value := gc.getNodeValue(inputNode)
			return fmt.Sprintf("var %s = %s", name, value), nil
		}
		return fmt.Sprintf("var %s", name), nil

	default:
		return fmt.Sprintf("// Unsupported node type: %s", node.Node.Type), nil
	}
}

// getNodeValue gets the value representation of a node
func (gc *GraphCompiler) getNodeValue(node *CompiledNode) string {
	switch node.Node.Type {
	case "NumberNode":
		if val, ok := node.Node.Data["value"].(float64); ok {
			return fmt.Sprintf("%v", val)
		}
	case "StringNode":
		if val, ok := node.Node.Data["value"].(string); ok {
			return fmt.Sprintf(`"%s"`, val)
		}
	case "BooleanNode":
		if val, ok := node.Node.Data["value"].(bool); ok {
			return fmt.Sprintf("%v", val)
		}
	case "GetVar":
		if name, ok := node.Node.Data["name"].(string); ok {
			return name
		}
	}
	return "nil"
}

// FindMapping finds the source mapping for a given line
func (sm *SourceMap) FindMapping(line, column int) *SourceMapping {
	for i := range sm.Mappings {
		if sm.Mappings[i].SourceLine == line {
			return &sm.Mappings[i]
		}
	}
	return nil
}

// MapCompilerError maps a compiler error back to a node
func (gc *GraphCompiler) MapCompilerError(errorLine int, errorMessage string) CompilationError {
	mapping := gc.sourceMap.FindMapping(errorLine, 0)

	nodeID := ""
	if mapping != nil {
		nodeID = mapping.NodeID
	}

	return CompilationError{
		NodeID:    nodeID,
		Message:   errorMessage,
		ErrorType: "compiler_error",
	}
}
