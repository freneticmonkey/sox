# Visual Node Editor → Sox Bytecode Compilation

**Author:** AI Agent
**Date:** 2025-11-29
**Status:** Planning (Revised after technical review)
**Branch:** `claude/node-editor-sox-bytecode-016zYvhVuMJfhm4aFgmX74Jx`
**Revision:** 2 - Addressed critical technical issues identified in review

---

## Vision & Objectives

Transform the React Flow node editor into a **visual programming environment** for Sox that:
- Provides an intuitive, graph-based programming interface
- Generates valid Sox bytecode directly from node graphs
- Offers real-time validation and feedback
- Enables both educational exploration and practical development
- Serves as a visual debugger/inspector for Sox programs

---

## I. ARCHITECTURAL APPROACHES

### Option A: Direct Bytecode Generation
**Flow:** Nodes → Graph Analysis → Bytecode Emission → VM Execution

**Pros:**
- Direct path to execution
- No intermediate representations
- Leverages existing bytecode infrastructure
- Maximum control over optimization

**Cons:**
- Tight coupling to VM implementation
- Complex graph-to-bytecode translation logic
- Harder to debug intermediate stages

### Option B: Sox Source Code Generation ⭐ (Recommended for MVP)
**Flow:** Nodes → Graph Analysis → Sox Source → Existing Compiler → Bytecode

**Pros:**
- Reuses existing, tested compiler
- Easier to debug (inspect generated source)
- Natural validation via compiler errors
- Future-proof against compiler changes

**Cons:**
- Extra compilation step
- Limited to language expressiveness
- May lose visual debugging fidelity

### Option C: Hybrid AST-Based Approach (Best Long-term)
**Flow:** Nodes → AST Construction → Bytecode Compiler → VM Execution
        └→ Optional: AST → Source Code (for debugging)

**Pros:**
- Clean separation of concerns
- AST enables optimizations
- Can generate both bytecode and source
- Industry-standard approach

**Cons:**
- Most implementation work
- Requires AST data structures
- Higher initial complexity

---

## II. NODE TYPE TAXONOMY

### 1. Value Nodes (Literals & Constants)

| Node Type | Properties | Bytecode Mapping |
|-----------|------------|------------------|
| `NumberNode` | `value: number` | `OP_CONSTANT <index>` |
| `StringNode` | `value: string` | `OP_CONSTANT <index>` |
| `BooleanNode` | `value: boolean` | `OP_TRUE` / `OP_FALSE` |
| `NilNode` | - | `OP_NIL` |
| `ArrayNode` | `elements: []` | `OP_ARRAY_EMPTY` + elements + count + `OP_ARRAY_PUSH` |

**Example:**
```javascript
NumberNode(42)    → OP_CONSTANT <index>
BooleanNode(true) → OP_TRUE
NilNode()         → OP_NIL

// Array initialization: [1, 2, 3]
ArrayNode([1,2,3]) → OP_ARRAY_EMPTY
                     OP_CONSTANT 1       // element 1
                     OP_CONSTANT 2       // element 2
                     OP_CONSTANT 3       // element 3
                     OP_CONSTANT 3       // count = 3
                     OP_ARRAY_PUSH       // pop count, pop elements, push to array
```

**⚠️ Note:** Array creation requires elements to be pushed to stack, then count, then `OP_ARRAY_PUSH` pops count and that many elements from stack.

**⚠️ Table Support:** Sox currently has no bytecode opcodes for table creation (only `OBJ_TABLE` objects accessed via `OP_GET_INDEX`/`OP_SET_INDEX`). Tables are created through native functions or internal mechanisms. **TableNode is deferred to post-MVP** pending VM changes or native function call node implementation.

### 2. Variable Nodes

| Node Type | Properties | Bytecode Mapping |
|-----------|------------|------------------|
| `DeclareVar` | `name: string, scope: 'local'\|'global'` | Global: `OP_DEFINE_GLOBAL` / Local: *(no opcode - stack allocation)* |
| `GetVar` | `name: string` | `OP_GET_LOCAL` / `OP_GET_GLOBAL` |
| `SetVar` | `name: string, value: port` | `OP_SET_LOCAL` / `OP_SET_GLOBAL` |
| `GetUpvalue` | `name: string, depth: number` | `OP_GET_UPVALUE` |
| `SetUpvalue` | `name: string, depth: number, value: port` | `OP_SET_UPVALUE` |
| `GetProperty` | `object: port, property: string` | `OP_GET_PROPERTY` |
| `GetIndex` | `collection: port, index: port` | `OP_GET_INDEX` |
| `SetIndex` | `collection: port, index: port, value: port` | `OP_SET_INDEX` |

