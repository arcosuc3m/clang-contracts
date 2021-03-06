# Experimental Clang support for contract based programming in C++ (P0542R5 TS)
[![HitCount](http://hits.dwyl.io/arcosuc3m/clang-contracts.svg)](http://hits.dwyl.io/arcosuc3m/clang-contracts) <a href="http://fragata.arcos.inf.uc3m.es/#"><img src="http://fragata.arcos.inf.uc3m.es/dist/assets/site-logo.3f5bcf90b56ade7be40ffa8cca8b2056.svg" height="20" alt="Compiler Explorer (godbolt) site that can be used for testing (regularly updated with master branch)"/></a>
## Low Level Virtual Machine (LLVM)
This directory and its subdirectories contain source code for LLVM, as it
looked on Thu Oct 5 2017.  This is the SVN development revision 314972.
The rest of this section contains the original README.txt of the LLVM project.
> LLVM is open source software. You may freely distribute it under the terms of
> the license agreement found in LICENSE.txt.
> 
> Please see the documentation provided in docs/ for further
> assistance with LLVM, and in particular docs/GettingStarted.rst for getting
> started with LLVM and docs/README.txt for an overview of LLVM's
> documentation setup.
> 
> If you are writing a package for LLVM, see docs/Packaging.rst for our
> suggestions.

## Clang C/C++/ObjectiveC frontend
This repository also contains a clone of the Clang repository (in its SVN revision
314964, dated Thu Oct 5 2017) under the `tools/clang` directory.

The Clang code included in this repository includes a prototype implementation of
the P0542R5 techinical specification, for [Support for contract based programming
in C++](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0542r5.html), recently
approved by the ISO C++ comitee to be part of C++20.  To see a summary of changes since this project was forked:
```
$ git diff --stat master{^{/SVN},}
```

As of Mon Jul 9 2018, this is a work in progress, however it is quite complete (see
below for a list of missing features).

If you find a bug/unexpected behaviour, please file a bug in the project page. You can
also try to fix it and post a patch or pull request.

## Build instructions of LLVM + Clang
Provided that you have the required dependencies already installed in your machine, just
run the following commands:
``` 
$ git clone https://github.com/arcosuc3m/clang-contracts/
$ mkdir -p clang-contracts/build/ && cd clang-contracts/build/
$ cmake -G "Unix Makefiles" -DLLVM_USE_LINKER=gold -DBUILD_SHARED_LIBS=ON -DLLVM_USE_SPLIT_DWARF=ON
  -DLLVM_OPTIMIZED_TABLEGEN=ON ../
$ make -j8
```

For additional build instruction see the [Clang project page](http://clang.llvm.org/) and
the [Getting Started](http://clang.llvm.org/get_started.html) guide.

## Command line options
Four new options were added to the Clang driver: `-build-level=`, `-axiom-mode=`, `-contract-violation-handler=` and `-fcontinue-after-violation`, e.g.
```
$ clang++ -std=c++14 [-build-level=(off|default|audit)] [-axiom-mode=(off|on)]
  [-contract-violation-handler=my_handler]
  [-fcontinue-after-violation] ...
```

Because the `contract` header is contained in the tools/clang/lib/Headers/ directory, you will probably need to add the `-idirafter path-to-clang-contracts-repo/tools/clang/lib/Headers/` option.

The `-build-level=` option allows to specify the build level of the translation (P0542R5
Proposed Wording, Section 10.6.11.12). If unspecified, it defaults to default.

The `-axiom-mode=` option allows changing the axiom mode of the translation. If set, the compiler assumes any [[expects]], [[ensures]] or [[assert]] tagged as axiom as if `__builtin_assume()` was there. If unspecified, it defaults to on.

The other two options allow specifying a custom violation handler and the violation
continuation mode, as per Section 10.6.11.16 and 10.6.11.18 of the current wording.

## MWE with C++ contracts
Remember that contract attribute spelling is quite different from that of CXX11, e.g.
`[[attribute contract-level-opt identifier-opt: conditional-expression]]`.

As of May 22 2018, templates are completely supported. Try this MWE that uses templates!
```
template <typename T>
T func(T a, T b)
[[expects: a > b]] [[ensures r: r > 0]] {
  return a + b;
}

int main(int argc, char *argv[]) {
  return func<int>(argc, 0);
}
```

## Known problems
See the [Issues](https://github.com/arcosuc3m/clang-contracts/issues) page. If you have found
a new (undocumented) issue, please add it to the tracker.

## TO DO
As of Jul 9 2018, the implementation is quite complete only missing a few features, namely:
- P0542R5 Section 2.3, Contracts repetition: currently, expects/ensures attributes are
merged into the most recent redeclaration.
- Late parsing of C++11 attributes is required to accomodate the modification of Section 12.2
[class.mem], p.6.  See also issue #3.
> Within the class member-specification, the class is regarded as complete within function
> bodies, default arguments, noexcept-specifiers, and default member initializers, and
> contract conditions (10.6.11 [dcl.attr.contracts]).
- Handle inheritance of [[expects]] and [[ensures]] attributes as per the P0542R5 TS.

Also, these features may be added to improve the quality of the implementation (not required 
by the TS):
- Static evaluation of contracts (if possible).

## Authors
This implementation of the P0542R5 TS was authored and maintained by Javier López-Gómez
(jalopezg [at] inf.uc3m.es), with notable contributions from José Cabrero Holgueras
(jcabrero [at] inf.uc3m.es).
