#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
// LLVM JIT includes
#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/Support/raw_ostream.h>
#include <map>

// Token types for our enhanced language
enum class TokenType {
    // Literals
    NUMBER,
    STRING_LITERAL,
    IDENTIFIER,
    
    // Keywords
    LET,
    PRINT,
    IF,
    ELSE,
    WHILE,
    FOR,
    FUNCTION,
    RETURN,
    
    // Operators
    PLUS,
    MINUS,
    MULTIPLY,
    DIVIDE,
    ASSIGN,
    POWER,
    
    // Comparison operators
    EQUAL,
    NOT_EQUAL,
    LESS_THAN,
    GREATER_THAN,
    LESS_EQUAL,
    GREATER_EQUAL,
    
    // Punctuation
    SEMICOLON,
    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    LBRACKET,
    RBRACKET,
    COMMA,
    DOT,
    COLON,
    
    // Special
    EOF_TOKEN,
    INVALID
};

// Enhanced Token structure
struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
    
    Token() : type(TokenType::INVALID), value(""), line(0), column(0) {}
    Token(TokenType t, const std::string& v, int l = 0, int c = 0) 
        : type(t), value(v), line(l), column(c) {}
};

// Forward declarations
class Expression;
class Statement;
class FunctionDeclaration;

struct Value {
    enum Type { NUMBER, ARRAY, STRING, MAP, FUNCTION } type;
    double number_value;
    std::vector<Value> array_value;
    std::string string_value;
    std::unordered_map<std::string, Value> map_value;
    FunctionDeclaration* function_value; // Pointer to function declaration
    
    Value() : type(NUMBER), number_value(0), function_value(nullptr) {}
    explicit Value(int n) : type(NUMBER), number_value(static_cast<double>(n)), function_value(nullptr) {}
    Value(double n) : type(NUMBER), number_value(n), function_value(nullptr) {}
    Value(const std::vector<Value>& arr) : type(ARRAY), array_value(arr), function_value(nullptr) {}
    Value(const std::string& str) : type(STRING), string_value(str), function_value(nullptr) {}
    Value(const std::unordered_map<std::string, Value>& map) : type(MAP), map_value(map), function_value(nullptr) {}
    Value(FunctionDeclaration* func) : type(FUNCTION), function_value(func) {}
    
    bool is_truthy() const {
        switch (type) {
            case NUMBER: return number_value != 0;
            case ARRAY: return !array_value.empty();
            case STRING: return !string_value.empty();
            case MAP: return !map_value.empty();
            case FUNCTION: return function_value != nullptr;
        }
        return false;
    }
    
    std::string to_string() const {
        switch (type) {
            case NUMBER: return std::to_string(number_value);
            case STRING: return string_value;
            case ARRAY: {
                std::string result = "[";
                for (size_t i = 0; i < array_value.size(); i++) {
                    result += array_value[i].to_string();
                    if (i < array_value.size() - 1) result += ", ";
                }
                result += "]";
                return result;
            }
            case MAP: {
                std::string result = "{";
                bool first = true;
                for (const auto& pair : map_value) {
                    if (!first) result += ", ";
                    result += "\"" + pair.first + "\": " + pair.second.to_string();
                    first = false;
                }
                result += "}";
                return result;
            }
            case FUNCTION: return "<function>";
        }
        return "<unknown>";
    }
};

// AST Node base class
class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual void print(int indent = 0) const = 0;
};

// Expression nodes
class Expression : public ASTNode {
public:
    virtual ~Expression() = default;
};

// --- JIT Symbol Table Type ---
typedef std::map<std::string, llvm::Value*> JITSymbolTable;

// --- JIT codegen for VariableDeclaration ---
class VariableDeclaration : public Statement {
public:
    std::string name;
    std::unique_ptr<Expression> initializer;
    VariableDeclaration(const std::string& n, std::unique_ptr<Expression> init)
        : name(n), initializer(std::move(init)) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "VariableDeclaration: " << name << std::endl;
        if (initializer) {
            initializer->print(indent + 2);
        }
    }
    llvm::Value* codegen(JITEngine& jit, JITSymbolTable& symbols) const {
        llvm::Value* initVal = initializer->codegen(jit, symbols);
        llvm::IRBuilder<>* builder = jit.builder.get();
        llvm::AllocaInst* alloca = builder->CreateAlloca(llvm::Type::getDoubleTy(jit.context), nullptr, name);
        builder->CreateStore(initVal, alloca);
        symbols[name] = alloca;
        return alloca;
    }
};
// --- JIT codegen for AssignmentStatement ---
class AssignmentStatement : public Statement {
public:
    std::string variable_name;
    std::unique_ptr<Expression> value;
    AssignmentStatement(const std::string& name, std::unique_ptr<Expression> val)
        : variable_name(name), value(std::move(val)) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Assignment: " << variable_name << std::endl;
        value->print(indent + 2);
    }
    llvm::Value* codegen(JITEngine& jit, JITSymbolTable& symbols) const {
        llvm::Value* val = value->codegen(jit, symbols);
        llvm::Value* var = symbols[variable_name];
        jit.builder->CreateStore(val, var);
        return val;
    }
};
// --- JIT codegen for Identifier ---
class Identifier : public Expression {
public:
    std::string name;
    Identifier(const std::string& n) : name(n) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Identifier: " << name << std::endl;
    }
    llvm::Value* codegen(JITEngine& jit, JITSymbolTable& symbols) const {
        llvm::Value* var = symbols[name];
        return jit.builder->CreateLoad(llvm::Type::getDoubleTy(jit.context), var, name + "_load");
    }
};
// --- JIT codegen for NumberLiteral (update signature) ---
class NumberLiteral : public Expression {
public:
    double value;
    NumberLiteral(double v) : value(v) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "NumberLiteral: " << value << std::endl;
    }
    llvm::Value* codegen(JITEngine& jit, JITSymbolTable&) const {
        return llvm::ConstantFP::get(jit.context, llvm::APFloat(value));
    }
};
// --- JIT codegen for BinaryOperation (update signature) ---
class BinaryOperation : public Expression {
public:
    std::unique_ptr<Expression> left;
    std::string operator_;
    std::unique_ptr<Expression> right;
    BinaryOperation(std::unique_ptr<Expression> l, const std::string& op, std::unique_ptr<Expression> r)
        : left(std::move(l)), operator_(op), right(std::move(r)) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "BinaryOperation: " << operator_ << std::endl;
        left->print(indent + 2);
        right->print(indent + 2);
    }
    llvm::Value* codegen(JITEngine& jit, JITSymbolTable& symbols) const {
        llvm::Value* l = left->codegen(jit, symbols);
        llvm::Value* r = right->codegen(jit, symbols);
        if (!l || !r) return nullptr;
        if (operator_ == "+") return jit.builder->CreateFAdd(l, r, "addtmp");
        if (operator_ == "-") return jit.builder->CreateFSub(l, r, "subtmp");
        if (operator_ == "*") return jit.builder->CreateFMul(l, r, "multmp");
        if (operator_ == "/") return jit.builder->CreateFDiv(l, r, "divtmp");
        if (operator_ == "**") return nullptr; // Not implemented
        return nullptr;
    }
};
// --- JIT codegen for FunctionCall (update signature) ---
class FunctionCall : public Expression {
public:
    std::string function_name;
    std::vector<std::unique_ptr<Expression>> arguments;
    FunctionCall(const std::string& name) : function_name(name) {}
    void addArgument(std::unique_ptr<Expression> arg) {
        arguments.push_back(std::move(arg));
    }
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "FunctionCall: " << function_name << std::endl;
        for (const auto& arg : arguments) {
            arg->print(indent + 2);
        }
    }
    llvm::Value* codegen(JITEngine& jit, JITSymbolTable& symbols) const {
        llvm::Function* calleeF = jit.module->getFunction(function_name);
        if (!calleeF) return nullptr;
        std::vector<llvm::Value*> argsV;
        for (const auto& arg : arguments) {
            argsV.push_back(arg->codegen(jit, symbols));
            if (!argsV.back()) return nullptr;
        }
        return jit.builder->CreateCall(calleeF, argsV, "calltmp");
    }
};