**Example:**
```javascript
// Global variable
DeclareVar('x', 'global') → OP_DEFINE_GLOBAL <index>

// Local variable - NO OPCODE EMITTED
// Locals are tracked in symbol table and allocated on stack
DeclareVar('y', 'local')  → (symbol table entry only, stack slot reserved)

GetVar('x')              → OP_GET_LOCAL <index> / OP_GET_GLOBAL <index>
SetVar('x')              → OP_SET_LOCAL <index> / OP_SET_GLOBAL <index>
```

**⚠️ Important:** Local variables in Sox do not have a "define" opcode. They are automatically allocated on the stack when scope depth > 0. Only `OP_DEFINE_GLOBAL` exists for global variables. The compiler tracks locals in the symbol table without emitting bytecode.

### 3. Operator Nodes (Binary & Unary)

| Category | Node Type | Properties | Bytecode |
|----------|-----------|------------|----------|
| **Arithmetic** | `Add` | `left: port, right: port` | `OP_ADD` |
| | `Subtract` | `left: port, right: port` | `OP_SUBTRACT` |
| | `Multiply` | `left: port, right: port` | `OP_MULTIPLY` |
| | `Divide` | `left: port, right: port` | `OP_DIVIDE` |
| | `Negate` | `operand: port` | `OP_NEGATE` |
| **Comparison** | `Equal` | `left: port, right: port` | `OP_EQUAL` |
| | `Greater` | `left: port, right: port` | `OP_GREATER` |
| | `Less` | `left: port, right: port` | `OP_LESS` |
| **Logical** | `Not` | `operand: port` | `OP_NOT` |
| | `And` | `left: port, right: port` | Short-circuit with jumps |
| | `Or` | `left: port, right: port` | Short-circuit with jumps |

**Stack-based evaluation:**
```javascript
Add(left, right) → [left bytecode] [right bytecode] OP_ADD
Negate(val)      → [val bytecode] OP_NEGATE
```

### 4. Control Flow Nodes

| Node Type | Properties | Bytecode Pattern |
|-----------|------------|------------------|
| `IfElse` | `condition: port, thenBranch: port, elseBranch: port` | `OP_JUMP_IF_FALSE` + `OP_JUMP` |
| `While` | `condition: port, body: port` | `OP_JUMP_IF_FALSE` + `OP_LOOP` |
| `For` | `initializer: port, condition: port, increment: port, body: port` | Loop with increment |
| `Switch` | `value: port, cases: [{pattern, body}], default: port` | Multiple comparisons + jumps |
| `Break` | - | `OP_BREAK` |
| `Continue` | - | `OP_CONTINUE` |

**Example bytecode pattern:**
```
IfElse:
  [condition code]
  OP_JUMP_IF_FALSE <else_offset>
  [then code]
  OP_JUMP <end_offset>
  [else code]

While:
  [condition code]
  OP_JUMP_IF_FALSE <exit_offset>
  [body code]
  OP_LOOP <start_offset>
```

### 5. Function Nodes

| Node Type | Properties | Bytecode Mapping |
|-----------|------------|------------------|
| `DefineFunction` | `name: string, parameters: string[], body: port` | `OP_CLOSURE <function_index>` |
| `CallFunction` | `function: port, arguments: port[]` | `[args...] OP_CALL <arg_count>` |
| `Return` | `value: port` | `[value] OP_RETURN` |
| `Closure` | `function: string, capturedVars: string[]` | `OP_CLOSURE` with upvalues |
| `Lambda` | `parameters: string[], expression: port` | Anonymous function |

**Example:**
```javascript
DefineFunction → OP_CLOSURE <function_index>
CallFunction   → [args...] OP_CALL <arg_count>
Return         → [value] OP_RETURN
```

### 6. Object-Oriented Nodes

| Node Type | Properties | Bytecode Mapping |
|-----------|------------|------------------|
| `DefineClass` | `name: string, methods: [{name, body}], superclass: string?` | `OP_CLASS` + `OP_METHOD` |
| `CreateInstance` | `className: string` | Constructor call |
| `GetProperty` | `object: port, property: string` | `OP_GET_PROPERTY` |
| `SetProperty` | `object: port, property: string, value: port` | `OP_SET_PROPERTY` |
| `InvokeMethod` | `object: port, method: string, arguments: port[]` | `OP_INVOKE` |

**Example:**
```javascript
DefineClass → OP_CLASS <name_index>
              [methods...] OP_METHOD <name_index>
GetProperty → [object] OP_GET_PROPERTY <name_index>
InvokeMethod → [object] [args...] OP_INVOKE <name_index> <arg_count>
```

