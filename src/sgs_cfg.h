
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

/* stage output dumps */
#ifndef SGS_DEBUG_DATA
#  define SGS_DEBUG_DATA 0
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

/* debugging of performance issues (currently logs vm_convert only) */
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

/* checks for having more memory allocated with..
	..the context memory functions than freed
	very fast, runs only in sgs_DestroyEngine */
#ifndef SGS_DEBUG_LEAKS
#  define SGS_DEBUG_LEAKS 1
#endif


/* profiling */
#ifndef SGS_DUMP_BYTECODE
#  define SGS_DUMP_BYTECODE 0
#endif


/* bytes reserved initially for token storage
	used to prevent multiple reallocations in the beginning */
#define SGS_TOKENLIST_PREALLOC 1024

/* maximum length for a string to be included in the string table */
#define SGS_STRINGTABLE_MAXLEN 16

/* max. length of the call stack */
#define SGS_MAX_CALL_STACK_SIZE 1024

/* the size of the stack buffer for temporary vsprintf'ed text
	if predicted text length is longer than that, the memory
	is allocated using the context allocator */
#define SGS_OUTPUT_STACKBUF_SIZE 128

/* size of the last property access cache for dict
	bigger numbers increase average access time
	smaller numbers decrease cache hit rate */
//#define SGS_DICT_CACHE_SIZE 8

/* postfix for function names in C functions
	EXTRABYTES should be set to the length of EXTRASTRING */
#define SGS_PRINTF_EXTRASTRING "(): "
#define SGS_PRINTF_EXTRABYTES 4


#endif /* SGS_CFG_H_INCLUDED */