class ArrayLiteral : public Expression {
public:
    std::vector<std::unique_ptr<Expression>> elements;
    
    void addElement(std::unique_ptr<Expression> elem) {
        elements.push_back(std::move(elem));
    }
    
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "ArrayLiteral:" << std::endl;
        for (const auto& elem : elements) {
            elem->print(indent + 2);
        }
    }
};

class MapLiteral : public Expression {
public:
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> pairs;
    
    void addPair(const std::string& key, std::unique_ptr<Expression> value) {
        pairs.push_back({key, std::move(value)});
    }
    
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "MapLiteral:" << std::endl;
        for (const auto& pair : pairs) {
            std::cout << std::string(indent + 2, ' ') << "\"" << pair.first << "\":" << std::endl;
            pair.second->print(indent + 4);
        }
    }
};

class ArrayAccess : public Expression {
public:
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;
    
    ArrayAccess(std::unique_ptr<Expression> arr, std::unique_ptr<Expression> idx)
        : array(std::move(arr)), index(std::move(idx)) {}
    
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "ArrayAccess:" << std::endl;
        array->print(indent + 2);
        index->print(indent + 2);
    }
};

class MapAccess : public Expression {
public:
    std::unique_ptr<Expression> map;
    std::string key;
    
    MapAccess(std::unique_ptr<Expression> m, const std::string& k)
        : map(std::move(m)), key(k) {}
    
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "MapAccess: key=\"" << key << "\"" << std::endl;
        map->print(indent + 2);
    }
};

// Statement nodes
class Statement : public ASTNode {
public:
    virtual ~Statement() = default;
};

// --- JIT codegen for ReturnStatement ---
class ReturnStatement : public Statement {
public:
    std::unique_ptr<Expression> value;
    ReturnStatement(std::unique_ptr<Expression> val = nullptr) : value(std::move(val)) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "ReturnStatement:" << std::endl;
        if (value) {
            value->print(indent + 2);
        }
    }
    llvm::Value* codegen(JITEngine& jit, JITSymbolTable& symbols) const {
        llvm::Value* retVal = value ? value->codegen(jit, symbols) : llvm::ConstantFP::get(jit.context, llvm::APFloat(0.0));
        return jit.builder->CreateRet(retVal);
    }
};

class BlockStatement : public Statement {
public:
    std::vector<std::unique_ptr<Statement>> statements;
    
    void addStatement(std::unique_ptr<Statement> stmt) {
        statements.push_back(std::move(stmt));
    }
    
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Block:" << std::endl;
        for (const auto& stmt : statements) {
            stmt->print(indent + 2);
        }
    }
    llvm::Value* codegen(JITEngine& jit, JITSymbolTable& symbols) const {
        llvm::Value* last = nullptr;
        for (const auto& stmt : statements) {
            last = stmt->codegen(jit, symbols);
        }
        return last;
    }
};

// --- JIT codegen for FunctionDeclaration ---
class FunctionDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::string> parameters;
    std::unique_ptr<BlockStatement> body;
    FunctionDeclaration(const std::string& n) : name(n) {}
    void addParameter(const std::string& param) {
        parameters.push_back(param);
    }
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "FunctionDeclaration: " << name << std::endl;
        std::cout << std::string(indent + 2, ' ') << "Parameters: ";
        for (size_t i = 0; i < parameters.size(); i++) {
            std::cout << parameters[i];
            if (i < parameters.size() - 1) std::cout << ", ";
        }
        std::cout << std::endl;
        std::cout << std::string(indent + 2, ' ') << "Body:" << std::endl;
        body->print(indent + 4);
    }
    llvm::Function* codegen(JITEngine& jit) const {
        std::vector<llvm::Type*> doubles(parameters.size(), llvm::Type::getDoubleTy(jit.context));
        llvm::FunctionType* funcType = llvm::FunctionType::get(llvm::Type::getDoubleTy(jit.context), doubles, false);
        llvm::Function* function = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name, jit.module.get());
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(jit.context, "entry", function);
        jit.builder->SetInsertPoint(entry);
        JITSymbolTable symbols;
        unsigned idx = 0;
        for (auto& arg : function->args()) {
            arg.setName(parameters[idx]);
            llvm::AllocaInst* alloca = jit.builder->CreateAlloca(llvm::Type::getDoubleTy(jit.context), nullptr, parameters[idx]);
            jit.builder->CreateStore(&arg, alloca);
            symbols[parameters[idx]] = alloca;
            idx++;
        }
        llvm::Value* retVal = body->codegen(jit, symbols);
        if (!retVal) jit.builder->CreateRet(llvm::ConstantFP::get(jit.context, llvm::APFloat(0.0)));
        return function;
    }
};

