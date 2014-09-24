# SGScript v0.9.5

## Usage

- MinGW users:
    * compile the makefile (add mode=release to get the release build)
    * link with -lsgscript from the bin/ directory (-Lbin)
- VC10/11 users: project file is in build/vc10/SGScript
- Code::Blocks users: project file is in build/codeblocks
- XCode users: project file is in build/xcode
- include ```src/sgscript.h``` in your project and link with ```libsgscript.a```

## Sample Code and Documentation

Look  in ```tests/``` and ```docs/``` directories. 

As you'll probably see, it's not that different from many other languages. There'll be no specification until the language survives a few iterations. After that, we'll see if it's necessary.

## Features

- a C-like syntax
- the usual stuff (while/do-while/for/foreach, expressions, variables etc.)
- highly optimized, register-based virtual machine
- mixed memory management (ref.counting + GC)
- extensive native debugging features
- **Interactive debug inspector add-on**
- 8 data types (with lots of space for extensions):
    * null, bool, int, real, string, function, C function, object
- tests
    * testing framework is in ext/sgstest.c => bin/sgstest ("make test" to run)
- object-oriented constructs (dict, class, closure, overloadable operators)

## Bugs

If you think you've found a bug, file an issue on the repo.

Don't forget to include a test sample, as small as possible!

## Future 

- more extensions, fully featured API
- got a suggestion? write some sample code (in the form of a test) and send it

## Credits

- developer: Arvīds Kokins (snake5)
    * I can be reached at snake5creator [at] GMail
- XCode project contributed by Elviss Strazdiņš

## Changelog
0.9.5 (2014/05/25, updated 2014/07/11):

    - full Mac OS X support
    - bugfixes: lots of those (regex is finally stable)
    - memory usage profiler
    - map object
    - syntax/VM upgrades (multi-index/property set, compatible call, unescaped strings, error suppression, named anonymous and short functions, var.arg. support)
    - stdlib upgrades (core, utf8, io)
    - xgmath (floatarray, matrix), sockets module upgrades
    - C API upgrades (major refactor for variable pointer support, output system)
    - almost fully documented the whole system, upgraded documentation system
    - cppbc upgraded and fully tested with heavy C++ projects (sgs-box2d) + documented
    - sgs2c experimental script developed to maximum bytecode compatibility
    - code is mostly warning-free on all supported platforms

0.9.0 (2013/11/10):

    - bugfixes: empty subexpr, unary op chaining, expression writes
    - implemented hooks interface
    - created two kinds of profilers
    - major internal refactoring (object interface & C-side handling)
    - major stdlib upgrades (OS, date, I/O, formatting, iterables, system, regex)
    - serialization
    - the meta-global, environment switching
    - unoptimized pseudo-empty statements due to possible side effects (1+funccall("x") etc.)
    - documented stdlib / C API
    - multiple return value capture via expression writes
    - if(cond,true,false) pseudo-function (ternary op substitute)
    - real closures

0.8.5 (2013/05/01): 
    - sorted MANY things out (lots of fixes and API changes)
    - utf8<->utf32, big math/type lib. changes, debug inspector, json addon DLL,
    - upgraded the test framework, created the monkey test, doc.gen. from MD

0.8.2 (2013/04/15): 
    - implicit "this", bytecode serialization, upgraded AST code,
    - DLL loading system (Windows-only ATM), core I/O API, variable cloning,
    - extended array API that is made to be sequencing-friendly, int/int=>real

0.8.1 (2013/04/04): 
    - important stability and main API improvements
    - the engine is finally tested to be production-code-ready

0.8 (2013/03/29): 
    - most of string API, type API, closures, API test framework,
    - improved debugging, fixed: boolean logic, div/0 error and other things

0.7 (2013/01/28): 
    - object orientation (w/ operator overloading), do-while,
    - optimizations, classes, eval, foreach, fixed comparisons

0.6 - skipped

0.5 (2013/01/06): cleanup, lambdas, literals, "dict" container

0.4 (2012/12/26): the initial release

