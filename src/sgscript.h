
#ifndef SGSCRIPT_H_INCLUDED
#define SGSCRIPT_H_INCLUDED


#define SGS_VERSION_MAJOR 0
#define SGS_VERSION_MINOR 8
#define SGS_VERSION_INCR  2
#define SGS_VERSION "0.8.2"

#define SGS_VERSION_OFFSET 8


#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <malloc.h>

#include "sgs_cfg.h"
#include "sgs_xpc.h"



#ifdef SGS_INTERNAL
#  define SVT_NULL SGS_VT_NULL
#  define SVT_BOOL SGS_VT_BOOL
#  define SVT_INT SGS_VT_INT
#  define SVT_REAL SGS_VT_REAL
#  define SVT_STRING SGS_VT_STRING
#  define SVT_FUNC SGS_VT_FUNC
#  define SVT_CFUNC SGS_VT_CFUNC
#  define SVT_OBJECT SGS_VT_OBJECT
#  define SVT__COUNT SGS_VT__COUNT

#  define SOP_END SGS_OP_END
#  define SOP_DESTRUCT SGS_OP_DESTRUCT
#  define SOP_GETINDEX SGS_OP_GETINDEX
#  define SOP_SETINDEX SGS_OP_SETINDEX
#  define SOP_CONVERT SGS_OP_CONVERT
#  define SOP_DUMP SGS_OP_DUMP
#  define SOP_GCMARK SGS_OP_GCMARK
#  define SOP_GETNEXT SGS_OP_GETNEXT
#  define SOP_CALL SGS_OP_CALL
#  define SOP_EXPR SGS_OP_EXPR
#  define SOP_FLAGS SGS_OP_FLAGS
#endif


/* API self-doc helpers */
#define SGSRESULT int     /* output code */
#define SGSBOOL   int     /* true (1) / false (0) */
#define SGSMIXED  int32_t /* usable (x >= 0) value on success, error code (x < 0) on failure */


/* Output codes */
#define SGS_SUCCESS  0  /* success (no errors) */
#define SGS_ENOTFND -1  /* item was not found */
#define SGS_ECOMP   -2  /* compile error */
#define SGS_ENOTOBJ -3  /* argument was not an object */
#define SGS_ENOTSUP -4  /* not supported */
#define SGS_EBOUNDS -5  /* index out of bounds */
#define SGS_EINVAL  -6  /* invalid value was passed */
#define SGS_EINPROC -7  /* process was interrupted */


/* Accessible / transferable data */
typedef int32_t sgs_Bool;
typedef int64_t sgs_Integer;
typedef double  sgs_Real;
typedef int32_t sgs_SizeVal;
typedef struct _LNTable sgs_LNTable;
typedef struct _sgs_Context sgs_Context;
typedef struct _sgs_Variable sgs_Variable;
typedef struct _sgs_StackFrame sgs_StackFrame;
typedef int (*sgs_CFunc) ( sgs_Context* );


struct _sgs_StackFrame
{
	sgs_Variable*   func;
	uint16_t*       lntable;
	const uint32_t* code;
	const uint32_t* iptr;
	const uint32_t* iend;
	sgs_StackFrame* prev;
	sgs_StackFrame* next;
};


/* Memory management */
typedef void* (*sgs_MemFunc)
(
	void* /* userdata */,
	void* /* curmem */,
	size_t /* numbytes */
);

/*
	unlike realloc, if size = 0, this function MUST return NULL
*/
static void* sgs_DefaultMemFunc( void* ud, void* ptr, size_t size )
{
	UNUSED( ud );
	if( ptr && size ) return realloc( ptr, size );
	else if( size )   return malloc( size );
	if( ptr )         free( ptr );
	return NULL;
}


/* Main output */
typedef void (*sgs_OutputFunc) (
	void* /* userdata */,
	sgs_Context* /* ctx / SGS_CTX */,
	const void* /* ptr */,
	sgs_SizeVal /* size */
);


/* Debug output */
#define SGS_INFO    100  /* information about potential issues and state of the system */
#define SGS_WARNING 200  /* non-fatal problems */
#define SGS_ERROR   300  /* fatal problems */