class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<Statement>> statements;
    
    void addStatement(std::unique_ptr<Statement> stmt) {
        statements.push_back(std::move(stmt));
    }
    
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Program:" << std::endl;
        for (const auto& stmt : statements) {
            stmt->print(indent + 2);
        }
    }
};

// Enhanced Lexer
class Lexer {
private:
    std::string source;
    size_t position;
    int line;
    int column;
    
    std::unordered_map<std::string, TokenType> keywords = {
        {"let", TokenType::LET},
        {"print", TokenType::PRINT},
        {"if", TokenType::IF},
        {"else", TokenType::ELSE},
        {"while", TokenType::WHILE},
        {"for", TokenType::FOR},
        {"function", TokenType::FUNCTION},
        {"return", TokenType::RETURN}
    };
    
    char current_char() const {
        if (position >= source.length()) return '\0';
        return source[position];
    }
    
    char peek_char() const {
        if (position + 1 >= source.length()) return '\0';
        return source[position + 1];
    }
    
    void advance() {
        if (position < source.length()) {
            if (source[position] == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
            position++;
        }
    }
    
    void skip_whitespace() {
        while (std::isspace(current_char())) {
            advance();
        }
    }
    
    void skip_comment() {
        // Skip single-line comment
        if (current_char() == '/' && peek_char() == '/') {
            while (current_char() != '\n' && current_char() != '\0') {
                advance();
            }
        }
    }
    
    std::string read_number() {
        std::string result;
        while (std::isdigit(current_char()) || current_char() == '.') {
            result += current_char();
            advance();
        }
        return result;
    }
    
    std::string read_identifier() {
        std::string result;
        while (std::isalnum(current_char()) || current_char() == '_') {
            result += current_char();
            advance();
        }
        return result;
    }
    
    std::string read_string() {
        std::string result;
        advance(); // Skip opening quote
        while (current_char() != '"' && current_char() != '\0') {
            if (current_char() == '\\') {
                advance();
                switch (current_char()) {
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    default: result += current_char();
                }
            } else {
                result += current_char();
            }
            advance();
        }
        if (current_char() == '"') {
            advance(); // Skip closing quote
        }
        return result;
    }
    
public:
    Lexer(const std::string& src) : source(src), position(0), line(1), column(1) {}
    
    Token next_token() {
        skip_whitespace();
        skip_comment();
        skip_whitespace();
        
        if (position >= source.length()) {
            return Token(TokenType::EOF_TOKEN, "", line, column);
        }
        
        char ch = current_char();
        
        // String literals
        if (ch == '"') {
            return Token(TokenType::STRING_LITERAL, read_string(), line, column);
        }
        
        // Numbers (including decimals)
        if (std::isdigit(ch)) {
            return Token(TokenType::NUMBER, read_number(), line, column);
        }
        
        // Identifiers and keywords
        if (std::isalpha(ch) || ch == '_') {
            std::string id = read_identifier();
            TokenType type = keywords.count(id) ? keywords[id] : TokenType::IDENTIFIER;
            return Token(type, id, line, column);
        }
        
        // Two-character operators
        if (ch == '=' && peek_char() == '=') {
            advance(); advance();
            return Token(TokenType::EQUAL, "==", line, column - 1);
        }
        if (ch == '!' && peek_char() == '=') {
            advance(); advance();
            return Token(TokenType::NOT_EQUAL, "!=", line, column - 1);
        }
        if (ch == '<' && peek_char() == '=') {
            advance(); advance();
            return Token(TokenType::LESS_EQUAL, "<=", line, column - 1);
        }
        if (ch == '>' && peek_char() == '=') {
            advance(); advance();
            return Token(TokenType::GREATER_EQUAL, ">=", line, column - 1);
        }
        if (ch == '*' && peek_char() == '*') {
            advance(); advance();
            return Token(TokenType::POWER, "**", line, column - 1);
        }
        
        // Single character tokens
        advance();
        switch (ch) {
            case '+': return Token(TokenType::PLUS, "+", line, column - 1);
            case '-': return Token(TokenType::MINUS, "-", line, column - 1);
            case '*': return Token(TokenType::MULTIPLY, "*", line, column - 1);
            case '/': return Token(TokenType::DIVIDE, "/", line, column - 1);
            case '=': return Token(TokenType::ASSIGN, "=", line, column - 1);
            case '<': return Token(TokenType::LESS_THAN, "<", line, column - 1);
            case '>': return Token(TokenType::GREATER_THAN, ">", line, column - 1);
            case ';': return Token(TokenType::SEMICOLON, ";", line, column - 1);
            case '(': return Token(TokenType::LPAREN, "(", line, column - 1);
            case ')': return Token(TokenType::RPAREN, ")", line, column - 1);
            case '{': return Token(TokenType::LBRACE, "{", line, column - 1);
            case '}': return Token(TokenType::RBRACE, "}", line, column - 1);
            case '[': return Token(TokenType::LBRACKET, "[", line, column - 1);
            case ']': return Token(TokenType::RBRACKET, "]", line, column - 1);
            case ',': return Token(TokenType::COMMA, ",", line, column - 1);
            case '.': return Token(TokenType::DOT, ".", line, column - 1);
            case ':': return Token(TokenType::COLON, ":", line, column - 1);
            default:
                return Token(TokenType::INVALID, std::string(1, ch), line, column - 1);
        }
    }
};

// Enhanced Parser
class Parser {
private:
    Lexer& lexer;
    Token current_token;
    
    void advance() {
        current_token = lexer.next_token();
    }
    
    bool match(TokenType type) {
        if (current_token.type == type) {
            advance();
            return true;
        }
        return false;
    }
    
    void expect(TokenType type) {
        if (current_token.type != type) {
            throw std::runtime_error("Expected token type, got: " + current_token.value);
        }
        advance();
    }
    
