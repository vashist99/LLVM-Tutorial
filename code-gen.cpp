#include<bits/stdc++.h>
#include "my-lang-parser.hpp"
//#include "/usr/include/KaleidoscopeJIT.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Pass.h"

//using namespace llvm::orc;


//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

//llvm declarations
static std::unique_ptr<llvm::LLVMContext> TheContext;
static std::unique_ptr<llvm::Module> TheModule;
static std::unique_ptr<llvm::IRBuilder<>> Builder;
static std::map<std::string, llvm::Value *> NamedValues;


llvm::Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}


//===----------------------------------------------------------------------===//
// For Optimization Pass Manager method
//===----------------------------------------------------------------------===//

static std::unique_ptr<llvm::legacy::FunctionPassManager> TheFPM;
//static std::unique_ptr<KaleidoscopeJIT> TheJIT;
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
static llvm::ExitOnError ExitOnErr;

// --------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------

llvm::Value *NumberExprAST::codegen() {
    return llvm::ConstantFP::get(*TheContext, llvm::APFloat(Val));
}

llvm::Value *VariableExprAST::codegen() {
    //Look this variable up in the function.
    llvm::Value *V = NamedValues[Name];
    if(!V){
        LogError("Unknown variable name.");
    }
    return V;
}


llvm::Value *BinaryExprAST::codegen(){
    llvm::Value *L = LHS->codegen();
    llvm::Value *R = RHS->codegen();
    if(!L||!R)
        return nullptr;
    
    switch(Op) {
        case '+':
            return Builder->CreateFAdd(L,R,"addtmp");
        case '-':
            return Builder->CreateFSub(L,R,"subtmp");
        case '*':
            return Builder->CreateFMul(L,R,"multmp");
        case '<':
            return Builder->CreateFCmpULT(L,R,"cmptmp");
            //convert bool 0/1 to double 0.0 or 1.0
            return Builder->CreateUIToFP(L, llvm::Type::getDoubleTy(*TheContext),"booltmp");

        default:
            return LogErrorV("invlid binary operator");    
    }
}



llvm::Value *CallExprAST::codegen() {
    //Lookup the name in the global module table
    llvm::Function *CalleeF = TheModule->getFunction(Callee);
    if(!CalleeF)
        return LogErrorV("Unknown function referenced");
    
    //If argument mismatch error
    if(CalleeF->arg_size() != Args.size())
        return LogErrorV("Incorrect # arguments passed");

    std::vector<llvm::Value *> ArgsV;
    for( unsigned i=0,e=Args.size();i!=e;i++){
        ArgsV.push_back(Args[i]->codegen());
        if(!ArgsV.back())
            return nullptr;
    }

    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
    
}

llvm::Function *PrototypeAST::codegen(){
    //Make the function type: double(double,double) etc.
    std::vector<llvm::Type*> Doubles (Args.size(), llvm::Type::getDoubleTy(*TheContext));

    llvm::FunctionType *FT = llvm::FunctionType::get(llvm::Type::getDoubleTy(*TheContext),Doubles, false);

    llvm::Function *F = llvm::Function::Create(FT,llvm::Function::ExternalLinkage, Name,TheModule.get());

    //set the names for all the arguments
    unsigned Idx = 0;
    for(auto &Arg : F->args())
        Arg.setName(Args[Idx++]);
    
    return F;
}


llvm::Function *FunctionAST::codegen(){
    // First, check for an existing function from a previous 'extern' declaration.

    llvm::Function *TheFunction = TheModule->getFunction(Proto->getName());
    if(!TheFunction)
        TheFunction = Proto->codegen();
    
    if(!TheFunction)
        return nullptr;
    
    if(!TheFunction->empty())
        return (llvm::Function*)LogErrorV("Function cannot be redefined.");
    
    //Create a new basic block
    llvm::BasicBlock *BB = llvm::BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);

    // Record the function arguments in the NamedValues map.
    NamedValues.clear();
    for (auto &Arg : TheFunction->args())
        NamedValues[ std::string(Arg.getName()) ] = &Arg;
    
    if(llvm::Value *RetVal = Body->codegen()){
        //Finish off the function.
        Builder->CreateRet(RetVal);

        //Validate the generated code, checking for consistency.
        llvm::verifyFunction(*TheFunction);

        // Optimize the function.
        TheFPM->run(*TheFunction);

        return TheFunction;
    }

    //Error reading the body, remove function
    TheFunction->eraseFromParent();
    return nullptr;

}

//===----------------------------------------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------------------------------------===//

// static void InitializeModule() {
//   // Open a new context and module.
//   TheContext = std::make_unique<llvm::LLVMContext>();
//   TheModule = std::make_unique<llvm::Module>("my cool jit", *TheContext);

//   // Create a new builder for the module.
//   Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);
// }


//Pass Manager for Optimization
static void InitializeModuleAndPassManager() {
  // Open a new context and module.
  TheContext = std::make_unique<llvm::LLVMContext>();
  TheModule = std::make_unique<llvm::Module>("my cool jit", *TheContext);
  //TheModule->setDataLayout(TheJIT->getDataLayout());

  // Create a new builder for the module.
  Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);

  // Create a new pass manager attached to it.
  TheFPM = std::make_unique<llvm::legacy::FunctionPassManager>(TheModule.get());

  // Do simple "peephole" optimizations and bit-twiddling optzns.
  TheFPM->add(llvm::createInstructionCombiningPass());
  // Reassociate expressions.
  TheFPM->add(llvm::createReassociatePass());
  // Eliminate Common SubExpressions.
  TheFPM->add(llvm::createGVNPass());
  // Simplify the control flow graph (deleting unreachable blocks, etc).
  TheFPM->add(llvm::createCFGSimplificationPass());

  TheFPM->doInitialization();
}

static void HandleDefinition() {
  if (auto FnAST = ParseDefinition()) {
    if (auto *FnIR = FnAST->codegen()) {
      fprintf(stderr, "Read function definition:\n");
      FnIR->print(llvm::errs());
      fprintf(stderr, "\n");
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    if (auto *FnIR = ProtoAST->codegen()) {
      fprintf(stderr, "Read extern:\n");
      FnIR->print(llvm::errs());
      fprintf(stderr, "\n");
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (auto FnAST = ParseTopLevelExpr()) {
    if (auto *FnIR = FnAST->codegen()) {
      fprintf(stderr, "Read top-level expression:\n");
      FnIR->print(llvm::errs());
      fprintf(stderr, "\n");

      // Remove the anonymous expression.
      FnIR->eraseFromParent();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

/// top ::= definition | external | expression | ';'
static void MainLoop() {
  while (true) {
    fprintf(stderr, "ready> ");
    switch (CurTok) {
    case tok_eof:
      return;
    case ';': // ignore top-level semicolons.
      getNextToken();
      break;
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}


int main() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    
    
    fprintf(stderr, "ready>");
    getNextToken();

    //InitializeModule();
    InitializeModuleAndPassManager();

    //run the main "interpreter loop" now.
    MainLoop();

    // Print out all of the generated code.
    TheModule->print(llvm::errs(), nullptr);
    
    return 0;

}