typedef void (*sgs_PrintFunc) (
	void* /* data */,
	sgs_Context* /* ctx / SGS_CTX */,
	int /* type */,
	const char* /* message */
);


/* Event Hook */
/* additional info can be found in the last stack frame */
#define SGS_HOOK_ENTER 1 /* entered a function */
#define SGS_HOOK_EXIT  2 /* exited a function */
#define SGS_HOOK_STEP  3 /* about to execute an instruction */
typedef void (*sgs_HookFunc) (
	void* /* userdata */,
	sgs_Context* /* ctx / SGS_CTX */,
	int /* event_id */
);


/* Virtual machine state */
#define SGS_STOP_ON_FIRST_ERROR		0x0001
#define SGS_HAS_ERRORS				0x00010000
#define SGS_MUST_STOP				(0x00020000 | SGS_HAS_ERRORS)


/* Statistics / debugging */
#define SGS_STAT_VERSION      0
#define SGS_STAT_VARCOUNT     1
#define SGS_STAT_MEMSIZE      2
#define SGS_STAT_DUMP_STACK   10
#define SGS_STAT_DUMP_GLOBALS 11
#define SGS_STAT_DUMP_OBJECTS 12
#define SGS_STAT_DUMP_FRAMES  13


/* Virtual machine control */
#define SGS_CNTL_STATE      1
#define SGS_CNTL_GET_STATE  2
#define SGS_CNTL_MINLEV     3
#define SGS_CNTL_GET_MINLEV 4


/* Context internals */
/* - variable types */
#define SGS_VT_NULL    0  /* null (/no) data */
#define SGS_VT_BOOL    1  /* bool(u8) data */
#define SGS_VT_INT     2  /* i64 data */
#define SGS_VT_REAL    3  /* f64 data */
#define SGS_VT_STRING  4  /* variable-length string data */
#define SGS_VT_FUNC    5  /* function data */
#define SGS_VT_CFUNC   6  /* C function */
#define SGS_VT_OBJECT  7  /* variable-length binary data */
#define SGS_VT__COUNT  8  /* number of available types */

/* - object data */
typedef struct sgs_ObjData sgs_VarObj;
struct sgs_ObjData
{
	sgs_SizeVal refcount;
	uint16_t flags;     /* features of the object */
	uint8_t redblue;    /* red or blue? mark & sweep */
	void* data;         /* should have offset=8 with packing alignment>=8 */
	void** iface;
	sgs_VarObj* prev;   /* pointer to previous GC object */
	sgs_VarObj* next;   /* pointer to next GC object */
};

typedef struct _sgs_iStr sgs_iStr;
typedef struct _sgs_iFunc sgs_iFunc;
typedef union _sgs_VarData
{
	sgs_Bool    B;
	sgs_Integer I;
	sgs_Real    R;
	sgs_iStr*   S;
	sgs_iFunc*  F;
	sgs_CFunc   C;
	sgs_VarObj* O;
}
sgs_VarData;

struct _sgs_Variable
{
	uint8_t     type;
	sgs_VarData data;
};

/* - object interface */
typedef int (*sgs_ObjCallback) ( sgs_Context*, sgs_VarObj*, int /* arg */ );

#define SGS_OP( idx ) ((void*)idx)
#define SGS_OP_END        SGS_OP( 0 )
#define SGS_OP_DESTRUCT   SGS_OP( 1 )  /* arg = should free child vars? */
#define SGS_OP_GETINDEX   SGS_OP( 2 )  /* arg = prop? */
#define SGS_OP_SETINDEX   SGS_OP( 3 )  /* arg = prop? */
#define SGS_OP_CONVERT    SGS_OP( 4 )  /* arg = type(B|I|R|S)/spec. */
#define SGS_OP_DUMP       SGS_OP( 5 )
#define SGS_OP_GCMARK     SGS_OP( 6 )
#define SGS_OP_GETNEXT    SGS_OP( 7 )
#define SGS_OP_CALL       SGS_OP( 8 )
#define SGS_OP_EXPR       SGS_OP( 9 )