    std::unique_ptr<Expression> parse_primary() {
        if (current_token.type == TokenType::NUMBER) {
            double value = std::stod(current_token.value);
            advance();
            return std::make_unique<NumberLiteral>(value);
        }
        
        if (current_token.type == TokenType::STRING_LITERAL) {
            std::string value = current_token.value;
            advance();
            return std::make_unique<StringLiteral>(value);
        }
        
        if (current_token.type == TokenType::IDENTIFIER) {
            std::string name = current_token.value;
            advance();
            
            // Function call
            if (current_token.type == TokenType::LPAREN) {
                advance();
                auto func_call = std::make_unique<FunctionCall>(name);
                
                if (current_token.type != TokenType::RPAREN) {
                    func_call->addArgument(parse_expression());
                    while (match(TokenType::COMMA)) {
                        func_call->addArgument(parse_expression());
                    }
                }
                expect(TokenType::RPAREN);
                return std::move(func_call);
            }
            
            // Array or map access
            if (current_token.type == TokenType::LBRACKET) {
                auto identifier = std::make_unique<Identifier>(name);
                advance();
                
                // Check if it's a string key (map access)
                if (current_token.type == TokenType::STRING_LITERAL) {
                    std::string key = current_token.value;
                    advance();
                    expect(TokenType::RBRACKET);
                    return std::make_unique<MapAccess>(std::move(identifier), key);
                } else {
                    // Numeric index (array access)
                    auto index = parse_expression();
                    expect(TokenType::RBRACKET);
                    return std::make_unique<ArrayAccess>(std::move(identifier), std::move(index));
                }
            }
            
            return std::make_unique<Identifier>(name);
        }
        
        // Array literal
        if (match(TokenType::LBRACKET)) {
            auto array = std::make_unique<ArrayLiteral>();
            if (current_token.type != TokenType::RBRACKET) {
                array->addElement(parse_expression());
                while (match(TokenType::COMMA)) {
                    array->addElement(parse_expression());
                }
            }
            expect(TokenType::RBRACKET);
            return std::move(array);
        }
        
        // Map literal
        if (match(TokenType::LBRACE)) {
            auto map = std::make_unique<MapLiteral>();
            if (current_token.type != TokenType::RBRACE) {
                // Parse key-value pairs
                do {
                    if (current_token.type != TokenType::STRING_LITERAL) {
                        throw std::runtime_error("Expected string key in map literal");
                    }
                    std::string key = current_token.value;
                    advance();
                    expect(TokenType::COLON);
                    auto value = parse_expression();
                    map->addPair(key, std::move(value));
                } while (match(TokenType::COMMA));
            }
            expect(TokenType::RBRACE);
            return std::move(map);
        }
        
        if (match(TokenType::LPAREN)) {
            auto expr = parse_expression();
            expect(TokenType::RPAREN);
            return expr;
        }
        
        throw std::runtime_error("Unexpected token: " + current_token.value);
    }
    
    std::unique_ptr<Expression> parse_power() {
        auto left = parse_primary();
        
        while (current_token.type == TokenType::POWER) {
            std::string op = current_token.value;
            advance();
            auto right = parse_primary();
            left = std::make_unique<BinaryOperation>(std::move(left), op, std::move(right));
        }
        
        return left;
    }
    
    std::unique_ptr<Expression> parse_term() {
        auto left = parse_power();
        
        while (current_token.type == TokenType::MULTIPLY || 
               current_token.type == TokenType::DIVIDE) {
            std::string op = current_token.value;
            advance();
            auto right = parse_power();
            left = std::make_unique<BinaryOperation>(std::move(left), op, std::move(right));
        }
        
        return left;
    }
    
    std::unique_ptr<Expression> parse_arithmetic() {
        auto left = parse_term();
        
        while (current_token.type == TokenType::PLUS || 
               current_token.type == TokenType::MINUS) {
            std::string op = current_token.value;
            advance();
            auto right = parse_term();
            left = std::make_unique<BinaryOperation>(std::move(left), op, std::move(right));
        }
        
        return left;
    }
    
    std::unique_ptr<Expression> parse_expression() {
        auto left = parse_arithmetic();
        
        while (current_token.type == TokenType::EQUAL || 
               current_token.type == TokenType::NOT_EQUAL ||
               current_token.type == TokenType::LESS_THAN ||
               current_token.type == TokenType::GREATER_THAN ||
               current_token.type == TokenType::LESS_EQUAL ||
               current_token.type == TokenType::GREATER_EQUAL) {
            std::string op = current_token.value;
            advance();
            auto right = parse_arithmetic();
            left = std::make_unique<BinaryOperation>(std::move(left), op, std::move(right));
        }
        
        return left;
    }
    
    std::unique_ptr<Statement> parse_block() {
        auto block = std::make_unique<BlockStatement>();
        expect(TokenType::LBRACE);
        
        while (current_token.type != TokenType::RBRACE && current_token.type != TokenType::EOF_TOKEN) {
            block->addStatement(parse_statement());
        }
        
        expect(TokenType::RBRACE);
        return std::move(block);
    }
    
    std::unique_ptr<Statement> parse_for_init() {
        if (match(TokenType::LET)) {
            std::string name = current_token.value;
            expect(TokenType::IDENTIFIER);
            expect(TokenType::ASSIGN);
            auto initializer = parse_expression();
            return std::make_unique<VariableDeclaration>(name, std::move(initializer));
        }
        
        if (current_token.type == TokenType::IDENTIFIER) {
            std::string name = current_token.value;
            advance();
            expect(TokenType::ASSIGN);
            auto value = parse_expression();
            return std::make_unique<AssignmentStatement>(name, std::move(value));
        }
        
        return nullptr;
    }
    
