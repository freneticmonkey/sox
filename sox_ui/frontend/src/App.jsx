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
import { GetFlowStats, ValidateFlow, SaveFlow } from '../wailsjs/go/main/App';

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

  return (
    <div style={{ width: '100vw', height: '100vh' }}>
      <ReactFlow
        nodes={nodes}
        edges={edges}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        onConnect={onConnect}
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
            minWidth: '250px'
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
          </div>
        </Panel>
      </ReactFlow>
    </div>
  );
}

export default App;
