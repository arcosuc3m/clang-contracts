//===--- RenamingAction.cpp - Clang refactoring library -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Provides an action to rename every symbol at a point.
///
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Refactoring/Rename/RenamingAction.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/FileManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Refactoring/RefactoringAction.h"
#include "clang/Tooling/Refactoring/Rename/USRFinder.h"
#include "clang/Tooling/Refactoring/Rename/USRFindingAction.h"
#include "clang/Tooling/Refactoring/Rename/USRLocFinder.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/STLExtras.h"
#include <string>
#include <vector>

using namespace llvm;

namespace clang {
namespace tooling {

namespace {

class SymbolSelectionRequirement : public SourceRangeSelectionRequirement {
public:
  Expected<const NamedDecl *> evaluate(RefactoringRuleContext &Context) const {
    Expected<SourceRange> Selection =
        SourceRangeSelectionRequirement::evaluate(Context);
    if (!Selection)
      return Selection.takeError();
    const NamedDecl *ND =
        getNamedDeclAt(Context.getASTContext(), Selection->getBegin());
    if (!ND) {
      // FIXME: Use a diagnostic.
      return llvm::make_error<StringError>("no symbol selected",
                                           llvm::inconvertibleErrorCode());
    }
    return getCanonicalSymbolDeclaration(ND);
  }
};

class OccurrenceFinder final : public FindSymbolOccurrencesRefactoringRule {
public:
  OccurrenceFinder(const NamedDecl *ND) : ND(ND) {}

  Expected<SymbolOccurrences>
  findSymbolOccurrences(RefactoringRuleContext &Context) override {
    std::vector<std::string> USRs =
        getUSRsForDeclaration(ND, Context.getASTContext());
    std::string PrevName = ND->getNameAsString();
    return getOccurrencesOfUSRs(
        USRs, PrevName, Context.getASTContext().getTranslationUnitDecl());
  }

private:
  const NamedDecl *ND;
};

class RenameOccurrences final : public SourceChangeRefactoringRule {
public:
  RenameOccurrences(const NamedDecl *ND) : Finder(ND) {}

  Expected<AtomicChanges>
  createSourceReplacements(RefactoringRuleContext &Context) {
    Expected<SymbolOccurrences> Occurrences =
        Finder.findSymbolOccurrences(Context);
    if (!Occurrences)
      return Occurrences.takeError();
    // FIXME: This is a temporary workaround that's needed until the refactoring
    // options are implemented.
    StringRef NewName = "Bar";
    return createRenameReplacements(
        *Occurrences, Context.getASTContext().getSourceManager(), NewName);
  }

private:
  OccurrenceFinder Finder;
};

class LocalRename final : public RefactoringAction {
public:
  StringRef getCommand() const override { return "local-rename"; }

  StringRef getDescription() const override {
    return "Finds and renames symbols in code with no indexer support";
  }

  /// Returns a set of refactoring actions rules that are defined by this
  /// action.
  RefactoringActionRules createActionRules() const override {
    RefactoringActionRules Rules;
    Rules.push_back(createRefactoringActionRule<RenameOccurrences>(
        SymbolSelectionRequirement()));
    return Rules;
  }
};

} // end anonymous namespace

std::unique_ptr<RefactoringAction> createLocalRenameAction() {
  return llvm::make_unique<LocalRename>();
}

Expected<std::vector<AtomicChange>>
createRenameReplacements(const SymbolOccurrences &Occurrences,
                         const SourceManager &SM,
                         ArrayRef<StringRef> NewNameStrings) {
  // FIXME: A true local rename can use just one AtomicChange.
  std::vector<AtomicChange> Changes;
  for (const auto &Occurrence : Occurrences) {
    ArrayRef<SourceRange> Ranges = Occurrence.getNameRanges();
    assert(NewNameStrings.size() == Ranges.size() &&
           "Mismatching number of ranges and name pieces");
    AtomicChange Change(SM, Ranges[0].getBegin());
    for (const auto &Range : llvm::enumerate(Ranges)) {
      auto Error =
          Change.replace(SM, CharSourceRange::getCharRange(Range.value()),
                         NewNameStrings[Range.index()]);
      if (Error)
        return std::move(Error);
    }
    Changes.push_back(std::move(Change));
  }
  return std::move(Changes);
}

/// Takes each atomic change and inserts its replacements into the set of
/// replacements that belong to the appropriate file.
static void convertChangesToFileReplacements(
    ArrayRef<AtomicChange> AtomicChanges,
    std::map<std::string, tooling::Replacements> *FileToReplaces) {
  for (const auto &AtomicChange : AtomicChanges) {
    for (const auto &Replace : AtomicChange.getReplacements()) {
      llvm::Error Err = (*FileToReplaces)[Replace.getFilePath()].add(Replace);
      if (Err) {
        llvm::errs() << "Renaming failed in " << Replace.getFilePath() << "! "
                     << llvm::toString(std::move(Err)) << "\n";
      }
    }
  }
}

class RenamingASTConsumer : public ASTConsumer {
public:
  RenamingASTConsumer(
      const std::vector<std::string> &NewNames,
      const std::vector<std::string> &PrevNames,
      const std::vector<std::vector<std::string>> &USRList,
      std::map<std::string, tooling::Replacements> &FileToReplaces,
      bool PrintLocations)
      : NewNames(NewNames), PrevNames(PrevNames), USRList(USRList),
        FileToReplaces(FileToReplaces), PrintLocations(PrintLocations) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    for (unsigned I = 0; I < NewNames.size(); ++I) {
      // If the previous name was not found, ignore this rename request.
      if (PrevNames[I].empty())
        continue;

      HandleOneRename(Context, NewNames[I], PrevNames[I], USRList[I]);
    }
  }