    std::unique_ptr<Statement> parse_statement() {
        if (match(TokenType::LET)) {
            if (current_token.type != TokenType::IDENTIFIER) {
                throw std::runtime_error("Expected identifier after 'let'");
            }
            std::string name = current_token.value;
            advance();
            
            expect(TokenType::ASSIGN);
            auto initializer = parse_expression();
            expect(TokenType::SEMICOLON);
            
            return std::make_unique<VariableDeclaration>(name, std::move(initializer));
        }
        
        if (match(TokenType::PRINT)) {
            expect(TokenType::LPAREN);
            auto expr = parse_expression();
            expect(TokenType::RPAREN);
            expect(TokenType::SEMICOLON);
            
            return std::make_unique<PrintStatement>(std::move(expr));
        }
        
        if (match(TokenType::IF)) {
            expect(TokenType::LPAREN);
            auto condition = parse_expression();
            expect(TokenType::RPAREN);
            
            auto then_branch = parse_block();
            
            std::unique_ptr<Statement> else_branch = nullptr;
            if (match(TokenType::ELSE)) {
                else_branch = parse_block();
            }
            
            return std::make_unique<IfStatement>(std::move(condition), std::move(then_branch), std::move(else_branch));
        }
        
        if (match(TokenType::WHILE)) {
            expect(TokenType::LPAREN);
            auto condition = parse_expression();
            expect(TokenType::RPAREN);
            
            auto body = parse_block();
            
            return std::make_unique<WhileStatement>(std::move(condition), std::move(body));
        }
        
        if (match(TokenType::FOR)) {
            expect(TokenType::LPAREN);
            
            auto init = parse_for_init();
            expect(TokenType::SEMICOLON);
            
            std::unique_ptr<Expression> condition = nullptr;
            if (current_token.type != TokenType::SEMICOLON) {
                condition = parse_expression();
            }
            expect(TokenType::SEMICOLON);
            
            std::unique_ptr<Statement> update = nullptr;
            if (current_token.type != TokenType::RPAREN) {
                if (current_token.type == TokenType::IDENTIFIER) {
                    std::string name = current_token.value;
                    advance();
                    expect(TokenType::ASSIGN);
                    auto value = parse_expression();
                    update = std::make_unique<AssignmentStatement>(name, std::move(value));
                }
            }
            expect(TokenType::RPAREN);
            
            auto body = parse_block();
            
            return std::make_unique<ForStatement>(std::move(init), std::move(condition), 
                                                  std::move(update), std::move(body));
        }
        
        if (match(TokenType::FUNCTION)) {
            std::string name = current_token.value;
            expect(TokenType::IDENTIFIER);
            expect(TokenType::LPAREN);
            
            auto func = std::make_unique<FunctionDeclaration>(name);
            
            if (current_token.type != TokenType::RPAREN) {
                func->addParameter(current_token.value);
                expect(TokenType::IDENTIFIER);
                while (match(TokenType::COMMA)) {
                    func->addParameter(current_token.value);
                    expect(TokenType::IDENTIFIER);
                }
            }
            expect(TokenType::RPAREN);
            
            func->body = std::unique_ptr<BlockStatement>(
                dynamic_cast<BlockStatement*>(parse_block().release())
            );
            
            return std::move(func);
        }
        
        if (match(TokenType::RETURN)) {
            std::unique_ptr<Expression> value = nullptr;
            if (current_token.type != TokenType::SEMICOLON) {
                value = parse_expression();
            }
            expect(TokenType::SEMICOLON);
            return std::make_unique<ReturnStatement>(std::move(value));
        }
        
        // Assignment statement (existing variable)
        if (current_token.type == TokenType::IDENTIFIER) {
            std::string name = current_token.value;
            advance();
            expect(TokenType::ASSIGN);
            auto value = parse_expression();
            expect(TokenType::SEMICOLON);
            
            return std::make_unique<AssignmentStatement>(name, std::move(value));
        }
        
        throw std::runtime_error("Unexpected statement: " + current_token.value);
    }
    
public:
    Parser(Lexer& l) : lexer(l) {
        advance();
    }
    
    std::unique_ptr<Program> parse() {
        auto program = std::make_unique<Program>();
        
        while (current_token.type != TokenType::EOF_TOKEN) {
            program->addStatement(parse_statement());
        }
        
        return program;
    }
};

// Return value for functions
struct ReturnValue {
    Value value;
    bool has_value;
    
    ReturnValue() : has_value(false) {}
    ReturnValue(const Value& v) : value(v), has_value(true) {}
};

class Interpreter {
private:
    std::unordered_map<std::string, Value> global_variables;
    std::vector<std::unordered_map<std::string, Value>> local_scopes;
    bool in_function = false;
    ReturnValue return_value;
    
    Value& get_variable(const std::string& name) {
        // Check local scopes from innermost to outermost
        for (auto it = local_scopes.rbegin(); it != local_scopes.rend(); ++it) {
            if (it->find(name) != it->end()) {
                return (*it)[name];
            }
        }
        // Check global scope
        if (global_variables.find(name) != global_variables.end()) {
            return global_variables[name];
        }
        throw std::runtime_error("Undefined variable: " + name);
    }
    
    void set_variable(const std::string& name, const Value& value) {
        // If in local scope, set in the innermost scope
        if (!local_scopes.empty()) {
            local_scopes.back()[name] = value;
        } else {
            global_variables[name] = value;
        }
    }
    
