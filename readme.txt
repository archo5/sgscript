
--- SGScript v0.4 ---

.the initial release

usage:
- grab the source from root directory...
- compile all .c files...
	* with main.c if you want to launch the test framework
	* without main.c if you want to build a static library
	! project isn't ready to be used as a DLL/SO yet
	* everything was tested on 32-bit Windows and both 32- and 64-bit Ubuntu
		* the new register-based VM code isn't tested as thoroughly
- include sgscript.h in your project

---

intro:
I'm not quite sure what to expect from this project. Worldwide acceptance or..
..(more likely) slow death from lack of recognition. Either way, after..
..putting some effort into it, I've decided to pull it out of shadows. It's..
..been fun writing this one, a great learning experience. And I've already..
..put it to some use. Not much, so far. It's not quite ready for anything but..
..the most simple applications. The goal is to make it game-friendly. Let's..
..see where that goal takes me and the project...

features:
- a C-like syntax
- the usual stuff (while/for, expressions, local/global variables etc.)
- speed! (register-based virtual machine)
- mixed memory management (ref.count + GC)
- extensive debugging features
- 8 data types (with some space for extensions):
	null, bool, int, real, string, function, C function, object
- tests & benchmark
	* testing framework is in main.c;
	* execute "bin/SGScript[.exe] tests/!!mandelbrot.txt" to run the benchmark

development:
- I work on it when I feel like it
- more support = bigger priority
! if you think you've found a bug, pop me an email to snake5creator [at] GMail
	* don't forget to include a test sample, as small as possible!

future:
- tail calls, a bit more OOP, containers, optimizations, fully featured API
- got a suggestion? write some sample code (in the form of a test) and send it

credits:
- this piece of software is created by Arvīds Kokins (snake5)
	* I can be reached at snake5creator [at] GMail
- inspired by C, PHP, Lua, Squirrel and AngelScript

