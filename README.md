# SGScript v1.0.4

## Usage

- MinGW users:
    * compile the makefile (add mode=release to get the release build)
    * link with -lsgscript from the bin/ directory (-Lbin)
- VC10+ users: project file is in build/vc10/SGScript
- XCode users: project file is in build/xcode
- include ```src/sgscript.h``` in your project and link with ```libsgscript.a```

## Sample Code and Documentation

Look in ```examples/```, ```tests/``` and ```docs/``` directories.

More sample code can be found in documentation: http://www.sgscript.org/docs/sgscript.docs/code-samples-sgscript

As you'll probably see, it's not that different from many other languages. There'll be no specification until the language survives a few iterations. After that, we'll see if it's necessary.

## Features

- a C-like syntax
- the usual stuff (while/do-while/for/foreach, expressions, variables etc.)
- highly optimized, register-based virtual machine
- mixed memory management (ref.counting + GC)
- extensive native debugging features
- **Interactive debug inspector add-on**
- **Function/instruction execution time and memory usage profiler add-on**
- 9 data types (with lots of space for extensions):
    * null, bool, int, real, string, function, C function, object, pointer
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
- original XCode project contributed by Elviss Strazdiņš