### 7. I/O & Side Effect Nodes

| Node Type | Properties | Bytecode Mapping |
|-----------|------------|------------------|
| `Print` | `value: port` | `[value] OP_PRINT` |
| `Pop` | `expression: port` | `[expression] OP_POP` |
| `Input` | `prompt: string` | Future: native function call |
| `FileRead` | `path: port` | Future: native function call |
| `FileWrite` | `path: port, content: port` | Future: native function call |

**⚠️ Critical:** `PopNode` is essential for expression statements. When an expression is evaluated but its result is not used (e.g., a function call for side effects), `OP_POP` removes the value from the stack.

**Example:**
```javascript
// Expression statement: print(42)
// This is both Print (side effect) and expression (leaves nil on stack)
// Need OP_POP to clean up
print(42)  → OP_CONSTANT 42
             OP_PRINT           // Prints and leaves nil on stack
             OP_POP             // Clean up nil
```

### 8. Utility Nodes

| Node Type | Properties | Purpose |
|-----------|------------|---------|
| `Comment` | `text: string` | Documentation (no bytecode) |
| `Sequence` | `steps: port[]` | Execute in order |
| `EntryPoint` | - | Marks program start |
| `Breakpoint` | - | Debug assistance |
| `DeferNode` | `function: port` | Deferred execution at scope end |
| `CloseUpvalue` | *(auto-generated)* | Automatically inserted by compiler when scope ends with captured variables |

**Defer Handling:**
```javascript
DeferNode(func) → Registers function for end-of-scope execution
                  Compiler tracks deferred functions and emits calls in reverse order
```

**⚠️ Note:** `CloseUpvalue` nodes are automatically generated by the compiler and typically invisible to users. They ensure captured variables are properly moved from stack to heap when scopes end.

---

## III. GRAPH-TO-BYTECODE COMPILATION PIPELINE

### Phase 1: Graph Validation

**Input:** `Nodes[]` + `Edges[]`

**Validation Steps:**
1. **Topology Validation**
   - ✓ Check for cycles (except in control flow)
   - ✓ Verify all required ports connected
   - ✓ Ensure single entry point exists
   - ✓ Validate port type compatibility

2. **Semantic Validation**
   - ✓ Variable scope analysis
   - ✓ Type checking (if enforced)
   - ✓ Function signature matching
   - ✓ Control flow reachability

3. **Error Reporting**
   - Visual feedback in UI (highlight problem nodes)
   - Detailed error messages with suggestions

**Output:** Validation report with errors/warnings

### Phase 2: Graph Analysis & Ordering

**Algorithm:** Topological Sort with Control Flow Awareness

**Steps:**
1. **Build Dependency Graph**
   - Data dependencies (value flow)
   - Control dependencies (execution order)
   - Side effect ordering (print, assignments)

2. **Identify Execution Blocks**
   - Sequential blocks (straight-line code)
   - Branching blocks (if/else, switch)
   - Loop blocks (while, for)
   - Function blocks (closures)

3. **Generate Execution Order**
   - Depth-first traversal from entry point
   - Respect data dependencies
   - Maintain control flow semantics

**Output:** Ordered node list with block structure

### Phase 3: Symbol Table Construction

**Steps:**
1. **Collect Variable Declarations**
   - Global variables
   - Local variables (per scope)
   - Function parameters
   - Upvalues (closures)

2. **Assign Indices**
   - Locals: stack slot indices
   - Globals: constant pool indices
   - Upvalues: upvalue array indices

3. **Scope Tracking**
   - Maintain scope depth
   - Track variable captures
   - Detect shadowing

**Output:** `SymbolTable` mapping `names → {index, scope, isCaptured}`

### Phase 4: Bytecode Generation

**For each node in execution order:**

1. **Expression Evaluation (Stack-based)**
   - Emit child expressions first (postorder)
   - Emit operation opcode
   - Track stack depth for validation

2. **Statement Execution**
   - Variable declarations
   - Assignments
   - Side effects (print)

3. **Control Flow**
   - Emit jump instructions with placeholders
   - Patch jump offsets after block generation
   - Maintain jump stack for break/continue

4. **Function Compilation**
   - Create new chunk for function body
   - Compile function graph recursively
   - Emit `OP_CLOSURE` with function constant

**Output:** `chunk_t` with bytecode + constant pool

### Phase 4b: Error Source Mapping (Critical for Option B)

**Problem:** When using Option B (Source Code Generation), the Sox compiler will report errors referencing **generated source code line numbers**, but users only see the **visual node graph**.

