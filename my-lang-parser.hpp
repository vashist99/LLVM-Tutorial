#include<bits/stdc++.h>
#include "my-lang-lexer.hpp"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Any.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"


///Abstract Syntax Tree

//base class for all expression nodes
class ExprAST {
    public:
        virtual ~ExprAST() {}
        virtual llvm::Value *codegen() = 0;
};


/// NumberExprAST - Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
    double Val;

    public:
        NumberExprAST(double Val) : Val(Val) {}
        virtual llvm::Value *codegen();
};


/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
    std::string Name;

    public:
        VariableExprAST(const std::string &Name) : Name(Name) {}
        virtual llvm::Value *codegen();
};


/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;

    public:
        BinaryExprAST(char op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS):
            Op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
        
        virtual llvm::Value *codegen();
};


/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;

    public:
        CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args):
            Callee(Callee), Args(std::move(Args)) {}
        
        virtual llvm::Value *codegen();
};

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).

class PrototypeAST{
    std::string Name;
    std::vector<std::string> Args;

    public:
        PrototypeAST(const std::string &Name, std::vector<std::string> Args):
        Name(Name), Args(std::move(Args)) {}

        const std::string &getName() const {return Name;}

        virtual llvm::Function *codegen();
};

/// FunctionAST - This class represents a function definition itself.
class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

    public:
        FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body):
            Proto(std::move(Proto)), Body(std::move(Body)) {}
        
        virtual llvm::Function *codegen();
};


///Function Protoypes:
static std::unique_ptr<ExprAST> ParseExpression();
static std::unique_ptr<ExprAST> ParseBinOpRHS(int,std::unique_ptr<ExprAST>);


/// CurTok/getNextToken - Provide a simple token buffer.  CurTok is the current
/// token the parser is looking at.  getNextToken reads another token from the
/// lexer and updates CurTok with its results.
static int CurTok;
static int getNextToken(){
    return CurTok = gettok();
}


/// LogError* - These are little helper functions for error handling.
std::unique_ptr<ExprAST> LogError(const char *Str){
    fprintf(stderr, "LogError: %s\n", Str);
    return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
    LogError(Str);
    return nullptr;
}
/// identifierexpr
///   ::= identifier
///   ::= identifier '(' expression* ')'

///Basic Expression Parsing

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken(); //consume the number
    return std::move(Result);
}

/// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken(); //eat (.
    auto V = ParseExpression();
    if(!V)
        return nullptr;
    
    if(CurTok != ')')
        return LogError("expected ')'");
    getNextToken(); //eat ).
    return V;
}


/// identifierexpr
///   ::= identifier
///   ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierOrCallExpr(){
    std::string IdName = IdentifierStr;

    getNextToken(); //eat identifier.

    if(CurTok != '(') //Simple variable ref.
        return std::make_unique<VariableExprAST>(IdName);

    getNextToken();// eat '('.
    std::vector<std::unique_ptr<ExprAST>> Args;
    if(CurTok!=')') {
        while(1){
            if(auto Arg = ParseExpression())
                Args.push_back(std::move(Arg));
            else
                return nullptr;
            
            if(CurTok == ')')   
                break;
            
            if(CurTok != ',')
                return LogError("Expected ')' or ',' in argument list");
            getNextToken();
        }
    }

    getNextToken(); //eat the ')'.
    return std::make_unique<CallExprAST> (IdName, std::move(Args));
}

/// primary
///   ::= identifierexpr
///   ::= numberexpr
///   ::= parenexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
    switch (CurTok) {
        default:
            return LogError("unknown token when expecting an expression");
        case tok_identifier:
            return ParseIdentifierOrCallExpr();
        case tok_number:
            return ParseNumberExpr();
        case '(':
            return ParseParenExpr();
    }
}

///Binary Expression Parsing

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.

static std::map<char,int> BinopPrecedence;

//GetTokPrecedence - Get the precedence of the pending binary operator token
static int GetTokPrecedence() {
    if(!isascii(CurTok))
        return -1;
    
    //Make sure it's declared binop.
    // int tokPrec = BinopPrecedence[CurTok];
    // if (TokPrec <= 0) return -1;
    // return TokPrec;

    switch(CurTok){
        case '<':
        case '>':
            return 10;
        case '+':
        case '-':
            return 20;
        case '*':
        case '/':
            return 40;
        default:
            return -1;

    }
}

