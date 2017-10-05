//===- llvm-objcopy.cpp -----------------------------------------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "llvm-objcopy.h"
#include "Object.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/ToolOutputFile.h"

#include <memory>
#include <string>
#include <system_error>

using namespace llvm;
using namespace object;
using namespace ELF;

// The name this program was invoked as.
static StringRef ToolName;

namespace llvm {

LLVM_ATTRIBUTE_NORETURN void error(Twine Message) {
  errs() << ToolName << ": " << Message << ".\n";
  errs().flush();
  exit(1);
}

LLVM_ATTRIBUTE_NORETURN void reportError(StringRef File, std::error_code EC) {
  assert(EC);
  errs() << ToolName << ": '" << File << "': " << EC.message() << ".\n";
  exit(1);
}

LLVM_ATTRIBUTE_NORETURN void reportError(StringRef File, llvm::Error E) {
  assert(E);
  std::string Buf;
  raw_string_ostream OS(Buf);
  logAllUnhandledErrors(std::move(E), OS, "");
  OS.flush();
  errs() << ToolName << ": '" << File << "': " << Buf;
  exit(1);
}
}

cl::opt<std::string> InputFilename(cl::Positional, cl::desc("<input>"));
cl::opt<std::string> OutputFilename(cl::Positional, cl::desc("<output>"),
                                    cl::init("-"));
cl::opt<std::string>
    OutputFormat("O", cl::desc("set output format to one of the following:"
                               "\n\tbinary"));

void CopyBinary(const ELFObjectFile<ELF64LE> &ObjFile) {
  std::unique_ptr<FileOutputBuffer> Buffer;
  std::unique_ptr<Object<ELF64LE>> Obj;
  if (!OutputFormat.empty() && OutputFormat != "binary")
    error("invalid output format '" + OutputFormat + "'");

  if (!OutputFormat.empty() && OutputFormat == "binary")
    Obj = llvm::make_unique<BinaryObject<ELF64LE>>(ObjFile);
  else
    Obj = llvm::make_unique<ELFObject<ELF64LE>>(ObjFile);
  Obj->finalize();
  ErrorOr<std::unique_ptr<FileOutputBuffer>> BufferOrErr =
      FileOutputBuffer::create(OutputFilename, Obj->totalSize(),
                               FileOutputBuffer::F_executable);
  if (BufferOrErr.getError())
    error("failed to open " + OutputFilename);
  else
    Buffer = std::move(*BufferOrErr);
  std::error_code EC;
  if (EC)
    report_fatal_error(EC.message());
  Obj->write(*Buffer);
  if (auto EC = Buffer->commit())
    reportError(OutputFilename, EC);
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y; // Call llvm_shutdown() on exit.
  cl::ParseCommandLineOptions(argc, argv, "llvm objcopy utility\n");
  ToolName = argv[0];
  if (InputFilename.empty()) {
    cl::PrintHelpMessage();
    return 2;
  }
  Expected<OwningBinary<Binary>> BinaryOrErr = createBinary(InputFilename);
  if (!BinaryOrErr)
    reportError(InputFilename, BinaryOrErr.takeError());
  Binary &Binary = *BinaryOrErr.get().getBinary();
  if (ELFObjectFile<ELF64LE> *o = dyn_cast<ELFObjectFile<ELF64LE>>(&Binary)) {
    CopyBinary(*o);
    return 0;
  }
  reportError(InputFilename, object_error::invalid_file_type);
}