#define SGS_OP_FLAGS      SGS_OP(100)
#define SGS_OBJ_ARRAY     0x01

/* parameter flags / special values */
#define SGS_GETNEXT_KEY   0x01
#define SGS_GETNEXT_VALUE 0x02

#define SGS_EOP_ADD       0
#define SGS_EOP_SUB       1
#define SGS_EOP_MUL       2
#define SGS_EOP_DIV       3
#define SGS_EOP_MOD       4
#define SGS_EOP_COMPARE   5
#define SGS_EOP_NEGATE    6

#define SGS_CONVOP_CLONE  11
#define SGS_CONVOP_TOTYPE 12
#define SGS_CONVOP_TOITER 13


/* Engine context */
#define SGS_CTX sgs_Context* C


sgs_Context*
sgs_CreateEngineExt
(
	sgs_MemFunc memfunc,
	void* mfuserdata
);
static SGS_INLINE sgs_Context* sgs_CreateEngine()
	{ return sgs_CreateEngineExt( sgs_DefaultMemFunc, NULL ); }

void sgs_DestroyEngine( SGS_CTX );


#define SGS_CODE_VT 1 /* variable type */
#define SGS_CODE_OP 2 /* VM instruction */
#define SGS_CODE_OI 3 /* object interface */
const char* sgs_CodeString( int type, int val );


/* Core systems */
#define SGSOUTPUTFN_DEFAULT ((sgs_OutputFunc)-1)
void sgs_SetOutputFunc( SGS_CTX, sgs_OutputFunc func, void* ctx );
void sgs_Write( SGS_CTX, const void* ptr, sgs_SizeVal size );
static SGS_INLINE void sgs_WriteStr( SGS_CTX, const char* str )
	{ sgs_Write( C, str, strlen( str ) ); }
void sgs_Writef( SGS_CTX, const char* what, ... );

#define SGSPRINTFN_DEFAULT ((sgs_PrintFunc)-1)
void sgs_SetPrintFunc( SGS_CTX, sgs_PrintFunc func, void* ctx );
void sgs_Printf( SGS_CTX, int type, const char* what, ... );

SGSBOOL sgs_GetHookFunc( SGS_CTX, sgs_HookFunc* outf, void** outc );
void sgs_SetHookFunc( SGS_CTX, sgs_HookFunc func, void* ctx );

void* sgs_Memory( SGS_CTX, void* ptr, size_t size );
#define sgs_Malloc( C, size ) sgs_Memory( C, NULL, size )
#define sgs_Free( C, ptr ) sgs_Memory( C, ptr, 0 )
#define sgs_Realloc sgs_Memory
/* assumes SGS_CTX: */
#define sgs_Alloc( what ) (what*) sgs_Malloc( C, sizeof(what) )
#define sgs_Alloc_n( what, cnt ) (what*) sgs_Malloc( C, sizeof(what) * cnt )
#define sgs_Alloc_a( what, app ) (what*) sgs_Malloc( C, sizeof(what) + app )
#define sgs_Dealloc( ptr ) sgs_Free( C, ptr )


SGSRESULT
sgs_ExecBuffer
(
	SGS_CTX,
	const char* buf,
	sgs_SizeVal size
);

SGSRESULT
sgs_EvalBuffer
(
	SGS_CTX,
	const char* buf,
	sgs_SizeVal size,
	int* rvc
);

SGSRESULT
sgs_EvalFile
(
	SGS_CTX,
	const char* file,
	int* rvc
);

SGSRESULT
sgs_Compile
(
	SGS_CTX,
	const char* buf,
	sgs_SizeVal size,
	char** outbuf,
	sgs_SizeVal* outsize
);

SGSRESULT sgs_DumpCompiled( SGS_CTX, const char* buf, sgs_SizeVal size );

SGSMIXED sgs_Stat( SGS_CTX, int type );
int32_t sgs_Cntl( SGS_CTX, int what, int32_t val );

void
sgs_StackFrameInfo
(
	SGS_CTX,
	sgs_StackFrame* frame,
	const char** name,
	const char** file,
	int* line
);