**Solution:** Implement a **Source Map** system similar to JavaScript transpilers:

**Data Structure:**
```go
type SourceMap struct {
    GeneratedSource string
    Mappings        []SourceMapping
}

type SourceMapping struct {
    SourceLine   int     // Line in generated .sox code
    SourceColumn int     // Column in generated .sox code
    NodeID       string  // Original node ID
    NodePort     string  // Specific port (optional)
}
```

**Implementation:**
```go
func GenerateSourceWithMap(graph Graph) (string, *SourceMap, error) {
    var source strings.Builder
    sourceMap := &SourceMap{}
    currentLine := 1

    for _, node := range graph.OrderedNodes {
        // Track mapping before emitting code
        sourceMap.Mappings = append(sourceMap.Mappings, SourceMapping{
            SourceLine: currentLine,
            NodeID:     node.ID,
        })

        // Emit source code for node
        code := node.GenerateSource()
        source.WriteString(code)
        source.WriteString("\n")
        currentLine++
    }

    sourceMap.GeneratedSource = source.String()
    return source.String(), sourceMap, nil
}
```

**Error Mapping:**
```go
func MapCompilerError(err CompilerError, sourceMap *SourceMap) VisualError {
    // Find mapping for error line
    mapping := sourceMap.FindMapping(err.Line, err.Column)

    return VisualError{
        NodeID:      mapping.NodeID,
        Message:     err.Message,
        Suggestion:  err.Suggestion,
        ErrorType:   err.Type,
    }
}
```

**UI Integration:**
```javascript
// In React frontend
function handleCompilationError(error) {
    // Error comes with nodeID from backend
    const node = reactFlowInstance.getNode(error.nodeID)

    // Highlight problematic node
    reactFlowInstance.setNodes((nodes) =>
        nodes.map((n) => ({
            ...n,
            style: n.id === error.nodeID
                ? { border: '3px solid red' }
                : n.style
        }))
    )

    // Show error message near node
    showErrorTooltip(node, error.message)
}
```

**⚠️ Critical:** Without error mapping, users will see cryptic errors like "Line 42: Undefined variable 'x'" with no way to find which node caused it.

### Phase 5: Optimization (Optional)

**Optimization Passes:**
1. **Constant Folding**
   - Evaluate constant expressions at compile time
   - Replace `NumberNode(2) + NumberNode(3)` with `NumberNode(5)`

2. **Dead Code Elimination**
   - Remove unreachable blocks
   - Eliminate unused variables

3. **Peephole Optimization**
   - `OP_CONSTANT x + OP_POP` → eliminate
   - `OP_CONSTANT x + OP_CONSTANT y` → merge constants

4. **Jump Threading**
   - Optimize jump chains
   - Eliminate redundant jumps

---

## IV. IMPLEMENTATION STRATEGY

### Phase 1: Foundation (Weeks 1-3) ⏱️ *2-3 weeks*

**Backend Infrastructure (Go)**
- [ ] Create `graph_compiler.go`
- [ ] Implement `GraphValidator`
- [ ] Implement `TopologicalSorter`
- [ ] Add bytecode generation helpers
- [ ] Test with simple graphs

**Node Type Definitions**
- [ ] Define JSON schema for all node types
- [ ] Create validation rules
- [ ] Document node specifications

**Integration Layer**
- [ ] Add `CompileGraph(nodes, edges) → bytecode`
- [ ] Add `ValidateGraph(nodes, edges) → errors`
- [ ] Expose via Wails bindings

### Phase 2: Core Nodes (Weeks 4-5) ⏱️ *2 weeks*

**Essential Nodes**
- [ ] Value nodes (number, string, boolean, nil)
- [ ] Variable nodes (declare, get, set)
- [ ] Operator nodes (arithmetic, comparison)
- [ ] Print node (for testing)
- [ ] Entry point node

**Frontend Node Components (React)**
- [ ] Create custom node components
- [ ] Add port visualizations
- [ ] Implement node property editors
- [ ] Add visual feedback for errors

**Testing**
- [ ] Create "Hello World" graph
- [ ] Create arithmetic expression graphs
- [ ] Verify bytecode generation
- [ ] Test VM execution

### Phase 3: Control Flow (Weeks 6-9) ⏱️ *3-4 weeks* ⚠️ **HIGH COMPLEXITY**

**Control Nodes**
- [ ] If/Else nodes
- [ ] While loops
- [ ] For loops
- [ ] Break/Continue

**Jump Patching System**
- [ ] Implement jump placeholder tracking
- [ ] Add backpatching for forward jumps
- [ ] Test nested control structures

