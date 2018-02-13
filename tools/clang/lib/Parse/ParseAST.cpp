//===--- ParseAST.cpp - Provide the clang::ParseAST method ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the clang::ParseAST method.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/ParseAST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/Stmt.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/CodeCompleteConsumer.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaConsumer.h"
#include "../Sema/TreeTransform.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include <cstdio>
#include <memory>

using namespace clang;

namespace {

/// Resets LLVM's pretty stack state so that stack traces are printed correctly
/// when there are nested CrashRecoveryContexts and the inner one recovers from
/// a crash.
class ResetStackCleanup
    : public llvm::CrashRecoveryContextCleanupBase<ResetStackCleanup,
                                                   const void> {
public:
  ResetStackCleanup(llvm::CrashRecoveryContext *Context, const void *Top)
      : llvm::CrashRecoveryContextCleanupBase<ResetStackCleanup, const void>(
            Context, Top) {}
  void recoverResources() override {
    llvm::RestorePrettyStackState(resource);
  }
};

/// If a crash happens while the parser is active, an entry is printed for it.
class PrettyStackTraceParserEntry : public llvm::PrettyStackTraceEntry {
  const Parser &P;
public:
  PrettyStackTraceParserEntry(const Parser &p) : P(p) {}
  void print(raw_ostream &OS) const override;
};

/// If a crash happens while the parser is active, print out a line indicating
/// what the current token is.
void PrettyStackTraceParserEntry::print(raw_ostream &OS) const {
  const Token &Tok = P.getCurToken();
  if (Tok.is(tok::eof)) {
    OS << "<eof> parser at end of file\n";
    return;
  }

  if (Tok.getLocation().isInvalid()) {
    OS << "<unknown> parser at unknown location\n";
    return;
  }

  const Preprocessor &PP = P.getPreprocessor();
  Tok.getLocation().print(OS, PP.getSourceManager());
  if (Tok.isAnnotation()) {
    OS << ": at annotation token\n";
  } else {
    // Do the equivalent of PP.getSpelling(Tok) except for the parts that would
    // allocate memory.
    bool Invalid = false;
    const SourceManager &SM = P.getPreprocessor().getSourceManager();
    unsigned Length = Tok.getLength();
    const char *Spelling = SM.getCharacterData(Tok.getLocation(), &Invalid);
    if (Invalid) {
      OS << ": unknown current parser token\n";
      return;
    }
    OS << ": current parser token '" << StringRef(Spelling, Length) << "'\n";
  }
}

class SemaSubtreeRebuild : public TreeTransform<SemaSubtreeRebuild> {
  typedef TreeTransform<SemaSubtreeRebuild> Inherited;
public:
  SemaSubtreeRebuild(Sema &S) : Inherited(S) {}
  bool AlwaysRebuild() { return true; }
};

/// Apply fixes to expects/ensures attributes after parsing a top level decl
class CXXContracts_AttrFixes : public DeclVisitor<CXXContracts_AttrFixes> {
  typedef DeclVisitor<CXXContracts_AttrFixes> Inherited;
  Sema &S;
public:
  CXXContracts_AttrFixes(Sema &S) : S(S) {}

  void FixFunctionAttrs(FunctionDecl *FD) {
    if (!FD->hasBody() || !(FD->hasAttr<ExpectsAttr>()
                            || FD->hasAttr<EnsuresAttr>()))
      return;

    VarDecl *________ret________ = FD->GetInternalReturnVarDecl();
    // changes ________ret_______ type to that of the function (may have been deduced)
    ________ret________->setType(FD->getReturnType());

    Sema::ContextRAII SC(S, FD);
    ExprResult ER;
    // contextually convert to bool 'expects' conditions
    for (auto Attr : FD->specific_attrs<ExpectsAttr>()) {
      if ((ER = S.PerformContextuallyConvertToBool(Attr->getCond())).isUsable())
        Attr->setCond(ER.get());
    }
    // rebuild 'ensures' expr, as the type of ________ret________ has changed
    for (auto Attr : FD->specific_attrs<EnsuresAttr>()) {
      if ((ER = S.PerformContextuallyConvertToBool(
                    SemaSubtreeRebuild(S).TransformExpr(Attr->getCond()).get())
          ).isUsable())
        Attr->setCond(ER.get());
    }
  }

  void Visit(Decl *D) {
    if (DeclContext *DC = dyn_cast<DeclContext>(D))
      for (Decl *i : DC->decls())
        Inherited::Visit(i);
    Inherited::Visit(D);
  }
  void VisitFunctionDecl(FunctionDecl *FD) { FixFunctionAttrs(FD); }
  void VisitCXXMethodDecl(CXXMethodDecl *MD) { FixFunctionAttrs(MD); }
};
}  // namespace

static bool HandleTopLevelDecl(ASTConsumer *Consumer, Sema &S, DeclGroupRef D) {
  CXXContracts_AttrFixes AF(S);
  for (Decl *TLD : D)
    AF.Visit(TLD);
  return Consumer->HandleTopLevelDecl(D);
}