sgs_StackFrame* sgs_GetFramePtr( SGS_CTX, int end );


static SGS_INLINE SGSRESULT sgs_ExecString( SGS_CTX, const char* str )
	{ return sgs_ExecBuffer( C, str, strlen( str ) ); }
static SGS_INLINE SGSRESULT sgs_EvalString( SGS_CTX, const char* str, int* rvc )
	{ return sgs_EvalBuffer( C, str, strlen( str ), rvc ); }
static SGS_INLINE SGSRESULT sgs_ExecFile( SGS_CTX, const char* file )
	{ return sgs_EvalFile( C, file, NULL ); }


/* Additional libraries */

SGSRESULT sgs_LoadLib_IO( SGS_CTX );
SGSRESULT sgs_LoadLib_Math( SGS_CTX );
#if 0
SGSRESULT sgs_LoadLib_Native( SGS_CTX );
#endif
SGSRESULT sgs_LoadLib_OS( SGS_CTX );
SGSRESULT sgs_LoadLib_String( SGS_CTX );
SGSRESULT sgs_LoadLib_Type( SGS_CTX );

typedef struct _sgs_RegFuncConst
{
	char* name;
	sgs_CFunc value;
}
sgs_RegFuncConst;
SGSRESULT sgs_RegFuncConsts( SGS_CTX, const sgs_RegFuncConst* list, int size );

typedef struct _sgs_RegIntConst
{
	char* name;
	sgs_Integer value;
}
sgs_RegIntConst;
SGSRESULT sgs_RegIntConsts( SGS_CTX, const sgs_RegIntConst* list, int size );

typedef struct _sgs_RegRealConst
{
	char* name;
	sgs_Real value;
}
sgs_RegRealConst;
SGSRESULT sgs_RegRealConsts( SGS_CTX, const sgs_RegRealConst* list, int size );


/* The core interface */

/*
	STACK & SUB-ITEMS
*/
void sgs_PushNull( SGS_CTX );
void sgs_PushBool( SGS_CTX, sgs_Bool value );
void sgs_PushInt( SGS_CTX, sgs_Integer value );
void sgs_PushReal( SGS_CTX, sgs_Real value );
void sgs_PushStringBuf( SGS_CTX, const char* str, sgs_SizeVal size );
void sgs_PushString( SGS_CTX, const char* str );
void sgs_PushCFunction( SGS_CTX, sgs_CFunc func );
void sgs_PushObject( SGS_CTX, void* data, void** iface );
void sgs_PushVariable( SGS_CTX, sgs_Variable* var );

SGSRESULT sgs_PushItem( SGS_CTX, int item );
SGSRESULT sgs_StoreItem( SGS_CTX, int item );
SGSRESULT sgs_PushProperty( SGS_CTX, const char* name );
SGSRESULT sgs_PushIndex( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx );
SGSRESULT sgs_PushGlobal( SGS_CTX, const char* name );
SGSRESULT sgs_StoreGlobal( SGS_CTX, const char* name );

/* sgs_Get(Num)Index: must release "out" */
SGSRESULT sgs_GetIndex( SGS_CTX, sgs_Variable* out, sgs_Variable* obj, sgs_Variable* idx );
SGSRESULT sgs_SetIndex( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_Variable* val );
SGSRESULT sgs_GetNumIndex( SGS_CTX, sgs_Variable* out, sgs_Variable* obj, sgs_Integer idx );
SGSRESULT sgs_SetNumIndex( SGS_CTX, sgs_Variable* obj, sgs_Integer idx, sgs_Variable* val );

SGSRESULT sgs_Pop( SGS_CTX, int count );
SGSRESULT sgs_PopSkip( SGS_CTX, int count, int skip );