**Visual Flow Representation**
- [ ] Different edge styles for data vs control flow
- [ ] Highlight active execution path
- [ ] Show loop boundaries

### Phase 4: Functions & Closures (Weeks 10-13) ⏱️ *3-4 weeks* ⚠️ **HIGH COMPLEXITY**

**Function Support**
- [ ] DefineFunction nodes
- [ ] CallFunction nodes
- [ ] Return nodes
- [ ] Parameter/argument matching

**Closure Implementation**
- [ ] Upvalue capture analysis
- [ ] Closure bytecode generation
- [ ] Test nested closures

**Subgraph System**
- [ ] Functions as embedded subgraphs
- [ ] Graph navigation UI
- [ ] Call graph visualization

### Phase 5: OOP Features (Weeks 14-16) ⏱️ *2-3 weeks*

**Class System Nodes**
- [ ] DefineClass
- [ ] CreateInstance
- [ ] GetProperty/SetProperty
- [ ] InvokeMethod

**Inheritance Support**
- [ ] Superclass references
- [ ] Super method calls
- [ ] Initialization

**Testing**
- [ ] Port existing OOP test cases to graphs

### Phase 6: Polish & UX (Weeks 17-19) ⏱️ *2-3 weeks*

**Developer Experience**
- [ ] Node palette/library
- [ ] Search and quick-add
- [ ] Templates and snippets
- [ ] Keyboard shortcuts

**Debugging Tools**
- [ ] Bytecode inspector panel
- [ ] Execution trace visualization
- [ ] Variable watch panel
- [ ] Breakpoint support

**Graph Management**
- [ ] Save/load graphs to files
- [ ] Export to .sox source
- [ ] Import from .sox source (reverse engineering)
- [ ] Version control friendly format

### Implementation Timeline Summary

**Revised Realistic Estimate:** 14-19 weeks (3.5-4.75 months)

| Phase | Duration | Cumulative | Risk Level | Notes |
|-------|----------|------------|------------|-------|
| Phase 1: Foundation | 2-3 weeks | Weeks 1-3 | Low | Add extra time for robust validation |
| Phase 2: Core Nodes | 2 weeks | Weeks 4-5 | Low | Original estimate adequate |
| Phase 3: Control Flow | 3-4 weeks | Weeks 6-9 | **HIGH** | Jump patching complexity, nested structures |
| Phase 4: Functions/Closures | 3-4 weeks | Weeks 10-13 | **HIGH** | Upvalue analysis, defer integration |
| Phase 5: OOP Features | 2-3 weeks | Weeks 14-16 | Medium | Inheritance and method binding |
| Phase 6: Polish & UX | 2-3 weeks | Weeks 17-19 | Low | Refinement and debugging tools |

**Conservative Estimate:** 16 weeks (4 months)
**Aggressive Estimate:** 14 weeks (3.5 months)
**Original Estimate:** 12 weeks *(too optimistic)*

**⚠️ High Risk Areas:**
- **Jump patching** in nested control flow (Phase 3)
- **Upvalue capture** for closures (Phase 4)
- **Switch statement** fall-through behavior (Phase 3)
- **Error source mapping** implementation (Phase 1)

---

## V. TECHNICAL ARCHITECTURE

### Data Structures

```go
// graph_compiler.go

type NodePort struct {
    NodeID   string
    PortName string
}

type CompiledNode struct {
    Node     Node
    Inputs   map[string]NodePort
    Outputs  map[string][]NodePort
    Order    int  // Execution order
}

type CompilationContext struct {
    Nodes        map[string]*CompiledNode
    SymbolTable  *SymbolTable
    Chunk        *Chunk
    CurrentDepth int
    LoopStack    []LoopInfo
}

type SymbolTable struct {
    Scopes []Scope
    Depth  int
}

type Scope struct {
    Variables map[string]Variable
    Parent    *Scope
}

type Variable struct {
    Name       string
    Index      int
    Depth      int
    IsCaptured bool
}
```

### Core Algorithms

#### 1. Topological Sort with Cycles
```go
func TopologicalSort(nodes []Node, edges []Edge) ([]Node, error) {
    // Modified DFS that allows cycles for control flow
    // Detect data dependency cycles (error)
    // Allow control flow cycles (loops)
}
```

#### 2. Stack Depth Tracking
```go
func (ctx *CompilationContext) EmitInstruction(op OpCode) {
    ctx.Chunk.WriteOpCode(op)
    ctx.StackDepth += GetStackEffect(op)

    if ctx.StackDepth < 0 {
        panic("Stack underflow")
    }
}
```

