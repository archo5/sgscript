
#ifndef SGSCRIPT_H_INCLUDED
#define SGSCRIPT_H_INCLUDED


#define SGS_VERSION_MAJOR 0
#define SGS_VERSION_MINOR 9
#define SGS_VERSION_INCR  0
#define SGS_VERSION "0.9.0"
#define SGS_API_VERSION 1

#define SGS_VERSION_OFFSET 8


#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <malloc.h>

#include "sgs_cfg.h"
#include "sgs_xpc.h"



#ifdef SGS_INTERNAL
#  define VT_NULL SGS_VT_NULL
#  define VT_BOOL SGS_VT_BOOL
#  define VT_INT SGS_VT_INT
#  define VT_REAL SGS_VT_REAL
#  define VT_STRING SGS_VT_STRING
#  define VT_FUNC SGS_VT_FUNC
#  define VT_CFUNC SGS_VT_CFUNC
#  define VT_OBJECT SGS_VT_OBJECT
#  define VTYPE_MASK SGS_VTYPE_MASK
#  define BASETYPE SGS_BASETYPE
#  define VTF_NUM SGS_VTF_NUM
#  define VTF_CALL SGS_VTF_CALL
#  define VTF_REF SGS_VTF_REF
#  define VTF_ARRAY SGS_VTF_ARRAY
#  define VTF_ARRAY_ITER SGS_VTF_ARRAY_ITER
#  define VTF_DICT SGS_VTF_DICT
#  define VTC_NULL SGS_VTC_NULL
#  define VTC_BOOL SGS_VTC_BOOL
#  define VTC_INT SGS_VTC_INT
#  define VTC_REAL SGS_VTC_REAL
#  define VTC_STRING SGS_VTC_STRING
#  define VTC_FUNC SGS_VTC_FUNC
#  define VTC_CFUNC SGS_VTC_CFUNC
#  define VTC_OBJECT SGS_VTC_OBJECT
#  define VTC_ARRAY SGS_VTC_ARRAY
#  define VTC_ARRAY_ITER SGS_VTC_ARRAY_ITER
#  define VTC_DICT SGS_VTC_DICT

#  define SOP_END SGS_OP_END
#  define SOP_DESTRUCT SGS_OP_DESTRUCT
#  define SOP_GETINDEX SGS_OP_GETINDEX
#  define SOP_SETINDEX SGS_OP_SETINDEX
#  define SOP_CONVERT SGS_OP_CONVERT
#  define SOP_SERIALIZE SGS_OP_SERIALIZE
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
typedef int64_t sgs_Int;
typedef double  sgs_Real;
typedef int32_t sgs_SizeVal;
typedef struct _LNTable sgs_LNTable;
typedef struct _sgs_Context sgs_Context;
typedef struct _sgs_Variable sgs_Variable;
typedef struct _sgs_StackFrame sgs_StackFrame;
typedef int (*sgs_CFunc) ( sgs_Context* );

#define sgs_Integer sgs_Int
#define sgs_Float sgs_Real