// int main() {
//   // Install standard binary operators.
//   // 1 is lowest precedence.
//   BinopPrecedence['<'] = 10;
//   BinopPrecedence['+'] = 20;
//   BinopPrecedence['-'] = 20;
//   BinopPrecedence['*'] = 40;  // highest.
//   ...
// }


/// expression
///   ::= primary binoprhs
///
static std::unique_ptr<ExprAST> ParseExpression(){
    auto LHS = ParsePrimary();
    if (!LHS)
        return nullptr;
    
    return ParseBinOpRHS(0,std::move(LHS));
}


/// binoprhs
///   ::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,std::unique_ptr<ExprAST> LHS){
    // If this is a binop, find its precedence.
    while(1){
        int TokPrec = GetTokPrecedence();

        // If this is a binop that binds at least as tightly as the current binop,
        // consume it, otherwise we are done.
        if(TokPrec<ExprPrec)
            return LHS;
        
        // Okay, we know this is a binop.
        int BinOp = CurTok;
        getNextToken();  // eat binop

        // Parse the primary expression after the binary operator.
        auto RHS = ParsePrimary();
        if(!RHS)
            return nullptr;

        // If BinOp binds less tightly with RHS than the operator after RHS, let
        // the pending operator take RHS as its LHS.
        int NextPrec = GetTokPrecedence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec+1,std::move(RHS));
            if(!RHS)
                return nullptr;
        }

        // Merge LHS/RHS.
        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS),std::move(RHS));

    }
}

///Parsing the Rest
static std::unique_ptr<PrototypeAST> ParsePrototype() {
    if( CurTok != tok_identifier)
        return LogErrorP("Expected function name in prototype");
    
    std::string FnName = IdentifierStr;
    getNextToken();
    
    if(CurTok != '(')
        return LogErrorP("Expected '(' in prototype");

    //Read the list of argument names.
    std::vector<std::string> ArgNames;
    while(getNextToken() == tok_identifier)
        ArgNames.push_back(IdentifierStr);
    
    if(CurTok != ')')
        return LogErrorP("Expected ')' in prototype");

    //Success.
    getNextToken(); //eat ')'.

    return std::make_unique<PrototypeAST> (FnName, std::move(ArgNames));
}

/// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
    getNextToken(); //eat def.
    auto Proto = ParsePrototype();
    if(!Proto) return nullptr;

    if (auto E = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto),std::move(E));
    return nullptr; 
}

/// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken();  // eat extern.
  return ParsePrototype();
}

/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    // Make an anonymous proto.
    auto Proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Top-Level parsing
//===----------------------------------------------------------------------===//

// static void HandleDefinition() {
//   if (ParseDefinition()) {
//     fprintf(stderr, "Parsed a function definition.\n");
//   } else {
//     // Skip token for error recovery.
//     getNextToken();
//   }
// }

// static void HandleExtern() {
//   if (ParseExtern()) {
//     fprintf(stderr, "Parsed an extern\n");
//   } else {
//     // Skip token for error recovery.
//     getNextToken();
//   }
// }

// static void HandleTopLevelExpression() {
//   // Evaluate a top-level expression into an anonymous function.
//   if (ParseTopLevelExpr()) {
//     fprintf(stderr, "Parsed a top-level expr\n");
//   } else {
//     // Skip token for error recovery.
//     getNextToken();
//   }
// }

// /// top ::= definition | external | expression | ';'
// static void MainLoop() {
//   while (true) {
//     fprintf(stderr, "ready> ");
//     switch (CurTok) {
//     case tok_eof:
//       return;
//     case ';': // ignore top-level semicolons.
//       getNextToken();
//       break;
//     case tok_def:
//       HandleDefinition();
//       break;
//     case tok_extern:
//       HandleExtern();
//       break;
//     default:
//       HandleTopLevelExpression();
//       break;
//     }
//   }
// }

// int main() {
//     fprintf(stderr, "ready>");
//     getNextToken();

//     //run the main "interpreter loop" now.
//     MainLoop();
//     return 0;

// }