#### 3. Jump Patching
```go
type JumpPatch struct {
    InstructionOffset int
    TargetLabel       string
}

func (ctx *CompilationContext) PatchJumps() {
    for _, patch := range ctx.PendingPatches {
        offset := ctx.Labels[patch.TargetLabel] - patch.InstructionOffset
        ctx.Chunk.PatchJump(patch.InstructionOffset, offset)
    }
}
```

---

## VI. USER INTERFACE ENHANCEMENTS

### Node Appearance Design

```javascript
// Custom node styling by type
const nodeStyles = {
  value: {
    background: '#4CAF50',  // Green
    border: '2px solid #388E3C'
  },
  operator: {
    background: '#2196F3',  // Blue
    border: '2px solid #1976D2'
  },
  control: {
    background: '#FF9800',  // Orange
    border: '2px solid #F57C00'
  },
  function: {
    background: '#9C27B0',  // Purple
    border: '2px solid #7B1FA2'
  },
  class: {
    background: '#E91E63',  // Pink
    border: '2px solid #C2185B'
  },
  io: {
    background: '#607D8B',  // Grey
    border: '2px solid #455A64'
  }
}
```

### Port Visualization

```javascript
// Type-specific port colors
const portColors = {
  number:  '#4CAF50',
  string:  '#2196F3',
  boolean: '#FF9800',
  object:  '#9C27B0',
  array:   '#00BCD4',
  any:     '#9E9E9E',
  control: '#F44336'  // Execution flow
}
```

### Real-time Validation UI

```javascript
// Visual feedback
const validationStates = {
  valid:   { border: '2px solid #4CAF50', icon: '✓' },
  warning: { border: '2px solid #FF9800', icon: '⚠' },
  error:   { border: '2px solid #F44336', icon: '✗' },
  unknown: { border: '2px dashed #9E9E9E', icon: '?' }
}
```

### Execution Visualization

```javascript
// Animate execution flow
function visualizeExecution(trace) {
  trace.forEach((step, index) => {
    setTimeout(() => {
      highlightNode(step.nodeId)
      showStack(step.stack)
      showVariables(step.locals)
    }, index * 500)  // 500ms per step
  })
}
```

---

## VII. EXAMPLE: "Hello World" Graph

### Graph Definition

```javascript
{
  nodes: [
    {
      id: "entry",
      type: "EntryPoint",
      position: {x: 100, y: 100},
      data: {label: "Program Start"}
    },
    {
      id: "str1",
      type: "StringNode",
      position: {x: 100, y: 200},
      data: {
        label: "String",
        value: "Hello, World!"
      }
    },
    {
      id: "print1",
      type: "Print",
      position: {x: 100, y: 300},
      data: {label: "Print"}
    }
  ],
  edges: [
    {
      id: "e1",
      source: "entry",
      target: "print1",
      sourceHandle: "control-out",
      targetHandle: "control-in",
      type: "control"  // Execution flow
    },
    {
      id: "e2",
      source: "str1",
      target: "print1",
      sourceHandle: "value-out",
      targetHandle: "value-in",
      type: "data"  // Data flow
    }
  ]
}
```

### Generated Bytecode

```
Chunk {
  constants: ["Hello, World!"],
  code: [
    OP_CONSTANT, 0,    // Push "Hello, World!"
    OP_PRINT,          // Print it
    OP_NIL,            // Return nil
    OP_RETURN
  ]
}
```

---

## VIII. ADVANCED FEATURES

### 1. Visual Debugging
- Step through bytecode execution
- Highlight active node
- Show stack state at each step
- Display variable values
- Set visual breakpoints

### 2. Graph Import/Export

```javascript
// Export graph to Sox source
exportToSource(graph) → "var x = 42\nprint(x)"

// Import Sox source to graph (AST parsing)
importFromSource("print('hello')") → graph

// Save graph as JSON
saveGraph(graph) → "program.soxgraph"
```

### 3. Live Execution Mode

```
┌─────────────┬──────────────┐
│   Editor    │   Output     │
│   [Graph]   │   [Console]  │
│             │   [Variables]│
│             │   [Stack]    │
└─────────────┴──────────────┘

Changes in graph → Auto-recompile → Live results
```

### 4. Template Library

```javascript
templates = {
  "Fibonacci": fib_graph,
  "FizzBuzz": fizzbuzz_graph,
  "Quicksort": quicksort_graph,
  "Class Example": class_graph,
  ...
}
```

### 5. Collaborative Editing (Future)
- Multiple users editing same graph
- Operational transforms for conflict resolution
- Real-time cursor positions
- Change history and versioning

