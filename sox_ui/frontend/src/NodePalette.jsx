import { useState } from 'react';

const nodeCategories = {
  literals: {
    title: 'Literals & Values',
    color: '#4CAF50',
    nodes: [
      { type: 'EntryPoint', label: 'Entry Point', icon: '🚀', description: 'Program start' },
      { type: 'NumberNode', label: 'Number', icon: '🔢', description: 'Numeric literal' },
      { type: 'StringNode', label: 'String', icon: '📝', description: 'Text literal' },
      { type: 'BooleanNode', label: 'Boolean', icon: '✓', description: 'True/False value' },
      { type: 'NilNode', label: 'Nil', icon: '∅', description: 'Null value' },
    ]
  },
  variables: {
    title: 'Variables',
    color: '#2196F3',
    nodes: [
      { type: 'DeclareVar', label: 'Declare Var', icon: '📦', description: 'Create variable' },
      { type: 'GetVar', label: 'Get Var', icon: '📤', description: 'Read variable' },
      { type: 'SetVar', label: 'Set Var', icon: '📥', description: 'Update variable' },
    ]
  },
  arithmetic: {
    title: 'Arithmetic',
    color: '#FF9800',
    nodes: [
      { type: 'Add', label: 'Add (+)', icon: '➕', description: 'Addition' },
      { type: 'Subtract', label: 'Subtract (-)', icon: '➖', description: 'Subtraction' },
      { type: 'Multiply', label: 'Multiply (*)', icon: '✖️', description: 'Multiplication' },
      { type: 'Divide', label: 'Divide (/)', icon: '➗', description: 'Division' },
      { type: 'Negate', label: 'Negate (-)', icon: '⊖', description: 'Unary negation' },
    ]
  },
  comparison: {
    title: 'Comparison',
    color: '#9C27B0',
    nodes: [
      { type: 'Equal', label: 'Equal (==)', icon: '=', description: 'Test equality' },
      { type: 'Greater', label: 'Greater (>)', icon: '>', description: 'Greater than' },
      { type: 'Less', label: 'Less (<)', icon: '<', description: 'Less than' },
    ]
  },
  logical: {
    title: 'Logical',
    color: '#E91E63',
    nodes: [
      { type: 'And', label: 'And', icon: '∧', description: 'Logical AND' },
      { type: 'Or', label: 'Or', icon: '∨', description: 'Logical OR' },
      { type: 'Not', label: 'Not', icon: '¬', description: 'Logical NOT' },
    ]
  },
  io: {
    title: 'Input/Output',
    color: '#607D8B',
    nodes: [
      { type: 'Print', label: 'Print', icon: '🖨️', description: 'Output to console' },
    ]
  },
};

