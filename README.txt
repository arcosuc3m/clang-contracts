Low Level Virtual Machine (LLVM)
================================

This directory and its subdirectories contain source code for LLVM,
a toolkit for the construction of highly optimized compilers,
optimizers, and runtime environments.

LLVM is open source software. You may freely distribute it under the terms of
the license agreement found in LICENSE.txt.

Please see the documentation provided in docs/ for further
assistance with LLVM, and in particular docs/GettingStarted.rst for getting
started with LLVM and docs/README.txt for an overview of LLVM's
documentation setup.

If you are writing a package for LLVM, see docs/Packaging.rst for our
suggestions.


Build instructions
==================

$ git clone https://gitlab.arcos.inf.uc3m.es:8380/jalopezg/llvm-clang.git
$ mkdir -p llvm-clang/build/ && cd llvm-clang/build/
$ cmake -G "Unix Makefiles" -DLLVM_USE_LINKER=gold -DBUILD_SHARED_LIBS=ON -DLLVM_USE_SPLIT_DWARF=ON -DLLVM_OPTIMIZED_TABLEGEN=ON ../
$ make -j8

Usage
=====

$ clang++ -std=c++11 -build-level=audit ...

Known problems
==============
- expects/ensures not supported in static member functions, constructors or
  destructors

To do
=====
- Handle contract inheritance for redeclarations as specified in D0542R2 (2017-11-07)
- Class inheritance?, see D0542R2 specification
- Call user-defined violation handler, continuation mode?
