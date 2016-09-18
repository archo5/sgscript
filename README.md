# SGScript v1.4.0

## Usage

- MinGW/GNU Make/GCC/Clang users:
    * compile the makefile (add ```mode=release``` to get the release build)
    * include ```src/sgscript.h``` and link with -lsgscript from the bin/ directory (-Lbin)
    * to get a static library, use ```static=1``` and link to ```lib/libsgscript.a``` (-Llib -lsgscript)
- VC10+ users: project file is in build/vc10/SGScript
- XCode users: project file is in build/xcode
- Android NDK users: include jni/Android.mk into your makefile

## Sample Code and Documentation

Look in ```examples/```, ```tests/``` and ```docs/``` directories.

To build local HTML5 documentation, use "make docs".

More sample code can be found in documentation: http://www.sgscript.org/docs/sgscript.docs/code-samples-sgscript

## Features

- A C-like syntax
- The usual stuff (while/do-while/for/foreach, expressions, variables etc.)
- Highly optimized, register-based virtual machine
- Mixed memory management (ref.counting + GC)
- Extensive native debugging features
- **Coroutines, threads, advanced sync features**
- **Interactive debug inspector add-on**
- **Function/instruction execution time and memory usage profiler add-on**
- 10 data types (with lots of space for extensions):
    * null, bool, int, real, string, function, C function, object, pointer, thread
- Tests:
    * testing framework is in ext/sgstest.c => bin/sgstest ("make test" to run)
    * API testing framework is in ext/sgsapitest.c => bin/sgsapitest ("make apitest" to run)
    * C++/BC testing framework is in ext/sgscppbctest.cpp/h => bin/sgscppbctest ("make cppbctest" or "make cppbctest11" to run)
- Object-oriented constructs (dict, class, closure, "compatible call", overloadable operators)

## Bugs

- Development branch status: [![Build Status](https://travis-ci.org/snake5/sgscript.svg?branch=apidev)](https://travis-ci.org/snake5/sgscript)

If you think you've found a bug, please create a new issue on GitHub.

Don't forget to include a test sample, as small as possible!

## Future plans

- full state serialization
- got a suggestion? write some sample code (in the form of a test) and send it here

## Community

- [SGScript on Discord](https://discord.gg/QysXUNq)

## Credits

- developer: Arvīds Kokins (snake5)
    * I can be reached at https://twitter.com/snake5creator and snake5creator [at] GMail

