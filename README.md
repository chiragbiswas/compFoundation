Custom Language Compiler with JIT <p>
A high-performance programming language implementation featuring an interpreter and LLVM-based JIT compiler. This project demonstrates advanced compiler engineering techniques including lexical analysis, recursive descent parsing, AST generation, and just-in-time compilation to native code.
<br><br>
Overview
<p>
This compiler implements a dynamic programming language with modern features like first-class functions, dynamic typing, and rich data structures. The language supports numbers, strings, arrays, and dictionaries as core data types, with a clean syntax inspired by JavaScript and Python. Control flow includes if/else statements, while loops, and C-style for loops. Functions are first-class citizens with full recursion support, and the language includes built-in mathematical and statistical functions like sqrt, pow, mean, and std.
</p>
<br>
<p>
The compiler architecture consists of several well-defined components. A hand-written lexer tokenizes source code while handling comments and string escape sequences. The recursive descent parser generates a type-safe Abstract Syntax Tree with clean separation between expressions and statements. For execution, users can choose between a tree-walking interpreter for quick development or LLVM-based JIT compilation for production performance. The JIT compiler generates optimized native code at runtime, providing 10-100x performance improvements for compute-intensive tasks.
</p>
<br><br>
Getting Started
<p>
To build the compiler, you'll need LLVM 14 or later and a C++14 compatible compiler. On macOS with Apple Silicon, install LLVM using Homebrew with <code>brew install llvm</code> and add it to your PATH. The project includes a Makefile for easy building - simply run <code>make</code> to build with JIT support or <code>make interpreter</code> for a standalone interpreter without LLVM dependencies. Once built, you can run the compiler with <code>make run</code> or execute the binary directly.
</p>
<br><br>
Language Features
<p>
The language syntax will feel familiar to JavaScript and Python developers while offering some unique features. Variables are declared with <code>let</code> and support dynamic typing. Functions are declared with the <code>function</code> keyword and support multiple parameters and return values. The type system includes numbers (64-bit floats), strings with escape sequences, arrays with dynamic sizing, and maps with string keys.
