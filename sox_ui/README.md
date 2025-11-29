# Sox UI

## About

Sox UI is a graphical node editor frontend for the Sox programming language, built with [Wails](https://wails.io) and [React Flow](https://reactflow.dev). It provides an interactive visual interface for creating and editing node-based workflows.

## Features

- **Interactive Node Editor**: Visual node-based interface using React Flow
- **Node Management**: Add, delete, and connect nodes with drag-and-drop
- **Real-time Validation**: Validate your node graph structure
- **Statistics**: View node and edge counts
- **Save/Export**: Save your node graphs as JSON
- **Native Desktop App**: Cross-platform support (macOS, Windows, Linux)
- **Mini Map**: Bird's eye view of your node graph
- **Zoom Controls**: Pan and zoom around your workflow

## Getting Started

### Prerequisites

- Go 1.21 or later
- Node.js and npm
- Wails CLI: `go install github.com/wailsapp/wails/v2/cmd/wails@latest`

### Live Development

To run in live development mode from the project root:

```bash
make ui
```

Or from the sox_ui directory:

```bash
cd sox_ui
wails dev
```

This will run a Vite development server that provides very fast hot reload of your frontend changes. The application window will open automatically with the node editor interface.

### Using the Node Editor

1. **Add Nodes**: Click the "Add Node" button to create new nodes
2. **Connect Nodes**: Drag from one node's edge to another to create connections
3. **Move Nodes**: Drag nodes to reposition them
4. **Delete Nodes**: Select a node and press Delete/Backspace
5. **Get Stats**: Click "Get Stats" to see node/edge counts
6. **Validate**: Click "Validate" to check graph integrity
7. **Save**: Click "Save" to export the graph (saved to console)

### Building

To build a redistributable, production mode package:

```bash
wails build
```

The built application will be in the `build/bin` directory.

## Project Structure

- [app.go](app.go) - Go backend with node graph operations
  - `SaveFlow()` - Save node graph as JSON
  - `ValidateFlow()` - Validate graph structure
  - `GetFlowStats()` - Get node/edge statistics
- [main.go](main.go) - Application entry point and Wails configuration
- [frontend/](frontend/) - React frontend with React Flow
  - [frontend/src/App.jsx](frontend/src/App.jsx) - Main React Flow editor component
  - [frontend/src/App.css](frontend/src/App.css) - Styles

## Backend API

The Go backend provides the following methods:

### `SaveFlow(nodesJSON, edgesJSON) (string, error)`
Saves the current flow graph as formatted JSON.

### `ValidateFlow(nodesJSON, edgesJSON) (bool, string)`
Validates the flow graph structure and returns validation status and message.

### `GetFlowStats(nodesJSON, edgesJSON) string`
Returns statistics about the current flow (node count, edge count).

## Configuration

You can configure the project by editing [wails.json](wails.json). More information about the project settings can be found here: https://wails.io/docs/reference/project-config

## Technology Stack

- **Backend**: Go with Wails v2
- **Frontend**: React 18 with Vite
- **Node Editor**: React Flow ([@xyflow/react](https://www.npmjs.com/package/@xyflow/react))
- **Bundler**: Vite 3