---

## IX. CHALLENGES & SOLUTIONS

### Challenge 1: Expression Precedence
**Problem:** Nodes don't inherently have precedence
**Solution:** Force explicit ordering via connections; no implicit precedence

### Challenge 2: Scope Visualization
**Problem:** Visual scope boundaries unclear
**Solution:** Color-coded regions, collapsible scope blocks, minimap highlights

### Challenge 3: Large Graphs
**Problem:** Performance with 100+ nodes
**Solutions:**
- Virtual rendering (only render visible nodes)
- Graph clustering/hierarchical views
- Performance optimizations in React Flow

### Challenge 4: Error Messages
**Problem:** Bytecode errors hard to map to visual nodes
**Solution:** Source map equivalent (bytecode offset → node ID mapping)

### Challenge 5: Circular Dependencies
**Problem:** Cycles in data flow (excluding loops)
**Solution:** Cycle detection with clear error highlighting

---

## X. SUCCESS METRICS

### Technical Metrics
- ✓ All 45 Sox opcodes representable as nodes or handled automatically
  - 42 opcodes require explicit nodes
  - 3 opcodes (OP_BREAK, OP_CONTINUE, OP_CASE_FALLTHROUGH) are compiler placeholders that get patched to OP_JUMP
- ✓ 100% bytecode compatibility with text compiler
- ✓ Round-trip: Graph → Bytecode → Graph (lossless)
- ✓ Graph compilation < 100ms for typical programs

### Usability Metrics
- ✓ Beginners can create programs without syntax knowledge
- ✓ < 5 clicks to create "Hello World"
- ✓ Real-time validation (< 500ms feedback)
- ✓ Visual debugging available for all programs

### Educational Value
- ✓ Visualize bytecode generation process
- ✓ Understand stack-based execution
- ✓ Learn compiler concepts interactively
- ✓ Bridge visual and textual programming

---

## XI. RECOMMENDED IMPLEMENTATION PATH

### Immediate Next Steps

**1. Choose Architecture: Option B (Sox Source Generation)**
- **Rationale:** Fastest path to working prototype
- Leverages existing compiler (zero bytecode bugs)
- Easy to validate (read generated source)
- Can migrate to Option C later

**2. Define Core Node Schema**
```json
{
  "nodeTypes": {
    "NumberNode": {...},
    "StringNode": {...},
    "PrintNode": {...},
    "EntryPoint": {...}
  }
}
```

**3. Implement Graph→Source Compiler (Go)**
```go
func CompileGraphToSource(nodes, edges) (string, error) {
    // Returns Sox source code string
}
```

**4. Create First 4 Node Components (React)**
- EntryPoint (start execution)
- NumberNode (literal value)
- StringNode (literal value)
- PrintNode (output)

**5. Test End-to-End Pipeline**
```
Graph → CompileGraphToSource() → SoxSource → l_compile() → Bytecode → VM
```

### Validation Test

```javascript
// Create graph for: print("Hello from nodes!")
graph = {
  nodes: [EntryPoint, StringNode("Hello from nodes!"), PrintNode],
  edges: [entry→print, string→print]
}

CompileAndRun(graph)
// Expected Output: Hello from nodes!
// ✓ Success!
```

---

## XII. CONCLUSION

This plan provides a comprehensive roadmap from visual node editor to Sox bytecode generation. The phased approach ensures incremental progress with testable milestones.

Starting with **Sox source generation** (Option B) provides the fastest path to a working prototype while maintaining the flexibility to migrate to a more sophisticated **AST-based system** (Option C) as the project matures.

The node-based visual programming interface will:
- **Democratize** Sox programming (no syntax barrier)
- **Visualize** compilation and execution processes
- **Educate** users on bytecode VM concepts
- **Accelerate** development with visual debugging

This is not just a bytecode generator—it's a **platform for exploring, learning, and innovating** in programming language design.

---

## Appendix A: Complete Sox Opcode Reference

All 45 Sox opcodes from `/home/user/sox/src/chunk.h` (lines 7-54):

### Constants & Literals (5)
- `OP_CONSTANT` - Load constant from constant pool
- `OP_NIL` - Push nil value
- `OP_TRUE` - Push true boolean
- `OP_FALSE` - Push false boolean

### Stack Operations (1)
- `OP_POP` - Pop and discard top stack value

### Local Variables (2)
- `OP_GET_LOCAL` - Get local variable by stack index
- `OP_SET_LOCAL` - Set local variable by stack index