    Value call_builtin_function(const std::string& name, const std::vector<Value>& args) {
        // Math functions
        if (name == "sqrt" && args.size() == 1 && args[0].type == Value::NUMBER) {
            return Value(std::sqrt(args[0].number_value));
        }
        if (name == "pow" && args.size() == 2 && args[0].type == Value::NUMBER && args[1].type == Value::NUMBER) {
            return Value(std::pow(args[0].number_value, args[1].number_value));
        }
        if (name == "log" && args.size() == 1 && args[0].type == Value::NUMBER) {
            return Value(std::log(args[0].number_value));
        }
        if (name == "exp" && args.size() == 1 && args[0].type == Value::NUMBER) {
            return Value(std::exp(args[0].number_value));
        }
        if (name == "abs" && args.size() == 1 && args[0].type == Value::NUMBER) {
            return Value(std::abs(args[0].number_value));
        }
        
        // String functions
        if (name == "len" && args.size() == 1) {
            if (args[0].type == Value::STRING) {
                return Value(static_cast<double>(args[0].string_value.length()));
            }
            if (args[0].type == Value::ARRAY) {
                return Value(static_cast<double>(args[0].array_value.size()));
            }
            if (args[0].type == Value::MAP) {
                return Value(static_cast<double>(args[0].map_value.size()));
            }
        }
        
        // Array statistical functions
        if (name == "mean" && args.size() == 1 && args[0].type == Value::ARRAY) {
            const auto& arr = args[0].array_value;
            if (arr.empty()) return Value(0.0);
            double sum = 0;
            for (const auto& val : arr) {
                if (val.type != Value::NUMBER) throw std::runtime_error("mean() requires numeric array");
                sum += val.number_value;
            }
            return Value(sum / arr.size());
        }
        
        if (name == "std" && args.size() == 1 && args[0].type == Value::ARRAY) {
            const auto& arr = args[0].array_value;
            if (arr.size() <= 1) return Value(0.0);
            
            double mean = 0;
            for (const auto& val : arr) {
                if (val.type != Value::NUMBER) throw std::runtime_error("std() requires numeric array");
                mean += val.number_value;
            }
            mean /= arr.size();
            
            double variance = 0;
            for (const auto& val : arr) {
                variance += (val.number_value - mean) * (val.number_value - mean);
            }
            variance /= (arr.size() - 1);
            
            return Value(std::sqrt(variance));
        }
        
        if (name == "max" && args.size() == 1 && args[0].type == Value::ARRAY) {
            const auto& arr = args[0].array_value;
            if (arr.empty()) return Value(0.0);
            double max_val = arr[0].number_value;
            for (const auto& val : arr) {
                if (val.type != Value::NUMBER) throw std::runtime_error("max() requires numeric array");
                if (val.number_value > max_val) max_val = val.number_value;
            }
            return Value(max_val);
        }
        
        if (name == "min" && args.size() == 1 && args[0].type == Value::ARRAY) {
            const auto& arr = args[0].array_value;
            if (arr.empty()) return Value(0.0);
            double min_val = arr[0].number_value;
            for (const auto& val : arr) {
                if (val.type != Value::NUMBER) throw std::runtime_error("min() requires numeric array");
                if (val.number_value < min_val) min_val = val.number_value;
            }
            return Value(min_val);
        }
        
        if (name == "sum" && args.size() == 1 && args[0].type == Value::ARRAY) {
            const auto& arr = args[0].array_value;
            double total = 0;
            for (const auto& val : arr) {
                if (val.type != Value::NUMBER) throw std::runtime_error("sum() requires numeric array");
                total += val.number_value;
            }
            return Value(total);
        }
        
        // Type conversion functions
        if (name == "str" && args.size() == 1) {
            return Value(args[0].to_string());
        }
        
        if (name == "num" && args.size() == 1 && args[0].type == Value::STRING) {
            try {
                return Value(std::stod(args[0].string_value));
            } catch (...) {
                throw std::runtime_error("Cannot convert string to number: " + args[0].string_value);
            }
        }
        
        throw std::runtime_error("Unknown function: " + name);
    }
    
    Value call_user_function(FunctionDeclaration* func, const std::vector<Value>& args) {
        if (args.size() != func->parameters.size()) {
            throw std::runtime_error("Function " + func->name + " expects " + 
                                   std::to_string(func->parameters.size()) + " arguments, got " + 
                                   std::to_string(args.size()));
        }
        
        // Create new scope for function
        local_scopes.push_back({});
        
        // Bind arguments to parameters
        for (size_t i = 0; i < args.size(); i++) {
            local_scopes.back()[func->parameters[i]] = args[i];
        }
        
        // Save current function state
        bool prev_in_function = in_function;
        ReturnValue prev_return = return_value;
        
        in_function = true;
        return_value = ReturnValue();
        
        // Execute function body
        try {
            execute_statement(func->body.get());
        } catch (...) {
            // Clean up and rethrow
            local_scopes.pop_back();
            in_function = prev_in_function;
            return_value = prev_return;
            throw;
        }
        
        // Get return value
        Value result = return_value.has_value ? return_value.value : Value(0.0);
        
        // Restore previous function state
        in_function = prev_in_function;
        return_value = prev_return;
        
        // Remove function scope
        local_scopes.pop_back();
        
        return result;
    }
    
public:
    Value evaluate_expression(const Expression* expr) {
        if (auto num = dynamic_cast<const NumberLiteral*>(expr)) {
            return Value(num->value);
        }
        
        if (auto str = dynamic_cast<const StringLiteral*>(expr)) {
            return Value(str->value);
        }
        
        if (auto id = dynamic_cast<const Identifier*>(expr)) {
            return get_variable(id->name);
        }
        
        if (auto arr = dynamic_cast<const ArrayLiteral*>(expr)) {
            std::vector<Value> values;
            for (const auto& elem : arr->elements) {
                values.push_back(evaluate_expression(elem.get()));
            }
            return Value(values);
        }
        
        if (auto map = dynamic_cast<const MapLiteral*>(expr)) {
            std::unordered_map<std::string, Value> map_val;
            for (const auto& pair : map->pairs) {
                map_val[pair.first] = evaluate_expression(pair.second.get());
            }
            return Value(map_val);
        }
        
        if (auto access = dynamic_cast<const ArrayAccess*>(expr)) {
            Value array_val = evaluate_expression(access->array.get());
            Value index_val = evaluate_expression(access->index.get());
            
            if (array_val.type != Value::ARRAY || index_val.type != Value::NUMBER) {
                throw std::runtime_error("Invalid array access");
            }
            
            int index = static_cast<int>(index_val.number_value);
            if (index < 0 || index >= static_cast<int>(array_val.array_value.size())) {
                throw std::runtime_error("Array index out of bounds");
            }
            
            return array_val.array_value[index];
        }
        
        if (auto access = dynamic_cast<const MapAccess*>(expr)) {
            Value map_val = evaluate_expression(access->map.get());
            
            if (map_val.type != Value::MAP) {
                throw std::runtime_error("Invalid map access");
            }
            
            auto it = map_val.map_value.find(access->key);
            if (it == map_val.map_value.end()) {
                throw std::runtime_error("Key not found in map: " + access->key);
            }
            
            return it->second;
        }
        
        if (auto func_call = dynamic_cast<const FunctionCall*>(expr)) {
            // Evaluate arguments
            std::vector<Value> args;
            for (const auto& arg : func_call->arguments) {
                args.push_back(evaluate_expression(arg.get()));
            }
            
            // Check if it's a user-defined function
            try {
                Value func_val = get_variable(func_call->function_name);
                if (func_val.type == Value::FUNCTION) {
                    return call_user_function(func_val.function_value, args);
                }
            } catch (...) {
                // Not a variable, try built-in function
            }
            
            // Try built-in function
            return call_builtin_function(func_call->function_name, args);
        }
        
        if (auto binop = dynamic_cast<const BinaryOperation*>(expr)) {
            Value left = evaluate_expression(binop->left.get());
            Value right = evaluate_expression(binop->right.get());
            
            // String concatenation
            if (binop->operator_ == "+" && (left.type == Value::STRING || right.type == Value::STRING)) {
                return Value(left.to_string() + right.to_string());
            }
            
            // Numeric operations
            if (left.type == Value::NUMBER && right.type == Value::NUMBER) {
                double l = left.number_value;
                double r = right.number_value;
                
                // Arithmetic operators
                if (binop->operator_ == "+") return Value(l + r);
                if (binop->operator_ == "-") return Value(l - r);
                if (binop->operator_ == "*") return Value(l * r);
                if (binop->operator_ == "/") return Value(l / r);
                if (binop->operator_ == "**") return Value(std::pow(l, r));
                
                // Comparison operators
                if (binop->operator_ == "==") return Value(l == r ? 1 : 0);
                if (binop->operator_ == "!=") return Value(l != r ? 1 : 0);
                if (binop->operator_ == "<") return Value(l < r ? 1 : 0);
                if (binop->operator_ == ">") return Value(l > r ? 1 : 0);
                if (binop->operator_ == "<=") return Value(l <= r ? 1 : 0);
                if (binop->operator_ == ">=") return Value(l >= r ? 1 : 0);
            }
            
            // String comparison
            if (left.type == Value::STRING && right.type == Value::STRING) {
                if (binop->operator_ == "==") return Value(left.string_value == right.string_value ? 1 : 0);
                if (binop->operator_ == "!=") return Value(left.string_value != right.string_value ? 1 : 0);
            }
            
            throw std::runtime_error("Invalid operation: " + binop->operator_ + " on " + 
                                   left.to_string() + " and " + right.to_string());
        }
        
        throw std::runtime_error("Unknown expression type");
    }
    
