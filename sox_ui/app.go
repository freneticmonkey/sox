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
