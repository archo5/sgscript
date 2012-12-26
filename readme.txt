
- - SGScript v0.4 - -

.the initial release


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
- if you think you've found a bug, pop me an email to snake5creator [at] GMail
	* don't forget to include a test sample, as small as possible!