    void execute_statement(const Statement* stmt) {
        if (return_value.has_value && in_function) {
            return; // Early exit if we've hit a return statement
        }
        
        if (auto vardecl = dynamic_cast<const VariableDeclaration*>(stmt)) {
            Value value = evaluate_expression(vardecl->initializer.get());
            set_variable(vardecl->name, value);
        } 
        else if (auto assignment = dynamic_cast<const AssignmentStatement*>(stmt)) {
            get_variable(assignment->variable_name); // Check it exists
            Value value = evaluate_expression(assignment->value.get());
            set_variable(assignment->variable_name, value);
        }
        else if (auto print = dynamic_cast<const PrintStatement*>(stmt)) {
            Value result = evaluate_expression(print->expression.get());
            std::cout << result.to_string() << std::endl;
        }
        else if (auto block = dynamic_cast<const BlockStatement*>(stmt)) {
            // Create new scope
            local_scopes.push_back({});
            
            for (const auto& s : block->statements) {
                execute_statement(s.get());
                if (return_value.has_value && in_function) break;
            }
            
            // Remove scope
            local_scopes.pop_back();
        }
        else if (auto if_stmt = dynamic_cast<const IfStatement*>(stmt)) {
            Value condition_result = evaluate_expression(if_stmt->condition.get());
            if (condition_result.is_truthy()) {
                execute_statement(if_stmt->then_branch.get());
            } else if (if_stmt->else_branch) {
                execute_statement(if_stmt->else_branch.get());
            }
        }
        else if (auto while_stmt = dynamic_cast<const WhileStatement*>(stmt)) {
            while (true) {
                Value condition_result = evaluate_expression(while_stmt->condition.get());
                if (!condition_result.is_truthy()) {
                    break;
                }
                execute_statement(while_stmt->body.get());
                if (return_value.has_value && in_function) break;
            }
        }
        else if (auto for_stmt = dynamic_cast<const ForStatement*>(stmt)) {
            // Create new scope for loop variable
            local_scopes.push_back({});
            
            // Execute init
            if (for_stmt->init) {
                execute_statement(for_stmt->init.get());
            }
            
            // Loop
            while (true) {
                // Check condition
                if (for_stmt->condition) {
                    Value cond = evaluate_expression(for_stmt->condition.get());
                    if (!cond.is_truthy()) break;
                }
                
                // Execute body
                execute_statement(for_stmt->body.get());
                if (return_value.has_value && in_function) break;
                
                // Execute update
                if (for_stmt->update) {
                    execute_statement(for_stmt->update.get());
                }
            }
            
            // Remove loop scope
            local_scopes.pop_back();
        }
        else if (auto func_decl = dynamic_cast<const FunctionDeclaration*>(stmt)) {
            // Store function in global scope
            global_variables[func_decl->name] = Value(const_cast<FunctionDeclaration*>(func_decl));
        }
        else if (auto ret_stmt = dynamic_cast<const ReturnStatement*>(stmt)) {
            if (!in_function) {
                throw std::runtime_error("Return statement outside of function");
            }
            if (ret_stmt->value) {
                return_value = ReturnValue(evaluate_expression(ret_stmt->value.get()));
            } else {
                return_value = ReturnValue(Value(0.0));
            }
        }
    }
    
    void execute(const Program* program) {
        for (const auto& stmt : program->statements) {
            execute_statement(stmt.get());
        }
    }
};

// --- JIT Engine for LLVM ---
class JITEngine {
public:
    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;
    llvm::ExecutionEngine* executionEngine;

    JITEngine(const std::string& moduleName) {
        module = std::make_unique<llvm::Module>(moduleName, context);
        builder = std::make_unique<llvm::IRBuilder<>>(context);
        std::string errStr;
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        executionEngine = llvm::EngineBuilder(std::move(module))
            .setErrorStr(&errStr)
            .setEngineKind(llvm::EngineKind::JIT)
            .create();
        if (!executionEngine) {
            throw std::runtime_error("Failed to create ExecutionEngine: " + errStr);
        }
        module = std::unique_ptr<llvm::Module>(executionEngine->getModuleForNewFunction());
    }