function NodePalette({ onAddNode, isOpen, onToggle }) {
  const [searchTerm, setSearchTerm] = useState('');
  const [expandedCategories, setExpandedCategories] = useState({
    literals: true,
    variables: true,
    arithmetic: true,
    comparison: false,
    logical: false,
    io: true,
  });

  const toggleCategory = (category) => {
    setExpandedCategories(prev => ({
      ...prev,
      [category]: !prev[category]
    }));
  };

  const handleAddNode = (nodeType, nodeLabel) => {
    onAddNode(nodeType, nodeLabel);
  };

  const filterNodes = (nodes) => {
    if (!searchTerm) return nodes;
    return nodes.filter(node =>
      node.label.toLowerCase().includes(searchTerm.toLowerCase()) ||
      node.type.toLowerCase().includes(searchTerm.toLowerCase()) ||
      node.description.toLowerCase().includes(searchTerm.toLowerCase())
    );
  };

  if (!isOpen) {
    return (
      <div style={{
        position: 'absolute',
        right: '20px',
        top: '50%',
        transform: 'translateY(-50%)',
        zIndex: 10,
      }}>
        <button
          onClick={onToggle}
          style={{
            padding: '12px 16px',
            background: '#1a192b',
            color: 'white',
            border: 'none',
            borderRadius: '8px 0 0 8px',
            cursor: 'pointer',
            fontSize: '16px',
            boxShadow: '0 4px 6px rgba(0,0,0,0.1)',
            writingMode: 'vertical-rl',
            textOrientation: 'mixed',
          }}
          title="Open Node Palette"
        >
          📦 Nodes
        </button>
      </div>
    );
  }

  return (
    <div style={{
      position: 'absolute',
      right: 0,
      top: 0,
      bottom: 0,
      width: '320px',
      background: 'white',
      borderLeft: '1px solid #e5e7eb',
      boxShadow: '-4px 0 6px rgba(0,0,0,0.1)',
      display: 'flex',
      flexDirection: 'column',
      zIndex: 10,
      overflowY: 'auto',
    }}>
      {/* Header */}
      <div style={{
        padding: '16px',
        borderBottom: '1px solid #e5e7eb',
        background: '#f9fafb',
      }}>
        <div style={{
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          marginBottom: '12px',
        }}>
          <h3 style={{
            margin: 0,
            fontSize: '18px',
            fontWeight: '600',
            color: '#1a192b',
          }}>
            Node Palette
          </h3>
          <button
            onClick={onToggle}
            style={{
              padding: '4px 8px',
              background: '#e5e7eb',
              color: '#374151',
              border: 'none',
              borderRadius: '4px',
              cursor: 'pointer',
              fontSize: '14px',
            }}
            title="Close Palette"
          >
            ✕
          </button>
        </div>

        {/* Search */}
        <input
          type="text"
          placeholder="Search nodes..."
          value={searchTerm}
          onChange={(e) => setSearchTerm(e.target.value)}
          style={{
            width: '100%',
            padding: '8px 12px',
            border: '1px solid #d1d5db',
            borderRadius: '6px',
            fontSize: '14px',
            outline: 'none',
            boxSizing: 'border-box',
          }}
          onFocus={(e) => e.target.style.borderColor = '#3b82f6'}
          onBlur={(e) => e.target.style.borderColor = '#d1d5db'}
        />
      </div>

      {/* Categories */}
      <div style={{
        flex: 1,
        overflowY: 'auto',
        padding: '12px',
      }}>
        {Object.entries(nodeCategories).map(([categoryKey, category]) => {
          const filteredNodes = filterNodes(category.nodes);
          if (searchTerm && filteredNodes.length === 0) return null;

          return (
            <div key={categoryKey} style={{ marginBottom: '12px' }}>
              {/* Category Header */}
              <button
                onClick={() => toggleCategory(categoryKey)}
                style={{
                  width: '100%',
                  padding: '10px 12px',
                  background: category.color + '15',
                  border: `1px solid ${category.color}40`,
                  borderRadius: '6px',
                  cursor: 'pointer',
                  display: 'flex',
                  justifyContent: 'space-between',
                  alignItems: 'center',
                  marginBottom: '8px',
                }}
              >
                <span style={{
                  fontWeight: '600',
                  fontSize: '14px',
                  color: category.color,
                }}>
                  {category.title}
                </span>
                <span style={{
                  fontSize: '12px',
                  color: '#6b7280',
                }}>
                  {expandedCategories[categoryKey] ? '▼' : '▶'}
                </span>
              </button>

              {/* Nodes */}
              {expandedCategories[categoryKey] && (
                <div style={{
                  display: 'flex',
                  flexDirection: 'column',
                  gap: '6px',
                }}>
                  {filteredNodes.map((node) => (
                    <button
                      key={node.type}
                      onClick={() => handleAddNode(node.type, node.label)}
                      style={{
                        padding: '10px 12px',
                        background: 'white',
                        border: `2px solid ${category.color}40`,
                        borderRadius: '6px',
                        cursor: 'pointer',
                        textAlign: 'left',
                        transition: 'all 0.2s',
                      }}
                      onMouseEnter={(e) => {
                        e.currentTarget.style.background = category.color + '10';
                        e.currentTarget.style.borderColor = category.color;
                        e.currentTarget.style.transform = 'translateX(-2px)';
                      }}
                      onMouseLeave={(e) => {
                        e.currentTarget.style.background = 'white';
                        e.currentTarget.style.borderColor = category.color + '40';
                        e.currentTarget.style.transform = 'translateX(0)';
                      }}
                    >
                      <div style={{
                        display: 'flex',
                        alignItems: 'center',
                        gap: '8px',
                      }}>
                        <span style={{ fontSize: '20px' }}>{node.icon}</span>
                        <div style={{ flex: 1 }}>
                          <div style={{
                            fontWeight: '600',
                            fontSize: '13px',
                            color: '#1a192b',
                            marginBottom: '2px',
                          }}>
                            {node.label}
                          </div>
                          <div style={{
                            fontSize: '11px',
                            color: '#6b7280',
                          }}>
                            {node.description}
                          </div>
                        </div>
                      </div>
                    </button>
                  ))}
                </div>
              )}
            </div>
          );
        })}
      </div>

      {/* Footer */}
      <div style={{
        padding: '12px',
        borderTop: '1px solid #e5e7eb',
        background: '#f9fafb',
        fontSize: '12px',
        color: '#6b7280',
        textAlign: 'center',
      }}>
        {Object.values(nodeCategories).reduce((sum, cat) => sum + cat.nodes.length, 0)} nodes available
      </div>
    </div>
  );
}

export default NodePalette;