static FunctionDecl *makeFunctionDecl(ASTContext &Context, DeclContext *DC,
                                      QualType ResultTy, StringRef Name,
                                      ArrayRef<QualType> Args,
                                      FunctionProtoType::ExtProtoInfo &EPI) {
  auto FD = FunctionDecl::Create(Context, DC, SourceLocation(),
                                 SourceLocation(), &Context.Idents.get(Name),
                                 Context.getFunctionType(ResultTy, Args, EPI),
                                 nullptr, SC_Extern);
  FD->setImplicit();
  return FD;
}

/// CXXContracts_InjectDecls - Inject declarations required for C++
/// contract support (D0542R2)
static void CXXContracts_InjectDecls(ASTContext &Context) {
  // extern C
  auto LSD = LinkageSpecDecl::Create(Context, Context.getTranslationUnitDecl(),
                                     SourceLocation(), SourceLocation(),
                                     LinkageSpecDecl::lang_c, true);
  LSD->setImplicit();

  // abort
  FunctionProtoType::ExtProtoInfo EPI_abort;
  EPI_abort.ExceptionSpec.Type = EST_DynamicNone;
  EPI_abort.ExtInfo = EPI_abort.ExtInfo.withNoReturn(true);
  auto FD_abort = makeFunctionDecl(Context, LSD, Context.VoidTy, "abort", {},
                                   EPI_abort);

  LSD->addDecl(FD_abort);
  Context.getTranslationUnitDecl()->addDecl(LSD);
}

//===----------------------------------------------------------------------===//
// Public interface to the file
//===----------------------------------------------------------------------===//

/// ParseAST - Parse the entire file specified, notifying the ASTConsumer as
/// the file is parsed.  This inserts the parsed decls into the translation unit
/// held by Ctx.
///
void clang::ParseAST(Preprocessor &PP, ASTConsumer *Consumer,
                     ASTContext &Ctx, bool PrintStats,
                     TranslationUnitKind TUKind,
                     CodeCompleteConsumer *CompletionConsumer,
                     bool SkipFunctionBodies) {

  std::unique_ptr<Sema> S(
      new Sema(PP, Ctx, *Consumer, TUKind, CompletionConsumer));

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<Sema> CleanupSema(S.get());
  
  ParseAST(*S.get(), PrintStats, SkipFunctionBodies);
}

void clang::ParseAST(Sema &S, bool PrintStats, bool SkipFunctionBodies) {
  // Collect global stats on Decls/Stmts (until we have a module streamer).
  if (PrintStats) {
    Decl::EnableStatistics();
    Stmt::EnableStatistics();
  }

  // Also turn on collection of stats inside of the Sema object.
  bool OldCollectStats = PrintStats;
  std::swap(OldCollectStats, S.CollectStats);

  ASTConsumer *Consumer = &S.getASTConsumer();
  // Inject required declarations for C++ contract support (D0542R2)
  if (S.getASTContext().getLangOpts().CPlusPlus
       && S.getASTContext().getLangOpts().BuildLevel > 0)
    CXXContracts_InjectDecls(S.getASTContext());

  std::unique_ptr<Parser> ParseOP(
      new Parser(S.getPreprocessor(), S, SkipFunctionBodies));
  Parser &P = *ParseOP.get();

  llvm::CrashRecoveryContextCleanupRegistrar<const void, ResetStackCleanup>
      CleanupPrettyStack(llvm::SavePrettyStackState());
  PrettyStackTraceParserEntry CrashInfo(P);

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<Parser>
    CleanupParser(ParseOP.get());

  S.getPreprocessor().EnterMainSourceFile();
  P.Initialize();

  Parser::DeclGroupPtrTy ADecl;
  ExternalASTSource *External = S.getASTContext().getExternalSource();
  if (External)
    External->StartTranslationUnit(Consumer);

  for (bool AtEOF = P.ParseFirstTopLevelDecl(ADecl); !AtEOF;
       AtEOF = P.ParseTopLevelDecl(ADecl)) {
    // If we got a null return and something *was* parsed, ignore it.  This
    // is due to a top-level semicolon, an action override, or a parse error
    // skipping something.
    if (ADecl && !HandleTopLevelDecl(Consumer, S, ADecl.get()))
      return;
  }

  // Process any TopLevelDecls generated by #pragma weak.
  for (Decl *D : S.WeakTopLevelDecls())
    HandleTopLevelDecl(Consumer, S, DeclGroupRef(D));
  
  Consumer->HandleTranslationUnit(S.getASTContext());

  std::swap(OldCollectStats, S.CollectStats);
  if (PrintStats) {
    llvm::errs() << "\nSTATISTICS:\n";
    P.getActions().PrintStats();
    S.getASTContext().PrintStats();
    Decl::PrintStats();
    Stmt::PrintStats();
    Consumer->PrintStats();
  }
}
