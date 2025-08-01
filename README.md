Custom Language Compiler with JIT
A high-performance programming language implementation featuring an interpreter and LLVM-based JIT compiler. This project demonstrates advanced compiler engineering techniques including lexical analysis, recursive descent parsing, AST generation, and just-in-time compilation to native code.
Features
Language Features

Variables & Types: Numbers, strings, arrays, and dictionaries
Control Flow: if/else statements, while loops, and C-style for loops
Functions: First-class functions with recursion support
Operators: Arithmetic (+, -, *, /, **), comparison (==, !=, <, >, <=, >=)
Built-in Functions: Mathematical (sqrt, pow, log, exp, abs) and statistical (mean, std, max, min, sum)
String Operations: Concatenation, length, and type conversion
Data Structures: Dynamic arrays and hash maps with intuitive syntax

Compiler Features

Lexical Analysis: Hand-written lexer with support for comments
Recursive Descent Parser: Generates a clean Abstract Syntax Tree (AST)
Interpreter Mode: Tree-walking interpreter for quick execution
JIT Compilation: LLVM-based just-in-time compilation to native code
Optimizations: Leverages LLVM's optimization passes for performance

Quick Start
Prerequisites
For JIT compilation:

LLVM 14+ (on macOS: brew install llvm)
C++14 compatible compiler
Make

Architecture
Components

Lexer (Lexer class)

Tokenizes source code into meaningful symbols
Handles comments, strings with escape sequences, and numeric literals
Position tracking for error reporting


Parser (Parser class)

Recursive descent parser with operator precedence
Generates a type-safe Abstract Syntax Tree
Comprehensive error messages


AST Nodes

Expression nodes: literals, identifiers, binary operations, function calls
Statement nodes: declarations, assignments, control flow, functions
Clean visitor pattern for traversal


Interpreter (Interpreter class)

Tree-walking interpreter with scope management
Support for nested scopes and closures
Built-in function library


JIT Compiler (JITCompiler class)

Generates LLVM IR from AST
Applies optimization passes
Compiles to native machine code at runtime



JIT Compilation Pipeline
Source Code → Lexer → Parser → AST → LLVM IR → Optimization → Native Code
Performance
The JIT compiler provides significant performance improvements over interpretation:

Arithmetic operations: 10-50x faster
Function calls: 5-20x faster
Loop iterations: 20-100x faster

Example: Computing fibonacci(40)

Interpreter: ~5 seconds
JIT Compiled: ~0.1 seconds

Technical Highlights
LLVM Integration

Generates optimized LLVM IR
Leverages LLVM's powerful optimization infrastructure
Cross-platform native code generation

Memory Management

Automatic memory management for built-in types
Efficient string interning
Reference counting for complex objects

Error Handling

Descriptive parse errors with line/column information
Runtime type checking
Stack traces for debugging
