
--- SGScript v0.8 ---

usage:
- grab the source from src/ directory...
- compile all .c files...
	* by using makefiles / build projects or just..
	..by building them and archiving the generated object files
	! project isn't ready to be used as a DLL/SO yet
	* everything was tested on 32-bit Windows and both 32- and 64-bit Ubuntu
		* fresh commits aren't tested as thoroughly
- include src/sgscript.h in your project

sample code and documentation ... is in tests/ and docs/ directories
	as you'll probably see, it's not that different from many other languages.
	there'll be no specification until the language survives a few iterations.
	after that, we'll see if it's necessary.

---

intro:
I'm not quite sure what to expect from this project. Worldwide acceptance or ..
..(more likely) slow death from lack of recognition. Either way, after       ..
..putting some effort into it, I've decided to pull it out of shadows. It's  ..
..been fun writing this one, a great learning experience. And I've already   ..
..put it to some use. Not much, so far. It's not quite ready for anything but..
..the most simple applications. The goal is to make it game-friendly. Let's  ..
..see where that goal takes me and the project...

features:
- a C-like syntax
- the usual stuff (while/do-while/for/foreach, expressions, variables etc.)
- speed! (register-based virtual machine)
- mixed memory management (ref.counting + GC)
- extensive native debugging features
- 8 data types (with some space for extensions):
	null, bool, int, real, string, function, C function, object
- tests & benchmark
	* testing framework is in ext/sgstest.c => bin/sgstest ("make test" to run)
	* execute "bin/sgsvm bench/bench.sgs" to run the benchmark
- object-oriented constructs (dict, class, closure, overloadable operators)

development:
- I work on it when I feel like it
- more support = bigger priority
! if you think you've found a bug, pop me an email to snake5creator [at] GMail
	* don't forget to include a test sample, as small as possible!

future:
- optimizations (the ones based on assumptions), fully featured API
- got a suggestion? write some sample code (in the form of a test) and send it

credits:
- this piece of software is created by Arvīds Kokins (snake5)
	* I can be reached at snake5creator [at] GMail
- inspired by C, PHP, Lua, JavaScript, Squirrel and AngelScript

change log:
0.8 (2013/03/29): most of string API, type API, closures, API test framework,
	improved debugging, fixed: boolean logic, div/0 error and other things
0.7 (2013/01/28): object orientation (w/ operator overloading), do-while,
	optimizations, classes, eval, foreach, fixed comparisons
0.6 - skipped
0.5 (2013/01/06): cleanup, lambdas, literals, "dict" container
0.4 (2012/12/26): the initial release