/*
	OPERATIONS
*/
SGSRESULT sgs_FCall( SGS_CTX, int args, int expect, int gotthis );
#define sgs_Call( C, args, expect ) sgs_FCall( C, args, expect, 0 )
#define sgs_ThisCall( C, args, expect ) sgs_FCall( C, args, expect, 1 )
SGSRESULT sgs_GlobalCall( SGS_CTX, const char* name, int args, int expect );
SGSRESULT sgs_TypeOf( SGS_CTX );
SGSRESULT sgs_DumpVar( SGS_CTX, int maxdepth );
SGSRESULT sgs_GCExecute( SGS_CTX );

SGSRESULT sgs_PadString( SGS_CTX );
SGSRESULT sgs_StringConcat( SGS_CTX );
SGSRESULT sgs_StringMultiConcat( SGS_CTX, int args );
SGSRESULT sgs_CloneItem( SGS_CTX, int item );

sgs_Real sgs_CompareF( SGS_CTX, sgs_Variable* v1, sgs_Variable* v2 );
static SGS_INLINE int sgs_Compare( SGS_CTX, sgs_Variable* v1, sgs_Variable* v2 )
{ sgs_Real v = sgs_CompareF( C, v1, v2 ); if( v < 0 ) return -1; return v > 0 ? 1 : 0; }
SGSBOOL sgs_EqualTypes( SGS_CTX, sgs_Variable* v1, sgs_Variable* v2 );

/*
	CONVERSION / RETRIEVAL
*/
sgs_Bool sgs_GetBool( SGS_CTX, int item );
sgs_Integer sgs_GetInt( SGS_CTX, int item );
sgs_Real sgs_GetReal( SGS_CTX, int item );

sgs_Bool sgs_ToBool( SGS_CTX, int item );
sgs_Integer sgs_ToInt( SGS_CTX, int item );
sgs_Real sgs_ToReal( SGS_CTX, int item );
char* sgs_ToStringBuf( SGS_CTX, int item, sgs_SizeVal* outsize );
#define sgs_ToString( ctx, item ) sgs_ToStringBuf( ctx, item, NULL )
char* sgs_ToStringBufFast( SGS_CTX, int item, sgs_SizeVal* outsize );
#define sgs_ToStringFast( ctx, item ) sgs_ToStringBufFast( ctx, item, NULL )
SGSRESULT sgs_Convert( SGS_CTX, int item, int type );

/*
	ARGUMENT HANDLING
*/
SGSBOOL sgs_IsNumericString( const char* str, sgs_SizeVal size );
SGSBOOL sgs_ParseBool( SGS_CTX, int item, sgs_Bool* out );
SGSBOOL sgs_ParseInt( SGS_CTX, int item, sgs_Integer* out );
SGSBOOL sgs_ParseReal( SGS_CTX, int item, sgs_Real* out );
SGSBOOL sgs_ParseString( SGS_CTX, int item, char** out, sgs_SizeVal* size );

SGSRESULT sgs_PushIterator( SGS_CTX, int item );
SGSMIXED sgs_PushNextKey( SGS_CTX, int item );

SGSBOOL sgs_IsArray( SGS_CTX, sgs_Variable* var );
SGSMIXED sgs_ArraySize( SGS_CTX, sgs_Variable* var );
SGSBOOL sgs_ArrayGet( SGS_CTX, sgs_Variable* var, sgs_SizeVal which, sgs_Variable* out );

/*
	EXTENSION UTILITIES
*/
int sgs_StackSize( SGS_CTX );
SGSBOOL sgs_IsValidIndex( SGS_CTX, int item );
SGSBOOL sgs_GetStackItem( SGS_CTX, int item, sgs_Variable* out );
int sgs_ItemType( SGS_CTX, int item );
SGSBOOL sgs_Method( SGS_CTX );
void sgs_Acquire( SGS_CTX, sgs_Variable* var );
void sgs_Release( SGS_CTX, sgs_Variable* var );
SGSRESULT sgs_GCMark( SGS_CTX, sgs_Variable* var );

char* sgs_GetStringPtr( SGS_CTX, int item );
sgs_SizeVal sgs_GetStringSize( SGS_CTX, int item );
sgs_VarObj* sgs_GetObjectData( SGS_CTX, int item );


#ifdef __cplusplus
}
#endif

#endif /* SGSCRIPT_H_INCLUDED */
