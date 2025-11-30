import { Handle, Position } from '@xyflow/react';

const nodeColors = {
  // Literals
  EntryPoint: { bg: '#4CAF50', border: '#388E3C', text: '#fff' },
  NumberNode: { bg: '#4CAF50', border: '#388E3C', text: '#fff' },
  StringNode: { bg: '#4CAF50', border: '#388E3C', text: '#fff' },
  BooleanNode: { bg: '#4CAF50', border: '#388E3C', text: '#fff' },
  NilNode: { bg: '#4CAF50', border: '#388E3C', text: '#fff' },

  // Variables
  DeclareVar: { bg: '#2196F3', border: '#1976D2', text: '#fff' },
  GetVar: { bg: '#2196F3', border: '#1976D2', text: '#fff' },
  SetVar: { bg: '#2196F3', border: '#1976D2', text: '#fff' },

  // Arithmetic
  Add: { bg: '#FF9800', border: '#F57C00', text: '#fff' },
  Subtract: { bg: '#FF9800', border: '#F57C00', text: '#fff' },
  Multiply: { bg: '#FF9800', border: '#F57C00', text: '#fff' },
  Divide: { bg: '#FF9800', border: '#F57C00', text: '#fff' },
  Negate: { bg: '#FF9800', border: '#F57C00', text: '#fff' },

  // Comparison
  Equal: { bg: '#9C27B0', border: '#7B1FA2', text: '#fff' },
  Greater: { bg: '#9C27B0', border: '#7B1FA2', text: '#fff' },
  Less: { bg: '#9C27B0', border: '#7B1FA2', text: '#fff' },

  // Logical
  And: { bg: '#E91E63', border: '#C2185B', text: '#fff' },
  Or: { bg: '#E91E63', border: '#C2185B', text: '#fff' },
  Not: { bg: '#E91E63', border: '#C2185B', text: '#fff' },

  // I/O
  Print: { bg: '#607D8B', border: '#455A64', text: '#fff' },

  // Default
  default: { bg: '#e5e7eb', border: '#9ca3af', text: '#1f2937' },
};

function SoxNode({ data, selected }) {
  const nodeType = data.nodeType || 'default';
  const colors = nodeColors[nodeType] || nodeColors.default;

  // Determine which handles to show based on node type
  const showTopHandle = !['EntryPoint'].includes(nodeType);
  const showBottomHandle = true;
  const showLeftHandles = ['Add', 'Subtract', 'Multiply', 'Divide', 'Equal', 'Greater', 'Less', 'And', 'Or'].includes(nodeType);
  const showLeftSingleHandle = ['Negate', 'Not', 'Print', 'DeclareVar', 'SetVar'].includes(nodeType);

  return (
    <div style={{
      padding: '12px 16px',
      borderRadius: '8px',
      background: colors.bg,
      border: `3px solid ${selected ? '#fbbf24' : colors.border}`,
      color: colors.text,
      minWidth: '120px',
      boxShadow: selected
        ? '0 8px 16px rgba(0,0,0,0.2)'
        : '0 4px 8px rgba(0,0,0,0.1)',
      transition: 'all 0.2s',
    }}>
      {/* Top Handle (for inputs from above) */}
      {showTopHandle && (
        <Handle
          type="target"
          position={Position.Top}
          style={{
            background: colors.border,
            width: '10px',
            height: '10px',
            border: '2px solid white',
          }}
        />
      )}

      {/* Left Handles (for binary operators) */}
      {showLeftHandles && (
        <>
          <Handle
            type="target"
            position={Position.Left}
            id="left"
            style={{
              background: colors.border,
              width: '10px',
              height: '10px',
              border: '2px solid white',
              top: '30%',
            }}
          />
          <Handle
            type="target"
            position={Position.Left}
            id="right"
            style={{
              background: colors.border,
              width: '10px',
              height: '10px',
              border: '2px solid white',
              top: '70%',
            }}
          />
        </>
      )}

      {/* Left Single Handle (for unary operators and single inputs) */}
      {showLeftSingleHandle && (
        <Handle
          type="target"
          position={Position.Left}
          style={{
            background: colors.border,
            width: '10px',
            height: '10px',
            border: '2px solid white',
          }}
        />
      )}

      {/* Node Content */}
      <div style={{
        fontWeight: '600',
        fontSize: '13px',
        marginBottom: data.value !== undefined || data.name ? '4px' : 0,
      }}>
        {data.label}
      </div>

      {/* Show value for literal nodes */}
      {data.value !== undefined && (
        <div style={{
          fontSize: '11px',
          opacity: 0.9,
          fontFamily: 'monospace',
          background: 'rgba(0,0,0,0.1)',
          padding: '2px 6px',
          borderRadius: '3px',
        }}>
          {typeof data.value === 'string' ? `"${data.value}"` : String(data.value)}
        </div>
      )}

      {/* Show variable name */}
      {data.name && (
        <div style={{
          fontSize: '11px',
          opacity: 0.9,
          fontFamily: 'monospace',
          background: 'rgba(0,0,0,0.1)',
          padding: '2px 6px',
          borderRadius: '3px',
        }}>
          {data.name}
        </div>
      )}

      {/* Bottom Handle (for outputs) */}
      {showBottomHandle && (
        <Handle
          type="source"
          position={Position.Bottom}
          style={{
            background: colors.border,
            width: '10px',
            height: '10px',
            border: '2px solid white',
          }}
        />
      )}
    </div>
  );
}

export const nodeTypes = {
  default: SoxNode,
  input: SoxNode,
  output: SoxNode,
};

export default SoxNode;
