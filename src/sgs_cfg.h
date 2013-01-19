
#ifndef SGS_CFG_H_INCLUDED
#define SGS_CFG_H_INCLUDED

/* global debug switch */
#ifndef SGS_DEBUG
#  ifdef _DEBUG
#    define SGS_DEBUG 1
#  else
#    define SGS_DEBUG 0
#  endif
#endif

/* buffer overflow/underflow tests (doesn't work with a custom allocation function!) */
#ifndef SGS_DEBUG_MEMORY
#  define SGS_DEBUG_MEMORY 1
#endif

/* leak dump after destructions (works properly with one context only!) */
#ifndef SGS_DEBUG_CHECK_LEAKS
#  define SGS_DEBUG_CHECK_LEAKS 1
#endif

/* stage output dumps */
#ifndef SGS_DEBUG_DATA
#  define SGS_DEBUG_DATA 1
#endif

/* function entry/exit tagging */
#ifndef SGS_DEBUG_FLOW
#  define SGS_DEBUG_FLOW 0
#endif

/* instruction logging */
#ifndef SGS_DEBUG_INSTR
#  define SGS_DEBUG_INSTR 0
#endif

/* stack dump before execution of each instruction */
#ifndef SGS_DEBUG_STATE
#  define SGS_DEBUG_STATE 0
#endif

#ifndef SGS_DEBUG_PERF
#  define SGS_DEBUG_PERF 0
#endif

/* break-if */
#ifndef SGS_DEBUG_VALIDATE
#  define SGS_DEBUG_VALIDATE 1
#endif

/* extensive buffer overflow tests */
#ifndef SGS_DEBUG_EXTRA
#  define SGS_DEBUG_EXTRA 0
#endif


/* profiling */
#ifndef SGS_PROFILE_BYTECODE
#  define SGS_PROFILE_BYTECODE 0
#endif


typedef unsigned short LineNum;


/*	Tokenizer settings	*/
#ifndef SGS_TOKENLIST_PREALLOC
#  define SGS_TOKENLIST_PREALLOC	1024
#endif

/*	Function tree settings	*/

/*	Interpreter settings	*/


#endif /* SGS_CFG_H_INCLUDED */