### Global Variables (3)
- `OP_GET_GLOBAL` - Get global variable by name
- `OP_DEFINE_GLOBAL` - Define new global variable
- `OP_SET_GLOBAL` - Set existing global variable

### Upvalues (Closures) (3)
- `OP_GET_UPVALUE` - Get captured variable from closure
- `OP_SET_UPVALUE` - Set captured variable in closure
- `OP_CLOSE_UPVALUE` - Move captured variable from stack to heap

### Properties (3)
- `OP_GET_PROPERTY` - Get object property
- `OP_SET_PROPERTY` - Set object property
- `OP_GET_SUPER` - Get superclass method

### Indexing (2)
- `OP_GET_INDEX` - Get element from array/table by index
- `OP_SET_INDEX` - Set element in array/table by index

### Comparison Operators (3)
- `OP_EQUAL` - Test equality
- `OP_GREATER` - Test greater than
- `OP_LESS` - Test less than

### Arithmetic Operators (5)
- `OP_ADD` - Addition
- `OP_SUBTRACT` - Subtraction
- `OP_MULTIPLY` - Multiplication
- `OP_DIVIDE` - Division
- `OP_NEGATE` - Unary negation

### Logical Operators (1)
- `OP_NOT` - Logical not

### I/O (1)
- `OP_PRINT` - Print value to stdout

### Control Flow (3)
- `OP_JUMP` - Unconditional jump
- `OP_JUMP_IF_FALSE` - Conditional jump (if top of stack is falsey)
- `OP_LOOP` - Backward jump for loops

### Functions (2)
- `OP_CALL` - Call function with N arguments
- `OP_RETURN` - Return from function

### Closures (1)
- `OP_CLOSURE` - Create closure from function

### Methods (2)
- `OP_INVOKE` - Invoke method on object
- `OP_SUPER_INVOKE` - Invoke superclass method

### Classes (3)
- `OP_CLASS` - Define class
- `OP_INHERIT` - Set superclass
- `OP_METHOD` - Define method on class

### Arrays (3)
- `OP_ARRAY_EMPTY` - Create empty array
- `OP_ARRAY_PUSH` - Push N elements to array
- `OP_ARRAY_RANGE` - Create array from range

### Compiler Placeholders (3)
*These are emitted during compilation but patched to OP_JUMP before execution:*
- `OP_BREAK` - Placeholder for break statement (patched to OP_JUMP)
- `OP_CONTINUE` - Placeholder for continue statement (patched to OP_LOOP)
- `OP_CASE_FALLTHROUGH` - Placeholder for switch fall-through (patched to OP_JUMP)

**Total: 45 opcodes**

---

## Appendix B: Revision History

### Revision 2 (2025-11-29)
**Status:** Addressed critical technical review findings

**Critical Fixes:**
1. ✅ Corrected opcode count from 54 to 45
2. ✅ Removed non-existent `OP_DEFINE_LOCAL` - documented correct local variable handling
3. ✅ Added missing `OP_POP` node (essential for expression statements)
4. ✅ Fixed array initialization sequence documentation
5. ✅ Removed `TableNode` from MVP (deferred pending VM changes)
6. ✅ Added comprehensive error source mapping design

**Enhancements:**
7. ✅ Added `DeferNode` and `CloseUpvalue` handling
8. ✅ Added `SetUpvalue` and `SetIndex` nodes
9. ✅ Updated implementation timeline to 14-19 weeks (realistic estimates)
10. ✅ Added high-risk area identification and mitigation

**Timeline Changes:**
- Phase 1: 2 weeks → 2-3 weeks (+1 week for validation)
- Phase 3: 2 weeks → 3-4 weeks (+1-2 weeks for jump patching complexity)
- Phase 4: 2 weeks → 3-4 weeks (+1-2 weeks for upvalue analysis)
- Phase 5: 2 weeks → 2-3 weeks (+1 week buffer)
- **Total: 12 weeks → 14-19 weeks**

**Review Verdict:** APPROVED for implementation after revisions

---

## Appendix C: References

- **Crafting Interpreters:** http://craftinginterpreters.com
- **React Flow Documentation:** https://reactflow.dev/
- **Wails Documentation:** https://wails.io/
- **Sox README:** `/home/user/sox/README.md`
- **Sox CLAUDE.md:** `/home/user/sox/CLAUDE.md`
- **WASM Documentation:** `/home/user/sox/docs/wasm.md`
- **Sox Chunk Header:** `/home/user/sox/src/chunk.h` (opcodes)
- **Sox Compiler:** `/home/user/sox/src/compiler.c` (compilation logic)
- **Sox VM:** `/home/user/sox/src/vm.c` (execution model)
