import { useCallback, useState } from 'react';
import {
  ReactFlow,
  MiniMap,
  Controls,
  Background,
  useNodesState,
  useEdgesState,
  addEdge,
  Panel,
} from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import './App.css';
import { GetFlowStats, ValidateFlow, SaveFlow, CompileGraph } from '../wailsjs/go/main/App';
import NodePalette from './NodePalette';
import { nodeTypes } from './CustomNodes';

const initialNodes = [
  { id: '1', position: { x: 250, y: 50 }, data: { label: 'Start Node' }, type: 'input' },
  { id: '2', position: { x: 250, y: 150 }, data: { label: 'Process Node' } },
  { id: '3', position: { x: 250, y: 250 }, data: { label: 'End Node' }, type: 'output' },
];

const initialEdges = [
  { id: 'e1-2', source: '1', target: '2' },
  { id: 'e2-3', source: '2', target: '3' },
];

function App() {
  const [nodes, setNodes, onNodesChange] = useNodesState(initialNodes);
  const [edges, setEdges, onEdgesChange] = useEdgesState(initialEdges);
  const [nodeId, setNodeId] = useState(4);
  const [stats, setStats] = useState('');
  const [message, setMessage] = useState('');
  const [compiledSource, setCompiledSource] = useState('');
  const [showSource, setShowSource] = useState(false);
  const [paletteOpen, setPaletteOpen] = useState(true);

  const onConnect = useCallback(
    (params) => setEdges((eds) => addEdge(params, eds)),
    [setEdges],
  );

  const addNode = () => {
    const newNode = {
      id: `${nodeId}`,
      position: { x: Math.random() * 400, y: Math.random() * 400 },
      data: { label: `Node ${nodeId}` },
    };
    setNodes((nds) => nds.concat(newNode));
    setNodeId((id) => id + 1);
  };

  const clearNodes = () => {
    setNodes([]);
    setEdges([]);
    setStats('');
    setMessage('');
  };

  const updateStats = async () => {
    try {
      const result = await GetFlowStats(JSON.stringify(nodes), JSON.stringify(edges));
      setStats(result);
      setMessage('');
    } catch (err) {
      setMessage(`Error: ${err}`);
    }
  };

  const validateGraph = async () => {
    try {
      const [isValid, msg] = await ValidateFlow(JSON.stringify(nodes), JSON.stringify(edges));
      setMessage(`${isValid ? '✓' : '✗'} ${msg}`);
    } catch (err) {
      setMessage(`Error: ${err}`);
    }
  };

  const saveGraph = async () => {
    try {
      const result = await SaveFlow(JSON.stringify(nodes), JSON.stringify(edges));
      console.log('Saved flow:', result);
      setMessage('✓ Flow saved to console');
    } catch (err) {
      setMessage(`Error: ${err}`);
    }
  };

  const compileGraph = async () => {
    try {
      const result = await CompileGraph(JSON.stringify(nodes), JSON.stringify(edges));
      const parsed = JSON.parse(result);

      if (parsed.success) {
        setCompiledSource(parsed.sourceCode);
        setShowSource(true);
        setMessage('✓ Compilation successful!');
      } else {
        setMessage(`✗ Compilation failed: ${parsed.errorMessage}`);
        setCompiledSource('');
        setShowSource(false);
      }
    } catch (err) {
      setMessage(`Error: ${err}`);
      setCompiledSource('');
      setShowSource(false);
    }
  };

  const handleAddNodeFromPalette = (nodeType, nodeLabel) => {
    const newNodeId = `${nodeId}`;
    const newNode = {
      id: newNodeId,
      type: getReactFlowNodeType(nodeType),
      position: {
        x: 250 + Math.random() * 100,
        y: 150 + Math.random() * 100,
      },
      data: getNodeData(nodeType, nodeLabel),
    };

    setNodes((nds) => nds.concat(newNode));
    setNodeId((id) => id + 1);
    setMessage(`✓ Added ${nodeLabel} node`);
  };

  const getReactFlowNodeType = (soxNodeType) => {
    // Map Sox node types to React Flow visual types
    if (soxNodeType === 'EntryPoint') return 'input';
    if (soxNodeType === 'Print') return 'output';
    return 'default';
  };

  const getNodeData = (nodeType, nodeLabel) => {
    const data = {
      label: nodeLabel,
      nodeType: nodeType, // Store the Sox node type
    };

    // Set default values based on node type
    switch (nodeType) {
      case 'NumberNode':
        data.value = 0;
        break;
      case 'StringNode':
        data.value = '';
        break;
      case 'BooleanNode':
        data.value = true;
        break;
      case 'DeclareVar':
      case 'GetVar':
      case 'SetVar':
        data.name = 'x';
        break;
      default:
        break;
    }

    return data;
  };

  return (
    <div style={{ width: '100vw', height: '100vh', position: 'relative' }}>
      <ReactFlow
        nodes={nodes}
        edges={edges}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        onConnect={onConnect}
        nodeTypes={nodeTypes}
        fitView
      >
        <Controls />
        <MiniMap />
        <Background variant="dots" gap={12} size={1} />
        <Panel position="top-left">
          <div style={{
            padding: '15px',
            background: 'white',
            borderRadius: '8px',
            boxShadow: '0 4px 6px rgba(0,0,0,0.1)',
            minWidth: '250px',
            maxWidth: '350px',
          }}>
            <h3 style={{ margin: '0 0 15px 0', color: '#333', fontSize: '18px' }}>Sox Node Editor</h3>

            <div style={{ marginBottom: '15px' }}>
              <button
                onClick={addNode}
                style={{
                  padding: '8px 16px',
                  marginRight: '8px',
                  marginBottom: '8px',
                  cursor: 'pointer',
                  background: '#1a192b',
                  color: 'white',
                  border: 'none',
                  borderRadius: '4px',
                  fontSize: '14px'
                }}
              >
                Add Node
              </button>
              <button
                onClick={clearNodes}
                style={{
                  padding: '8px 16px',
                  marginBottom: '8px',
                  cursor: 'pointer',
                  background: '#ef4444',
                  color: 'white',
                  border: 'none',
                  borderRadius: '4px',
                  fontSize: '14px'
                }}
              >
                Clear All
              </button>
            </div>

            <div style={{ marginBottom: '15px', borderTop: '1px solid #e5e7eb', paddingTop: '15px' }}>
              <button
                onClick={updateStats}
                style={{
                  padding: '8px 16px',
                  marginRight: '8px',
                  marginBottom: '8px',
                  cursor: 'pointer',
                  background: '#3b82f6',
                  color: 'white',
                  border: 'none',
                  borderRadius: '4px',
                  fontSize: '14px'
                }}
              >
                Get Stats
              </button>
              <button
                onClick={validateGraph}
                style={{
                  padding: '8px 16px',
                  marginRight: '8px',
                  marginBottom: '8px',
                  cursor: 'pointer',
                  background: '#10b981',
                  color: 'white',
                  border: 'none',
                  borderRadius: '4px',
                  fontSize: '14px'
                }}
              >
                Validate
              </button>
              <button
                onClick={saveGraph}
                style={{
                  padding: '8px 16px',
                  marginBottom: '8px',
                  marginRight: '8px',
                  cursor: 'pointer',
                  background: '#8b5cf6',
                  color: 'white',
                  border: 'none',
                  borderRadius: '4px',
                  fontSize: '14px'
                }}
              >
                Save
              </button>
              <button
                onClick={compileGraph}
                style={{
                  padding: '8px 16px',
                  marginBottom: '8px',
                  cursor: 'pointer',
                  background: '#f59e0b',
                  color: 'white',
                  border: 'none',
                  borderRadius: '4px',
                  fontSize: '14px',
                  fontWeight: 'bold'
                }}
              >
                🔨 Compile
              </button>
            </div>

            {stats && (
              <div style={{
                padding: '8px',
                background: '#f3f4f6',
                borderRadius: '4px',
                marginBottom: '8px',
                fontSize: '13px',
                color: '#374151'
              }}>
                {stats}
              </div>
            )}

            {message && (
              <div style={{
                padding: '8px',
                background: message.includes('✓') ? '#d1fae5' : '#fee2e2',
                borderRadius: '4px',
                fontSize: '13px',
                color: message.includes('✓') ? '#065f46' : '#991b1b'
              }}>
                {message}
              </div>
            )}

            {showSource && compiledSource && (
              <div style={{
                marginTop: '15px',
                borderTop: '1px solid #e5e7eb',
                paddingTop: '15px'
              }}>
                <div style={{
                  display: 'flex',
                  justifyContent: 'space-between',
                  alignItems: 'center',
                  marginBottom: '8px'
                }}>
                  <h4 style={{
                    margin: 0,
                    fontSize: '14px',
                    color: '#374151',
                    fontWeight: '600'
                  }}>
                    Generated Sox Code
                  </h4>
                  <button
                    onClick={() => setShowSource(false)}
                    style={{
                      padding: '4px 8px',
                      cursor: 'pointer',
                      background: '#e5e7eb',
                      color: '#374151',
                      border: 'none',
                      borderRadius: '4px',
                      fontSize: '12px'
                    }}
                  >
                    ✕
                  </button>
                </div>
                <pre style={{
                  padding: '12px',
                  background: '#1e293b',
                  color: '#e2e8f0',
                  borderRadius: '4px',
                  fontSize: '12px',
                  fontFamily: 'Consolas, Monaco, monospace',
                  maxHeight: '200px',
                  overflow: 'auto',
                  margin: 0,
                  whiteSpace: 'pre-wrap',
                  wordBreak: 'break-word'
                }}>
                  {compiledSource}
                </pre>
              </div>
            )}
          </div>
        </Panel>
      </ReactFlow>
      <NodePalette
        onAddNode={handleAddNodeFromPalette}
        isOpen={paletteOpen}
        onToggle={() => setPaletteOpen(!paletteOpen)}
      />
    </div>
  );
}

export default App;
