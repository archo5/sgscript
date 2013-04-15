
#ifndef SGSCRIPT_H_INCLUDED
#define SGSCRIPT_H_INCLUDED


#define SGS_VERSION_MAJOR 0
#define SGS_VERSION_MINOR 8
#define SGS_VERSION_INCR  2
#define SGS_VERSION "0.8.2"


#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <malloc.h>

#include "sgs_cfg.h"
#include "sgs_xpc.h"


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

static void* sgs_DefaultMemFunc( void* ud, void* ptr, size_t size )
{
	UNUSED( ud );
	return realloc( ptr, size );
}


/* Debug output */
#define SGS_INFO    0  /* information about potential issues and state of the system */
#define SGS_WARNING 1  /* non-fatal problems */
#define SGS_ERROR   2  /* fatal problems */

typedef void (*sgs_PrintFunc) (
	void* /* data */,
	sgs_Context* /* ctx / SGS_CTX */,
	int /* type */,
	int /* line */,
	const char* /* message */
);


/* Statistics / debugging */
#define SGS_STAT_VARCOUNT     1
#define SGS_STAT_DUMP_STACK   10
#define SGS_STAT_DUMP_GLOBALS 11
#define SGS_STAT_DUMP_OBJECTS 12
#define SGS_STAT_DUMP_FRAMES  13


/* Context internals */
/* - variable types */
#define SVT_NULL    0  /* null (/no) data */
#define SVT_BOOL    1  /* bool(u8) data */
#define SVT_INT     2  /* i64 data */
#define SVT_REAL    3  /* f64 data */
#define SVT_STRING  4  /* variable-length string data */
#define SVT_FUNC    5  /* function data */
#define SVT_CFUNC   6  /* C function */
#define SVT_OBJECT  7  /* variable-length binary data */
#define SVT__COUNT  8  /* number of available types */

/* - object data */
typedef struct sgs_ObjData sgs_VarObj;
struct sgs_ObjData
{
	sgs_SizeVal refcount;
	void* data;
	void** iface;
	sgs_VarObj* prev;   /* pointer to previous GC object */
	sgs_VarObj* next;   /* pointer to next GC object */
	uint8_t redblue;    /* red or blue? mark & sweep */
	uint8_t destroying; /* whether the variable is already in a process of destruction.
	                       for container circ. ref. handling. */
};

/* - object interface */
typedef int (*sgs_ObjCallback) ( sgs_Context*, sgs_VarObj* data );

#define SOP( idx ) ((void*)idx)
#define SOP_END        SOP( 0 )
#define SOP_DESTRUCT   SOP( 1 )
#define SOP_CLONE      SOP( 2 )
#define SOP_GETTYPE    SOP( 3 )
#define SOP_GETPROP    SOP( 4 )
#define SOP_SETPROP    SOP( 5 )
#define SOP_GETINDEX   SOP( 6 )
#define SOP_SETINDEX   SOP( 7 )
#define SOP_TOBOOL     SOP( 8 )
#define SOP_TOINT      SOP( 9 )
#define SOP_TOREAL     SOP( 10 )
#define SOP_TOSTRING   SOP( 11 )
#define SOP_DUMP       SOP( 12 )
#define SOP_GCMARK     SOP( 13 )
#define SOP_GETITER    SOP( 14 )
#define SOP_NEXTKEY    SOP( 15 )
#define SOP_CALL       SOP( 16 )
#define SOP_COMPARE    SOP( 17 )
#define SOP_OP_ADD     SOP( 18 )
#define SOP_OP_SUB     SOP( 19 )
#define SOP_OP_MUL     SOP( 20 )
#define SOP_OP_DIV     SOP( 21 )
#define SOP_OP_MOD     SOP( 22 )
#define SOP_OP_NEGATE  SOP( 23 )


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


/* Core systems */
#define SGSPRINTFN_DEFAULT ((sgs_PrintFunc)-1)
void sgs_SetPrintFunc( SGS_CTX, sgs_PrintFunc func, void* ctx );
void sgs_Printf( SGS_CTX, int type, int line, const char* what, ... );
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

SGSRESULT sgs_Stat( SGS_CTX, int type );

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
#define sgs_Call( C, args, expect ) sgs_FCall( C, args, expect, FALSE )
#define sgs_ThisCall( C, args, expect ) sgs_FCall( C, args, expect, TRUE )
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

/*
	ARGUMENT HANDLING
*/
SGSBOOL sgs_IsNumericString( const char* str, sgs_SizeVal size );
SGSBOOL sgs_ParseBool( SGS_CTX, int item, sgs_Bool* out );
SGSBOOL sgs_ParseInt( SGS_CTX, int item, sgs_Integer* out );
SGSBOOL sgs_ParseReal( SGS_CTX, int item, sgs_Real* out );
SGSBOOL sgs_ParseString( SGS_CTX, int item, char** out, sgs_SizeVal* size );

SGSBOOL sgs_IsArray( SGS_CTX, sgs_Variable* var );
SGSMIXED sgs_ArraySize( SGS_CTX, sgs_Variable* var );
SGSBOOL sgs_ArrayGet( SGS_CTX, sgs_Variable* var, sgs_SizeVal which, sgs_Variable* out );

/*
	EXTENSION UTILITIES
*/
int sgs_StackSize( SGS_CTX );
SGSBOOL sgs_IsValidIndex( SGS_CTX, int item );
sgs_Variable* sgs_StackItem( SGS_CTX, int item );
int sgs_ItemType( SGS_CTX, int item );
SGSBOOL sgs_Method( SGS_CTX );
void sgs_Acquire( SGS_CTX, sgs_Variable* var );
void sgs_Release( SGS_CTX, sgs_Variable* var );
SGSRESULT sgs_GCMark( SGS_CTX, sgs_Variable* var );

const char* sgs_GetStringPtr( SGS_CTX, int item );
sgs_SizeVal sgs_GetStringSize( SGS_CTX, int item );
sgs_VarObj* sgs_GetObjectData( SGS_CTX, int item );


#ifdef __cplusplus
}
#endif

#endif /* SGSCRIPT_H_INCLUDED */
