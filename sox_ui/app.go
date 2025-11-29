package main

import (
	"context"
	"encoding/json"
	"fmt"
)

// App struct
type App struct {
	ctx context.Context
}

// NewApp creates a new App application struct
func NewApp() *App {
	return &App{}
}

// startup is called when the app starts. The context is saved
// so we can call the runtime methods
func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
}

// Node represents a flow node
type Node struct {
	ID       string                 `json:"id"`
	Type     string                 `json:"type"`
	Position map[string]float64     `json:"position"`
	Data     map[string]interface{} `json:"data"`
}

// Edge represents a connection between nodes
type Edge struct {
	ID     string `json:"id"`
	Source string `json:"source"`
	Target string `json:"target"`
}

// FlowData represents the complete flow graph
type FlowData struct {
	Nodes []Node `json:"nodes"`
	Edges []Edge `json:"edges"`
}

// SaveFlow saves the current flow to a JSON string
func (a *App) SaveFlow(nodesJSON string, edgesJSON string) (string, error) {
	var nodes []Node
	var edges []Edge

	if err := json.Unmarshal([]byte(nodesJSON), &nodes); err != nil {
		return "", fmt.Errorf("failed to parse nodes: %w", err)
	}

	if err := json.Unmarshal([]byte(edgesJSON), &edges); err != nil {
		return "", fmt.Errorf("failed to parse edges: %w", err)
	}

	flowData := FlowData{
		Nodes: nodes,
		Edges: edges,
	}

	result, err := json.MarshalIndent(flowData, "", "  ")
	if err != nil {
		return "", fmt.Errorf("failed to marshal flow data: %w", err)
	}

	return string(result), nil
}

// ValidateFlow validates the flow graph
func (a *App) ValidateFlow(nodesJSON string, edgesJSON string) (bool, string) {
	var nodes []Node
	var edges []Edge

	if err := json.Unmarshal([]byte(nodesJSON), &nodes); err != nil {
		return false, fmt.Sprintf("Invalid nodes data: %v", err)
	}

	if err := json.Unmarshal([]byte(edgesJSON), &edges); err != nil {
		return false, fmt.Sprintf("Invalid edges data: %v", err)
	}

	if len(nodes) == 0 {
		return false, "Flow must contain at least one node"
	}

	// Check for orphaned edges
	nodeIDs := make(map[string]bool)
	for _, node := range nodes {
		nodeIDs[node.ID] = true
	}

	for _, edge := range edges {
		if !nodeIDs[edge.Source] {
			return false, fmt.Sprintf("Edge references non-existent source node: %s", edge.Source)
		}
		if !nodeIDs[edge.Target] {
			return false, fmt.Sprintf("Edge references non-existent target node: %s", edge.Target)
		}
	}

	return true, "Flow is valid"
}

// GetFlowStats returns statistics about the flow
func (a *App) GetFlowStats(nodesJSON string, edgesJSON string) string {
	var nodes []Node
	var edges []Edge

	json.Unmarshal([]byte(nodesJSON), &nodes)
	json.Unmarshal([]byte(edgesJSON), &edges)

	return fmt.Sprintf("Nodes: %d, Edges: %d", len(nodes), len(edges))
}

// CompileGraphResult contains compilation results
type CompileGraphResult struct {
	Success      bool               `json:"success"`
	SourceCode   string             `json:"sourceCode"`
	SourceMap    *SourceMap         `json:"sourceMap"`
	Errors       []CompilationError `json:"errors"`
	ErrorMessage string             `json:"errorMessage"`
}

// CompileGraph compiles a visual graph to Sox source code
func (a *App) CompileGraph(nodesJSON string, edgesJSON string) string {
	var nodes []Node
	var edges []Edge

	// Parse input
	if err := json.Unmarshal([]byte(nodesJSON), &nodes); err != nil {
		result := CompileGraphResult{
			Success:      false,
			ErrorMessage: fmt.Sprintf("Failed to parse nodes: %v", err),
		}
		resultJSON, _ := json.Marshal(result)
		return string(resultJSON)
	}

	if err := json.Unmarshal([]byte(edgesJSON), &edges); err != nil {
		result := CompileGraphResult{
			Success:      false,
			ErrorMessage: fmt.Sprintf("Failed to parse edges: %v", err),
		}
		resultJSON, _ := json.Marshal(result)
		return string(resultJSON)
	}

	// Compile
	compiler := NewGraphCompiler()
	source, sourceMap, err := compiler.CompileToSource(nodes, edges)

	if err != nil {
		result := CompileGraphResult{
			Success:      false,
			ErrorMessage: fmt.Sprintf("Compilation failed: %v", err),
			Errors:       compiler.errors,
		}
		resultJSON, _ := json.Marshal(result)
		return string(resultJSON)
	}

	// Success
	result := CompileGraphResult{
		Success:    true,
		SourceCode: source,
		SourceMap:  sourceMap,
	}
	resultJSON, _ := json.Marshal(result)
	return string(resultJSON)
}

// ValidateGraphDetailed performs detailed graph validation
func (a *App) ValidateGraphDetailed(nodesJSON string, edgesJSON string) string {
	var nodes []Node
	var edges []Edge

	// Parse input
	if err := json.Unmarshal([]byte(nodesJSON), &nodes); err != nil {
		result := ValidationResult{
			IsValid: false,
			Errors: []CompilationError{
				{
					Message:   fmt.Sprintf("Failed to parse nodes: %v", err),
					ErrorType: "parse_error",
				},
			},
		}
		resultJSON, _ := json.Marshal(result)
		return string(resultJSON)
	}

	if err := json.Unmarshal([]byte(edgesJSON), &edges); err != nil {
		result := ValidationResult{
			IsValid: false,
			Errors: []CompilationError{
				{
					Message:   fmt.Sprintf("Failed to parse edges: %v", err),
					ErrorType: "parse_error",
				},
			},
		}
		resultJSON, _ := json.Marshal(result)
		return string(resultJSON)
	}

	// Validate
	compiler := NewGraphCompiler()
	compiler.buildGraph(nodes, edges)
	validationResult := compiler.validateGraph()

	resultJSON, _ := json.Marshal(validationResult)
	return string(resultJSON)
}
