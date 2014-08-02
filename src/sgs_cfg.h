
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


/* SGScript error messages */
#define SGS_ERRMSG_CALLSTACKLIMIT "Max. call stack size reached"


/* bytes reserved initially for token storage
	used to prevent multiple reallocations in the beginning */
#define SGS_TOKENLIST_PREALLOC 1024

/* maximum length for a string to be included in the string table */
#define SGS_STRINGTABLE_MAXLEN 0x7fffffff

/* max. length of the call stack */
#define SGS_MAX_CALL_STACK_SIZE 256

/* the size of the stack buffer for temporary vsprintf'ed text
	if predicted text length is longer than that, the memory
	is allocated using the context allocator */
#define SGS_OUTPUT_STACKBUF_SIZE 128

/* postfix for function names in C functions
	EXTRABYTES should be set to the length of EXTRASTRING */
#define SGS_MSG_EXTRASTRING "(): "
#define SGS_MSG_EXTRABYTES 4

/* object pool settings
	- size of object pool (max. number of items)
	- max. appended memory size for pool usage */
#define SGS_OBJPOOL_SIZE 64
#define SGS_OBJPOOL_MAX_APPMEM 256

/* inclusion system settings:
	- name of entry point function to look for
	- the default include path */
#define SGS_LIB_ENTRY_POINT "sgscript_main"
#define SGS_INCLUDE_PATH "|/?;|/?" SGS_MODULE_EXT ";|/?.sgc;|/?.sgs;?;?" SGS_MODULE_EXT ";?.sgc;?.sgs"


/* per-platform switches */
#ifdef SGS_PF_ANDROID
#define SGS_INVALID_LCONV /* disables os_locale_format */
#endif


#endif /* SGS_CFG_H_INCLUDED */