  void HandleOneRename(ASTContext &Context, const std::string &NewName,
                       const std::string &PrevName,
                       const std::vector<std::string> &USRs) {
    const SourceManager &SourceMgr = Context.getSourceManager();

    SymbolOccurrences Occurrences = tooling::getOccurrencesOfUSRs(
        USRs, PrevName, Context.getTranslationUnitDecl());
    if (PrintLocations) {
      for (const auto &Occurrence : Occurrences) {
        FullSourceLoc FullLoc(Occurrence.getNameRanges()[0].getBegin(),
                              SourceMgr);
        errs() << "clang-rename: renamed at: " << SourceMgr.getFilename(FullLoc)
               << ":" << FullLoc.getSpellingLineNumber() << ":"
               << FullLoc.getSpellingColumnNumber() << "\n";
      }
    }
    // FIXME: Support multi-piece names.
    // FIXME: better error handling (propagate error out).
    StringRef NewNameRef = NewName;
    Expected<std::vector<AtomicChange>> Change =
        createRenameReplacements(Occurrences, SourceMgr, NewNameRef);
    if (!Change) {
      llvm::errs() << "Failed to create renaming replacements for '" << PrevName
                   << "'! " << llvm::toString(Change.takeError()) << "\n";
      return;
    }
    convertChangesToFileReplacements(*Change, &FileToReplaces);
  }

private:
  const std::vector<std::string> &NewNames, &PrevNames;
  const std::vector<std::vector<std::string>> &USRList;
  std::map<std::string, tooling::Replacements> &FileToReplaces;
  bool PrintLocations;
};

// A renamer to rename symbols which are identified by a give USRList to
// new name.
//
// FIXME: Merge with the above RenamingASTConsumer.
class USRSymbolRenamer : public ASTConsumer {
public:
  USRSymbolRenamer(const std::vector<std::string> &NewNames,
                   const std::vector<std::vector<std::string>> &USRList,
                   std::map<std::string, tooling::Replacements> &FileToReplaces)
      : NewNames(NewNames), USRList(USRList), FileToReplaces(FileToReplaces) {
    assert(USRList.size() == NewNames.size());
  }

  void HandleTranslationUnit(ASTContext &Context) override {
    for (unsigned I = 0; I < NewNames.size(); ++I) {
      // FIXME: Apply AtomicChanges directly once the refactoring APIs are
      // ready.
      auto AtomicChanges = tooling::createRenameAtomicChanges(
          USRList[I], NewNames[I], Context.getTranslationUnitDecl());
      convertChangesToFileReplacements(AtomicChanges, &FileToReplaces);
    }
  }

private:
  const std::vector<std::string> &NewNames;
  const std::vector<std::vector<std::string>> &USRList;
  std::map<std::string, tooling::Replacements> &FileToReplaces;
};

std::unique_ptr<ASTConsumer> RenamingAction::newASTConsumer() {
  return llvm::make_unique<RenamingASTConsumer>(NewNames, PrevNames, USRList,
                                                FileToReplaces, PrintLocations);
}

std::unique_ptr<ASTConsumer> QualifiedRenamingAction::newASTConsumer() {
  return llvm::make_unique<USRSymbolRenamer>(NewNames, USRList, FileToReplaces);
}

} // end namespace tooling
} // end namespace clang