struct _sgs_StackFrame
{
	sgs_Variable*   func;
	uint16_t*       lntable;
	const uint32_t* code;
	const uint32_t* iptr;
	const uint32_t* iend;
	const char*     nfname;
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
#define SGS_STAT_APIVERSION   1
#define SGS_STAT_VARCOUNT     2
#define SGS_STAT_MEMSIZE      3
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
/* - variable type flags, primary and extended */
#define SGS_VT_NULL   0
#define SGS_VT_BOOL   0x01
#define SGS_VT_INT    0x02
#define SGS_VT_REAL   0x04
#define SGS_VT_STRING 0x08
#define SGS_VT_FUNC   0x10
#define SGS_VT_CFUNC  0x20
#define SGS_VT_OBJECT 0x40

#define SGS_VTYPE_MASK 0xff
#define SGS_BASETYPE( x ) ( x & SGS_VTYPE_MASK )

#define SGS_VTF_NUM    0x0100
#define SGS_VTF_CALL   0x0200
#define SGS_VTF_REF    0x0400
#define SGS_VTF_ARRAY  0x1000
#define SGS_VTF_ARRAY_ITER 0x2000
#define SGS_VTF_DICT   0x4000

/* - complete variable types */
#define SGS_VTC_NULL    (SGS_VT_NULL)
#define SGS_VTC_BOOL    (SGS_VT_BOOL)
#define SGS_VTC_INT     (SGS_VT_INT | SGS_VTF_NUM)
#define SGS_VTC_REAL    (SGS_VT_REAL | SGS_VTF_NUM)
#define SGS_VTC_STRING  (SGS_VT_STRING | SGS_VTF_REF)
#define SGS_VTC_FUNC    (SGS_VT_FUNC | SGS_VTF_CALL | SGS_VTF_REF)
#define SGS_VTC_CFUNC   (SGS_VT_CFUNC | SGS_VTF_CALL)
#define SGS_VTC_OBJECT  (SGS_VT_OBJECT | SGS_VTF_REF)
#define SGS_VTC_ARRAY   (SGS_VTC_OBJECT | SGS_VTF_ARRAY)
#define SGS_VTC_ARRAY_ITER (SGS_VTC_OBJECT | SGS_VTF_ARRAY_ITER)
#define SGS_VTC_DICT    (SGS_VTC_OBJECT | SGS_VTF_DICT)

/* - object data */
typedef struct sgs_ObjData sgs_VarObj;
typedef int (*sgs_ObjCallback) ( sgs_Context*, sgs_VarObj*, int /* arg */ );
struct sgs_ObjData
{
	sgs_SizeVal refcount;
	uint16_t flags;     /* features of the object */
	uint8_t redblue;    /* red or blue? mark & sweep */
	void* data;         /* should have offset=8 with packing alignment>=8 */
	void** iface;
	sgs_VarObj* prev;   /* pointer to previous GC object */
	sgs_VarObj* next;   /* pointer to next GC object */
	/* cache */
	sgs_ObjCallback getindex;
	sgs_ObjCallback getnext;
};

typedef struct _sgs_iStr sgs_iStr;
typedef struct _sgs_iFunc sgs_iFunc;
typedef union _sgs_VarData
{
	sgs_SizeVal* pRC;
	sgs_Bool    B;
	sgs_Int     I;
	sgs_Real    R;
	sgs_iStr*   S;
	sgs_iFunc*  F;
	sgs_CFunc   C;
	sgs_VarObj* O;
}
sgs_VarData;

struct _sgs_Variable
{
	uint32_t    type;
	sgs_VarData data;
};

/* - object interface */
#define SGS_OP( idx ) ((void*)idx)
#define SGS_OP_END        SGS_OP( 0 )
#define SGS_OP_DESTRUCT   SGS_OP( 1 )  /* arg = should free child vars? */
#define SGS_OP_GETINDEX   SGS_OP( 2 )  /* arg = prop? */
#define SGS_OP_SETINDEX   SGS_OP( 3 )  /* arg = prop? */
#define SGS_OP_CONVERT    SGS_OP( 4 )  /* arg = type(B|I|R|S)/spec. */
#define SGS_OP_SERIALIZE  SGS_OP( 5 )
#define SGS_OP_DUMP       SGS_OP( 6 )  /* arg = depth */
#define SGS_OP_GCMARK     SGS_OP( 7 )
#define SGS_OP_GETNEXT    SGS_OP( 8 )  /* arg = flags */
#define SGS_OP_CALL       SGS_OP( 9 )
#define SGS_OP_EXPR       SGS_OP(10 )  /* arg = op */

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

#define SGS_CONVOP_CLONE  0x10000
#define SGS_CONVOP_TOTYPE 0x20000
#define SGS_CONVOP_TOITER 0x30000


/* Engine context */
#define SGS_CTX sgs_Context* C


SGS_APIFUNC
sgs_Context*
sgs_CreateEngineExt
(
	sgs_MemFunc memfunc,
	void* mfuserdata
);
static SGS_INLINE sgs_Context* sgs_CreateEngine()
	{ return sgs_CreateEngineExt( sgs_DefaultMemFunc, NULL ); }

SGS_APIFUNC void sgs_DestroyEngine( SGS_CTX );


#define SGS_CODE_ER 0 /* error codes */
#define SGS_CODE_VT 1 /* variable type */
#define SGS_CODE_OP 2 /* VM instruction */
#define SGS_CODE_OI 3 /* object interface */
SGS_APIFUNC const char* sgs_CodeString( int type, int val );


/* Core systems */
#define SGSOUTPUTFN_DEFAULT ((sgs_OutputFunc)-1)
SGS_APIFUNC void sgs_SetOutputFunc( SGS_CTX, sgs_OutputFunc func, void* ctx );
SGS_APIFUNC void sgs_Write( SGS_CTX, const void* ptr, sgs_SizeVal size );
static SGS_INLINE void sgs_WriteStr( SGS_CTX, const char* str )
	{ sgs_Write( C, str, strlen( str ) ); }
SGS_APIFUNC void sgs_Writef( SGS_CTX, const char* what, ... );

#define SGSPRINTFN_DEFAULT ((sgs_PrintFunc)-1)
#define SGSPRINTFN_DEFAULT_NOABORT ((sgs_PrintFunc)-2)
SGS_APIFUNC void sgs_SetPrintFunc( SGS_CTX, sgs_PrintFunc func, void* ctx );
SGS_APIFUNC int sgs_Printf( SGS_CTX, int type, const char* what, ... );

SGS_APIFUNC SGSBOOL sgs_GetHookFunc( SGS_CTX, sgs_HookFunc* outf, void** outc );
SGS_APIFUNC void sgs_SetHookFunc( SGS_CTX, sgs_HookFunc func, void* ctx );

SGS_APIFUNC void* sgs_Memory( SGS_CTX, void* ptr, size_t size );
#define sgs_Malloc( C, size ) sgs_Memory( C, NULL, size )
#define sgs_Free( C, ptr ) sgs_Memory( C, ptr, 0 )
#define sgs_Realloc sgs_Memory
/* assumes SGS_CTX: */
#define sgs_Alloc( what ) (what*) sgs_Malloc( C, sizeof(what) )
#define sgs_Alloc_n( what, cnt ) (what*) sgs_Malloc( C, sizeof(what) * cnt )
#define sgs_Alloc_a( what, app ) (what*) sgs_Malloc( C, sizeof(what) + app )
#define sgs_Dealloc( ptr ) sgs_Free( C, ptr )


SGS_APIFUNC
SGSRESULT
sgs_EvalBuffer
(
	SGS_CTX,
	const char* buf,
	sgs_SizeVal size,
	int* rvc
);

SGS_APIFUNC
SGSRESULT
sgs_EvalFile
(
	SGS_CTX,
	const char* file,
	int* rvc
);

SGS_APIFUNC
SGSRESULT
sgs_Compile
(
	SGS_CTX,
	const char* buf,
	sgs_SizeVal size,
	char** outbuf,
	sgs_SizeVal* outsize
);

SGS_APIFUNC SGSRESULT sgs_DumpCompiled( SGS_CTX, const char* buf, sgs_SizeVal size );
SGS_APIFUNC SGSRESULT sgs_Abort( SGS_CTX );
SGS_APIFUNC SGSMIXED sgs_Stat( SGS_CTX, int type );
SGS_APIFUNC int32_t sgs_Cntl( SGS_CTX, int what, int32_t val );

SGS_APIFUNC
void
sgs_StackFrameInfo
(
	SGS_CTX,
	sgs_StackFrame* frame,
	const char** name,
	const char** file,
	int* line
);

SGS_APIFUNC sgs_StackFrame* sgs_GetFramePtr( SGS_CTX, int end );


#ifndef SGS_STRINGLENGTHFUNC
#define SGS_STRINGLENGTHFUNC strlen
#endif

#define sgs_ExecBuffer( C, buf, sz ) sgs_EvalBuffer( C, buf, sz, NULL )
#define sgs_ExecString( C, str ) sgs_ExecBuffer( C, str, SGS_STRINGLENGTHFUNC( str ) )
#define sgs_EvalString( C, str, rvc ) sgs_EvalBuffer( C, str, SGS_STRINGLENGTHFUNC( str ), rvc )
#define sgs_ExecFile( C, str ) sgs_EvalFile( C, str, NULL )


/* Additional libraries */

SGS_APIFUNC SGSRESULT sgs_LoadLib_Fmt( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_LoadLib_IO( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_LoadLib_Math( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_LoadLib_OS( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_LoadLib_String( SGS_CTX );

#define SGS_RC_END() { NULL, NULL }

typedef struct _sgs_RegFuncConst
{
	char* name;
	sgs_CFunc value;
}
sgs_RegFuncConst;
SGS_APIFUNC SGSRESULT sgs_RegFuncConsts( SGS_CTX, const sgs_RegFuncConst* list, int size );

typedef struct _sgs_RegIntConst
{
	char* name;
	sgs_Int value;
}
sgs_RegIntConst;
SGS_APIFUNC SGSRESULT sgs_RegIntConsts( SGS_CTX, const sgs_RegIntConst* list, int size );

typedef struct _sgs_RegRealConst
{
	char* name;
	sgs_Real value;
}
sgs_RegRealConst;
SGS_APIFUNC SGSRESULT sgs_RegRealConsts( SGS_CTX, const sgs_RegRealConst* list, int size );


/* The core interface */

/*
	STACK & SUB-ITEMS
*/
SGS_APIFUNC void sgs_PushNull( SGS_CTX );
SGS_APIFUNC void sgs_PushBool( SGS_CTX, sgs_Bool value );
SGS_APIFUNC void sgs_PushInt( SGS_CTX, sgs_Int value );
SGS_APIFUNC void sgs_PushReal( SGS_CTX, sgs_Real value );
SGS_APIFUNC void sgs_PushStringBuf( SGS_CTX, const char* str, sgs_SizeVal size );
SGS_APIFUNC void sgs_PushString( SGS_CTX, const char* str );
SGS_APIFUNC void sgs_PushCFunction( SGS_CTX, sgs_CFunc func );
SGS_APIFUNC void sgs_PushObject( SGS_CTX, void* data, void** iface );
SGS_APIFUNC void* sgs_PushObjectIPA( SGS_CTX, sgs_SizeVal added, void** iface );
SGS_APIFUNC void sgs_PushVariable( SGS_CTX, sgs_Variable* var );

SGS_APIFUNC SGSRESULT sgs_InsertVariable( SGS_CTX, int pos, sgs_Variable* var );
SGS_APIFUNC SGSRESULT sgs_PushArray( SGS_CTX, sgs_SizeVal numitems );
SGS_APIFUNC SGSRESULT sgs_PushDict( SGS_CTX, sgs_SizeVal numitems );

SGS_APIFUNC SGSRESULT sgs_PushItem( SGS_CTX, int item );
SGS_APIFUNC SGSRESULT sgs_StoreItem( SGS_CTX, int item );
SGS_APIFUNC SGSRESULT sgs_PushProperty( SGS_CTX, const char* name );
SGS_APIFUNC SGSRESULT sgs_PushIndexExt( SGS_CTX, int obj, int idx, int prop );
#define sgs_PushIndex( C, obj, idx ) sgs_PushIndexExt( C, obj, idx, 0 )
SGS_APIFUNC SGSRESULT sgs_PushIndexP( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx );
SGS_APIFUNC SGSRESULT sgs_StoreIndexExt( SGS_CTX, int obj, int idx, int prop );
#define sgs_StoreIndex( C, obj, idx ) sgs_StoreIndexExt( C, obj, idx, 0 )
SGS_APIFUNC SGSRESULT sgs_StoreIndexP( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx );
SGS_APIFUNC SGSRESULT sgs_PushGlobal( SGS_CTX, const char* name );
SGS_APIFUNC SGSRESULT sgs_StoreGlobal( SGS_CTX, const char* name );

SGS_APIFUNC SGSRESULT sgs_PushPath( SGS_CTX, int item, const char* path, ... );
SGS_APIFUNC SGSRESULT sgs_StorePath( SGS_CTX, int item, const char* path, ... );

/* sgs_Get(Num)Index: must release "out" */
SGS_APIFUNC SGSRESULT sgs_GetIndex( SGS_CTX, sgs_Variable* out, sgs_Variable* obj, sgs_Variable* idx );
SGS_APIFUNC SGSRESULT sgs_SetIndex( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_Variable* val );
SGS_APIFUNC SGSRESULT sgs_GetNumIndex( SGS_CTX, sgs_Variable* out, sgs_Variable* obj, sgs_Int idx );
SGS_APIFUNC SGSRESULT sgs_SetNumIndex( SGS_CTX, sgs_Variable* obj, sgs_Int idx, sgs_Variable* val );

SGS_APIFUNC SGSRESULT sgs_Pop( SGS_CTX, int count );
SGS_APIFUNC SGSRESULT sgs_PopSkip( SGS_CTX, int count, int skip );

/*
	OPERATIONS
*/
SGS_APIFUNC SGSRESULT sgs_FCall( SGS_CTX, int args, int expect, int gotthis );
#define sgs_Call( C, args, expect ) sgs_FCall( C, args, expect, 0 )
#define sgs_ThisCall( C, args, expect ) sgs_FCall( C, args, expect, 1 )
SGS_APIFUNC SGSRESULT sgs_GlobalCall( SGS_CTX, const char* name, int args, int expect );
SGS_APIFUNC SGSRESULT sgs_TypeOf( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_DumpVar( SGS_CTX, int maxdepth );
SGS_APIFUNC SGSRESULT sgs_GCExecute( SGS_CTX );

SGS_APIFUNC SGSRESULT sgs_PadString( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_StringConcat( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_StringMultiConcat( SGS_CTX, int args );
SGS_APIFUNC SGSRESULT sgs_CloneItem( SGS_CTX, int item );

SGS_APIFUNC SGSRESULT sgs_Serialize( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_SerializeObject( SGS_CTX, int args, const char* func );
SGS_APIFUNC SGSRESULT sgs_Unserialize( SGS_CTX );

SGS_APIFUNC sgs_Real sgs_CompareF( SGS_CTX, sgs_Variable* v1, sgs_Variable* v2 );
static SGS_INLINE int sgs_Compare( SGS_CTX, sgs_Variable* v1, sgs_Variable* v2 )
{ sgs_Real v = sgs_CompareF( C, v1, v2 ); if( v < 0 ) return -1; return v > 0 ? 1 : 0; }
SGS_APIFUNC SGSBOOL sgs_EqualTypes( SGS_CTX, sgs_Variable* v1, sgs_Variable* v2 );

/*
	CONVERSION / RETRIEVAL
*/
SGS_APIFUNC sgs_Bool sgs_GetBool( SGS_CTX, int item );
SGS_APIFUNC sgs_Int sgs_GetInt( SGS_CTX, int item );
SGS_APIFUNC sgs_Real sgs_GetReal( SGS_CTX, int item );

SGS_APIFUNC sgs_Bool sgs_ToBool( SGS_CTX, int item );
SGS_APIFUNC sgs_Int sgs_ToInt( SGS_CTX, int item );
SGS_APIFUNC sgs_Real sgs_ToReal( SGS_CTX, int item );
SGS_APIFUNC char* sgs_ToStringBuf( SGS_CTX, int item, sgs_SizeVal* outsize );
#define sgs_ToString( ctx, item ) sgs_ToStringBuf( ctx, item, NULL )
SGS_APIFUNC char* sgs_ToStringBufFast( SGS_CTX, int item, sgs_SizeVal* outsize );
#define sgs_ToStringFast( ctx, item ) sgs_ToStringBufFast( ctx, item, NULL )
SGS_APIFUNC SGSRESULT sgs_Convert( SGS_CTX, int item, int type );

/*
	ARGUMENT HANDLING
*/
SGS_APIFUNC SGSBOOL sgs_IsObject( SGS_CTX, int item, void** iface );
SGS_APIFUNC SGSBOOL sgs_IsNumericString( const char* str, sgs_SizeVal size );
SGS_APIFUNC SGSBOOL sgs_ParseBool( SGS_CTX, int item, sgs_Bool* out );
SGS_APIFUNC SGSBOOL sgs_ParseInt( SGS_CTX, int item, sgs_Int* out );
SGS_APIFUNC SGSBOOL sgs_ParseReal( SGS_CTX, int item, sgs_Real* out );
SGS_APIFUNC SGSBOOL sgs_ParseString( SGS_CTX, int item, char** out, sgs_SizeVal* size );

SGS_APIFUNC SGSRESULT sgs_PushIterator( SGS_CTX, int item );
SGS_APIFUNC SGSMIXED sgs_IterAdvance( SGS_CTX, int item );
SGS_APIFUNC SGSRESULT sgs_IterPushData( SGS_CTX, int item, int key, int value );

SGS_APIFUNC SGSBOOL sgs_IsArray( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC SGSMIXED sgs_ArraySize( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC SGSBOOL sgs_ArrayGet( SGS_CTX, sgs_Variable* var, sgs_SizeVal which, sgs_Variable* out );

/*
	EXTENSION UTILITIES
*/
SGS_APIFUNC int sgs_StackSize( SGS_CTX );
SGS_APIFUNC SGSBOOL sgs_IsValidIndex( SGS_CTX, int item );
SGS_APIFUNC SGSBOOL sgs_GetStackItem( SGS_CTX, int item, sgs_Variable* out );
SGS_APIFUNC int sgs_ItemType( SGS_CTX, int item );
SGS_APIFUNC int sgs_ItemTypeExt( SGS_CTX, int item );
SGS_APIFUNC SGSBOOL sgs_Method( SGS_CTX );
SGS_APIFUNC void sgs_Acquire( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC void sgs_Release( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC void sgs_ReleaseOwned( SGS_CTX, sgs_Variable* var, int dco );
SGS_APIFUNC SGSRESULT sgs_GCMark( SGS_CTX, sgs_Variable* var );

SGS_APIFUNC char* sgs_GetStringPtr( SGS_CTX, int item );
SGS_APIFUNC sgs_SizeVal sgs_GetStringSize( SGS_CTX, int item );
SGS_APIFUNC sgs_VarObj* sgs_GetObjectData( SGS_CTX, int item );

SGS_APIFUNC int sgs_HasFuncName( SGS_CTX );
SGS_APIFUNC void sgs_FuncName( SGS_CTX, const char* fnliteral );
SGS_APIFUNC int sgs_Errno( SGS_CTX, int clear );
SGS_APIFUNC int sgs_SetErrno( SGS_CTX, int err );
SGS_APIFUNC int sgs_GetLastErrno( SGS_CTX );

#define SGSFN( x ) sgs_FuncName( C, x )
#define SGSBASEFN( x ) if( !sgs_HasFuncName( C ) ) sgs_FuncName( C, x )
#define SGSCERR sgs_Errno( C, 0 )
#define SGSCLEARERR sgs_Errno( C, 1 )


#ifdef __cplusplus
}
#endif

#endif /* SGSCRIPT_H_INCLUDED */