    llvm::Function* createMainFunction() {
        llvm::FunctionType* funcType = llvm::FunctionType::get(llvm::Type::getDoubleTy(context), false);
        llvm::Function* func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "main_jit", module.get());
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", func);
        builder->SetInsertPoint(entry);
        return func;
    }

    double runMainFunction() {
        executionEngine->finalizeObject();
        std::vector<llvm::GenericValue> noargs;
        llvm::GenericValue gv = executionEngine->runFunction(module->getFunction("main_jit"), noargs);
        return gv.DoubleVal;
    }
};

// --- Add codegen() methods to AST nodes ---
// Forward declaration
class JITEngine;

// --- Demo: JIT compile and run a simple arithmetic expression ---
// Add this to main() before the interpreter demo
    std::cout << "\n=== JIT Demo (arithmetic: 2 + 3 * 4) ===" << std::endl;
    try {
        JITEngine jit("jit_module");
        llvm::Function* mainFunc = jit.createMainFunction();
        // Build AST for 2 + 3 * 4
        auto expr = std::make_unique<BinaryOperation>(
            std::make_unique<NumberLiteral>(2),
            "+",
            std::make_unique<BinaryOperation>(
                std::make_unique<NumberLiteral>(3),
                "*",
                std::make_unique<NumberLiteral>(4)
            )
        );
        llvm::Value* retVal = expr->codegen(jit, {}); // Pass empty symbol table for now
        jit.builder->CreateRet(retVal);
        double result = jit.runMainFunction();
        std::cout << "JIT result: " << result << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "JIT Error: " << e.what() << std::endl;
    }

// --- Demo: JIT compile and run a user-defined function with control flow ---
// Add this to main() before the interpreter demo
    std::cout << "\n=== JIT Demo: User Function and Control Flow ===" << std::endl;
    try {
        JITEngine jit("jit_module2");
        // function sumToN(n) { let sum = 0; for (let i = 1; i <= n; i = i + 1) { sum = sum + i; } return sum; }
        auto funcBody = std::make_unique<BlockStatement>();
        funcBody->addStatement(std::make_unique<VariableDeclaration>("sum", std::make_unique<NumberLiteral>(0)));
        auto forBody = std::make_unique<BlockStatement>();
        forBody->addStatement(std::make_unique<AssignmentStatement>("sum",
            std::make_unique<BinaryOperation>(
                std::make_unique<Identifier>("sum"),
                "+",
                std::make_unique<Identifier>("i")
            )));
        auto forStmt = std::make_unique<ForStatement>(
            std::make_unique<VariableDeclaration>("i", std::make_unique<NumberLiteral>(1)),
            std::make_unique<BinaryOperation>(
                std::make_unique<Identifier>("i"),
                "<=",
                std::make_unique<Identifier>("n")
            ),
            std::make_unique<AssignmentStatement>("i",
                std::make_unique<BinaryOperation>(
                    std::make_unique<Identifier>("i"),
                    "+",
                    std::make_unique<NumberLiteral>(1)
                )
            ),
            std::move(forBody)
        );
        funcBody->addStatement(std::move(forStmt));
        funcBody->addStatement(std::make_unique<ReturnStatement>(std::make_unique<Identifier>("sum")));
        auto sumToN = std::make_unique<FunctionDeclaration>("sumToN");
        sumToN->addParameter("n");
        sumToN->body = std::move(funcBody);
        sumToN->codegen(jit);
        // main_jit: call sumToN(10)
        llvm::Function* mainFunc = jit.createMainFunction();
        JITSymbolTable mainSymbols;
        auto callExpr = std::make_unique<FunctionCall>("sumToN");
        callExpr->addArgument(std::make_unique<NumberLiteral>(10));
        llvm::Value* retVal = callExpr->codegen(jit, mainSymbols);
        jit.builder->CreateRet(retVal);
        double result = jit.runMainFunction();
        std::cout << "sumToN(10) = " << result << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "JIT Error: " << e.what() << std::endl;
    }

// Demo program showcasing all new features
int main() {
    std::string code = R"(
        // 1. String support
        let message = "Hello, World!";
        print(message);
        print("Length: " + str(len(message)));
        
        // String concatenation
        let first = "Hello";
        let second = " World";
        let combined = first + second;
        print(combined);
        
        // 2. Map/Dictionary support
        let person = {"name": "Alice", "age": "30", "city": "New York"};
        print("Name: " + person["name"]);
        print("Age: " + person["age"]);
        
        // 3. For loops
        print("\nCounting with for loop:");
        for (let i = 0; i < 5; i = i + 1) {
            print("Count: " + str(i));
        }
        
        // 4. User-defined functions
        function factorial(n) {
            if (n <= 1) {
                return 1;
            }
            return n * factorial(n - 1);
        }
        
        print("\nFactorials:");
        for (let i = 1; i <= 5; i = i + 1) {
            print(str(i) + "! = " + str(factorial(i)));
        }
        
        // Fibonacci function
        function fibonacci(n) {
            if (n <= 1) {
                return n;
            }
            return fibonacci(n - 1) + fibonacci(n - 2);
        }
        
        print("\nFibonacci sequence:");
        for (let i = 0; i < 10; i = i + 1) {
            print("fib(" + str(i) + ") = " + str(fibonacci(i)));
        }
        
        // Function with multiple parameters
        function greet(name, age) {
            return "Hello " + name + ", you are " + str(age) + " years old!";
        }
        
        print("\n" + greet("Bob", 25));
        
        // Array processing with functions
        function array_sum(arr) {
            let total = 0;
            for (let i = 0; i < len(arr); i = i + 1) {
                total = total + arr[i];
            }
            return total;
        }
        
        let numbers = [1, 2, 3, 4, 5];
        print("\nSum of array: " + str(array_sum(numbers)));
        
        // Nested data structures
        let data = {"scores": [85, 90, 78, 92, 88], "name": "Test Results"};
        print("\n" + data["name"] + " - Average: " + str(mean(data["scores"])));
    )";
    
    try {
        Lexer lexer(code);
        Parser parser(lexer);
        auto program = parser.parse();
        
        std::cout << "=== AST ===" << std::endl;
        program->print();
        
        std::cout << "\n=== Execution ===" << std::endl;
        Interpreter interpreter;
        interpreter.execute(program.get());
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}