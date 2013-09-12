
--- SGScript v0.8.5 ---

usage:
- MinGW users:
	* compile the makefile (add mode=release to get the release build)
	* link against libsgscript.a from the lib/ directory (-lsgscript)
- VC10/11 users: project file is in build/vc10/SGScript
- Code::Blocks users: project file is in build/codeblocks
- XCode users: project file is in build/xcode
- include src/sgscript.h in your project and link with libsgscript.a

sample code and documentation ... is in tests/ and docs/ directories
	as you'll probably see, it's not that different from many other languages.
	there'll be no specification until the language survives a few iterations.
	after that, we'll see if it's necessary.

---

features:
- a C-like syntax
- the usual stuff (while/do-while/for/foreach, expressions, variables etc.)
- highly optimized, register-based virtual machine
- mixed memory management (ref.counting + GC)
- extensive native debugging features
- ** interactive debug inspector add-on **
- 8 data types (with lots of space for extensions):
	null, bool, int, real, string, function, C function, object
- tests
	* testing framework is in ext/sgstest.c => bin/sgstest ("make test" to run)
- object-oriented constructs (dict, class, closure, overloadable operators)

development:
- urge-oriented. I work on what I want to, at the moment.
! if you think you've found a bug, pop me an email to snake5creator [at] GMail
	* don't forget to include a test sample, as small as possible!

future:
- more extensions, fully featured API
- got a suggestion? write some sample code (in the form of a test) and send it

credits:
- developer: Arvīds Kokins (snake5)
	* I can be reached at snake5creator [at] GMail
- XCode project contributed by Elviss Strazdiņš
- inspired by C, PHP, Lua, JavaScript, Squirrel and AngelScript

change log:
0.8.5 (2013/05/01): sorted MANY things out (lots of fixes and API changes)
	utf8<->utf32, big math/type lib. changes, debug inspector, json addon DLL,
	upgraded the test framework, created the monkey test, doc.gen. from MD
0.8.2 (2013/04/15): implicit "this", bytecode serialization, upgraded AST code,
	DLL loading system (Windows-only ATM), core I/O API, variable cloning,
	extended array API that is made to be sequencing-friendly, int/int=>real
0.8.1 (2013/04/04): important stability and main API improvements
	the engine is finally tested to be production-code-ready
0.8 (2013/03/29): most of string API, type API, closures, API test framework,
	improved debugging, fixed: boolean logic, div/0 error and other things
0.7 (2013/01/28): object orientation (w/ operator overloading), do-while,
	optimizations, classes, eval, foreach, fixed comparisons
0.6 - skipped
0.5 (2013/01/06): cleanup, lambdas, literals, "dict" container
0.4 (2012/12/26): the initial release

