
#ifndef SGSCRIPT_H_INCLUDED
#define SGSCRIPT_H_INCLUDED


#define SGS_VERSION_MAJOR 0
#define SGS_VERSION_MINOR 9
#define SGS_VERSION_INCR  7
#define SGS_VERSION "0.9.7"

#define SGS_VERSION_OFFSET 8
#define SGS_VERSION_INT ( ( ( ( SGS_VERSION_MAJOR << SGS_VERSION_OFFSET ) | \
        SGS_VERSION_MINOR ) << SGS_VERSION_OFFSET ) | SGS_VERSION_INCR )


#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#include "sgs_cfg.h"
#include "sgs_xpc.h"



/* API self-doc helpers */
#define SGSZERO   int     /* always 0 */
#define SGSONE    int     /* always 1 */
#define SGSRESULT int     /* output code */
#define SGSBOOL   int     /* true (1) / false (0) */
#define SGSMIXED  int32_t /* usable (x >= 0) value on success, error code (x < 0) on failure */


/* Output codes */
#define SGS_SUCCESS  0  /* success (no errors) */
#define SGS_SUCABRT  1  /* success (aborted) */
#define SGS_ENOTFND -1  /* item was not found */
#define SGS_ECOMP   -2  /* compile error */
#define SGS_ENOTOBJ -3  /* argument was not an object */
#define SGS_ENOTSUP -4  /* not supported */
#define SGS_EBOUNDS -5  /* index out of bounds */
#define SGS_EINVAL  -6  /* invalid value was passed */
#define SGS_EINPROC -7  /* process was interrupted */


/* Code tests */
#define SGS_FAILED( e ) ((e)<0)
#define SGS_SUCCEEDED( e ) ((e)>=0)
#define SGS_CALL_TRIED( e ) ((e)!=SGS_EINVAL)
#define SGS_CALL_FAILED( e ) ((e)==SGS_EINVAL)


/* Accessible / transferable data */
typedef int32_t sgs_Bool;
typedef int64_t sgs_Int;
typedef double  sgs_Real;
typedef int32_t sgs_SizeVal;
typedef int32_t sgs_StkIdx;
typedef struct _sgs_Context sgs_Context;
typedef struct _sgs_Variable sgs_Variable;
typedef struct _sgs_StackFrame sgs_StackFrame;
typedef int (*sgs_CFunc) ( sgs_Context* );

#define sgs_Integer sgs_Int
#define sgs_Float sgs_Real


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
	SGS_UNUSED( ud );
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
	size_t /* size */
);


/* Debug output */
#define SGS_INFO    100  /* information about potential issues and state of the system */
#define SGS_WARNING 200  /* non-fatal problems */
#define SGS_ERROR   300  /* fatal problems */

typedef void (*sgs_MsgFunc) (
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


/* Script file system */
typedef struct _sgs_ScriptFSData
{
	void* userhandle;
	const char* filename;
	void* output;
	size_t size;
}
sgs_ScriptFSData;
#define SGS_SFS_FILE_EXISTS 1
#define SGS_SFS_FILE_OPEN 2
#define SGS_SFS_FILE_READ 3
#define SGS_SFS_FILE_CLOSE 4
typedef SGSRESULT (*sgs_ScriptFSFunc) (
	void* /* userdata */,
	sgs_Context* /* ctx / SGS_CTX */,
	int /* op */,
	sgs_ScriptFSData* /* data */
);


/* Virtual machine state */
#define SGS_STOP_ON_FIRST_ERROR 0x0001
#define SGS_HAS_ERRORS          0x00010000
#define SGS_MUST_STOP          (0x00020000 | SGS_HAS_ERRORS)
#define SGS_SERIALIZE_MODE2     0x0004
#define SGS_STATE_PAUSED        0x0008


/* Statistics / debugging */
#define SGS_STAT_VERSION      0
#define SGS_STAT_STATECOUNT   1
#define SGS_STAT_OBJCOUNT     2
#define SGS_STAT_MEMSIZE      3
#define SGS_STAT_NUMALLOCS    4
#define SGS_STAT_NUMFREES     5
#define SGS_STAT_NUMBLOCKS    6
#define SGS_STAT_DUMP_STACK   10
#define SGS_STAT_DUMP_GLOBALS 11
#define SGS_STAT_DUMP_OBJECTS 12
#define SGS_STAT_DUMP_FRAMES  13
#define SGS_STAT_DUMP_STATS   14
#define SGS_STAT_XDUMP_STACK  20


/* Virtual machine control */
#define SGS_CNTL_STATE      1
#define SGS_CNTL_GET_STATE  2
#define SGS_CNTL_MINLEV     3
#define SGS_CNTL_GET_MINLEV 4
#define SGS_CNTL_APILEV     5
#define SGS_CNTL_GET_APILEV 6
#define SGS_CNTL_ERRNO      7
#define SGS_CNTL_SET_ERRNO  8
#define SGS_CNTL_GET_ERRNO  9
#define SGS_CNTL_ERRSUP     10
#define SGS_CNTL_GET_ERRSUP 11
#define SGS_CNTL_SERIALMODE 12
#define SGS_CNTL_NUMRETVALS 13
#define SGS_CNTL_GET_PAUSED 14


/* Object actions */
#define SGS_ACT_ARRAY_PUSH   1
#define SGS_ACT_ARRAY_POP    2
#define SGS_ACT_ARRAY_POPRET 3
#define SGS_ACT_ARRAY_FIND   4
#define SGS_ACT_ARRAY_RM_ONE 5
#define SGS_ACT_ARRAY_RM_ALL 6
#define SGS_ACT_DICT_UNSET   11
#define SGS_ACT_MAP_UNSET    21


/* Context internals */
/* - variable types */
#define SGS_VT_NULL   0
#define SGS_VT_BOOL   1
#define SGS_VT_INT    2
#define SGS_VT_REAL   3
#define SGS_VT_STRING 4
#define SGS_VT_FUNC   5
#define SGS_VT_CFUNC  6
#define SGS_VT_OBJECT 7
#define SGS_VT_PTR    8
#define SGS_VT__COUNT 9

/* - object data */
typedef struct sgs_ObjData sgs_VarObj;
typedef int (*sgs_ObjCallback) ( sgs_Context*, sgs_VarObj*, int /* arg */ );

typedef int (*sgs_OC_Self) ( sgs_Context*, sgs_VarObj* );
typedef int (*sgs_OC_SlPr) ( sgs_Context*, sgs_VarObj*, int );
typedef int (*sgs_OC_V1Pr) ( sgs_Context*, sgs_VarObj*, sgs_Variable*, int );
typedef int (*sgs_OC_V2Pr) ( sgs_Context*, sgs_VarObj*, sgs_Variable*, sgs_Variable*, int );

typedef struct _sgs_ObjInterface
{
	const char* name;
	
	sgs_OC_Self destruct;
	sgs_OC_Self gcmark;
	
	sgs_OC_V1Pr getindex;
	sgs_OC_V2Pr setindex;
	
	sgs_OC_SlPr convert;
	sgs_OC_Self serialize;
	sgs_OC_SlPr dump;
	sgs_OC_SlPr getnext;
	
	sgs_OC_Self call;
	sgs_OC_V2Pr expr;
}
sgs_ObjInterface;

struct sgs_ObjData
{
	sgs_SizeVal refcount;
	uint32_t appsize;
	uint8_t redblue;    /* red or blue? mark & sweep */
	uint8_t mm_enable;  /* use metamethods? */
	uint8_t is_iface;   /* whether is also in the interface table */
	uint8_t in_setindex; /* whether already running custom setindex code */
	void* data;         /* should have offset=8 with packing alignment>=8 */
	sgs_ObjInterface* iface;
	sgs_VarObj* prev;   /* pointer to previous GC object */
	sgs_VarObj* next;   /* pointer to next GC object */
	sgs_VarObj* metaobj; /* pointer to meta-object */
};

typedef struct _sgs_iStr sgs_iStr;
struct _sgs_iStr
{
	sgs_SizeVal refcount;
	uint32_t size;
	uint32_t hash;
};
#define sgs_str_cstr( pstr ) (((char*)(pstr))+sizeof(sgs_iStr))
#define sgs_str_c_cstr( pstr ) (((const char*)(pstr))+sizeof(sgs_iStr))
#define sgs_var_cstr( var ) sgs_str_cstr( (var)->data.S )

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
	void*       P;
}
sgs_VarData;

struct _sgs_Variable
{
	uint32_t    type;
	sgs_VarData data;
};


#define SGS_SF_METHOD  0x01
#define SGS_SF_HASTHIS 0x02
#define SGS_SF_ABORTED 0x04
#define SGS_SF_REENTER 0x08

struct _sgs_StackFrame
{
	sgs_Variable    func;
	uint16_t*       lntable;
	const uint32_t* code;
	const uint32_t* iptr;
	const uint32_t* iend;
	const uint32_t* lptr;
	sgs_Variable*   cptr;
	const char*     nfname;
	const char*     filename;
	sgs_StackFrame* prev;
	sgs_StackFrame* next;
	sgs_StackFrame* cached;
	sgs_StkIdx argbeg;
	sgs_StkIdx argend;
	sgs_StkIdx argsfrom;
	sgs_StkIdx stkoff;
	sgs_StkIdx clsoff;
	int32_t constcount;
	int32_t errsup;
	uint8_t argcount;
	uint8_t inexp;
	uint8_t flags;
};


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
#define SGS_CONVOP_TYPEOF 0x20000
#define SGS_CONVOP_TOITER 0x30000

#define SGS_BOP_AND       0
#define SGS_BOP_OR        1
#define SGS_BOP_XOR       2
#define SGS_BOP_LSH       3
#define SGS_BOP_RSH       4


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

SGS_APIFUNC sgs_Context* sgs_ForkState( SGS_CTX, int copystate );
SGS_APIFUNC void sgs_FreeState( SGS_CTX );
SGS_APIFUNC SGSBOOL sgs_PauseState( SGS_CTX );
SGS_APIFUNC SGSBOOL sgs_ResumeStateRet( SGS_CTX, int args, int* outrvc );
SGS_APIFUNC SGSBOOL sgs_ResumeStateExp( SGS_CTX, int args, int expect );
#define sgs_ResumeState( C ) sgs_ResumeStateExp( C, 0, 0 )


#define SGS_CODE_ER 0 /* error codes */
#define SGS_CODE_VT 1 /* variable type */
#define SGS_CODE_OP 2 /* VM instruction */
SGS_APIFUNC const char* sgs_CodeString( int type, int val );


/* Core systems */
#define SGSOUTPUTFN_DEFAULT ((sgs_OutputFunc)-1)
SGS_APIFUNC void sgs_GetOutputFunc( SGS_CTX, sgs_OutputFunc* outf, void** outc );
SGS_APIFUNC void sgs_SetOutputFunc( SGS_CTX, sgs_OutputFunc func, void* ctx );
SGS_APIFUNC void sgs_Write( SGS_CTX, const void* ptr, size_t size );
SGS_APIFUNC SGSBOOL sgs_Writef( SGS_CTX, const char* what, ... );

SGS_APIFUNC void sgs_GetErrOutputFunc( SGS_CTX, sgs_OutputFunc* outf, void** outc );
SGS_APIFUNC void sgs_SetErrOutputFunc( SGS_CTX, sgs_OutputFunc func, void* ctx );
SGS_APIFUNC void sgs_ErrWrite( SGS_CTX, const void* ptr, size_t size );
SGS_APIFUNC SGSBOOL sgs_ErrWritef( SGS_CTX, const char* what, ... );

#define SGSMSGFN_DEFAULT ((sgs_MsgFunc)-1)
#define SGSMSGFN_DEFAULT_NOABORT ((sgs_MsgFunc)-2)
SGS_APIFUNC void sgs_GetMsgFunc( SGS_CTX, sgs_MsgFunc* outf, void** outc );
SGS_APIFUNC void sgs_SetMsgFunc( SGS_CTX, sgs_MsgFunc func, void* ctx );
SGS_APIFUNC SGSZERO sgs_Msg( SGS_CTX, int type, const char* what, ... );

typedef int (*sgs_ErrorOutputFunc) ( void*, const char*, ... );
#define SGS_ERRORINFO_STACK 0x01
#define SGS_ERRORINFO_ERROR 0x02
#define SGS_ERRORINFO_FULL (SGS_ERRORINFO_STACK | SGS_ERRORINFO_ERROR)
SGS_APIFUNC void sgs_WriteErrorInfo( SGS_CTX, int flags,
	sgs_ErrorOutputFunc func, void* ctx, int type, const char* msg );
SGS_APIFUNC void sgs_PushErrorInfo( SGS_CTX, int flags, int type, const char* msg );

SGS_APIFUNC SGSBOOL sgs_GetHookFunc( SGS_CTX, sgs_HookFunc* outf, void** outc );
SGS_APIFUNC void sgs_SetHookFunc( SGS_CTX, sgs_HookFunc func, void* ctx );

SGS_APIFUNC SGSBOOL sgs_GetScriptFSFunc( SGS_CTX, sgs_ScriptFSFunc* outf, void** outc );
SGS_APIFUNC void sgs_SetScriptFSFunc( SGS_CTX, sgs_ScriptFSFunc func, void* ctx );

SGS_APIFUNC void* sgs_Memory( SGS_CTX, void* ptr, size_t size );
#define sgs_Malloc( C, size ) sgs_Memory( C, NULL, size )
#define sgs_Free( C, ptr ) sgs_Memory( C, ptr, 0 )
#define sgs_Realloc sgs_Memory
/* assumes SGS_CTX: */
#define sgs_Alloc( what ) (what*) sgs_Malloc( C, sizeof(what) )
#define sgs_Alloc_n( what, cnt ) (what*) sgs_Malloc( C, sizeof(what) * cnt )
#define sgs_Alloc_a( what, app ) (what*) sgs_Malloc( C, sizeof(what) + app )
#define sgs_Dealloc( ptr ) sgs_Free( C, ptr )


SGS_APIFUNC SGSRESULT sgs_EvalBuffer( SGS_CTX, const char* buf, size_t size, int* outrvc );
SGS_APIFUNC SGSRESULT sgs_EvalFile( SGS_CTX, const char* file, int* outrvc );
SGS_APIFUNC SGSBOOL sgs_IncludeExt( SGS_CTX, const char* name, const char* searchpath );
SGS_APIFUNC SGSRESULT sgs_Compile( SGS_CTX, const char* buf, size_t size, char** outbuf, size_t* outsize );

SGS_APIFUNC SGSRESULT sgs_DumpCompiled( SGS_CTX, const char* buf, size_t size );
SGS_APIFUNC SGSRESULT sgs_Abort( SGS_CTX );
SGS_APIFUNC ptrdiff_t sgs_Stat( SGS_CTX, int type );
SGS_APIFUNC int32_t sgs_Cntl( SGS_CTX, int what, int32_t val );

SGS_APIFUNC void sgs_StackFrameInfo( SGS_CTX, sgs_StackFrame* frame, const char** name, const char** file, int* line );
SGS_APIFUNC sgs_StackFrame* sgs_GetFramePtr( SGS_CTX, int end );


#ifndef SGS_STRINGLENGTHFUNC
#define SGS_STRINGLENGTHFUNC strlen
#endif

#define sgs_ExecBuffer( C, buf, sz ) sgs_EvalBuffer( C, buf, sz, NULL )
#define sgs_ExecString( C, str ) sgs_ExecBuffer( C, str, SGS_STRINGLENGTHFUNC( str ) )
#define sgs_EvalString( C, str, outrvc ) sgs_EvalBuffer( C, str, SGS_STRINGLENGTHFUNC( str ), outrvc )
#define sgs_ExecFile( C, str ) sgs_EvalFile( C, str, NULL )
#define sgs_Include( C, str ) sgs_IncludeExt( C, str, NULL )
#define sgs_WriteStr( C, str ) sgs_Write( C, str, SGS_STRINGLENGTHFUNC( str ) )
#define sgs_ErrWriteStr( C, str ) sgs_ErrWrite( C, str, SGS_STRINGLENGTHFUNC( str ) )


/* Additional libraries */

#if !SGS_NO_STDLIB
SGS_APIFUNC SGSRESULT sgs_LoadLib_Fmt( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_LoadLib_IO( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_LoadLib_Math( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_LoadLib_OS( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_LoadLib_RE( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_LoadLib_String( SGS_CTX );
#endif

#define SGS_RC_END() { NULL, NULL }

typedef struct _sgs_RegFuncConst
{
	const char* name;
	sgs_CFunc value;
}
sgs_RegFuncConst;
SGS_APIFUNC SGSRESULT sgs_RegFuncConsts( SGS_CTX, const sgs_RegFuncConst* list, int size );
SGS_APIFUNC SGSRESULT sgs_StoreFuncConsts( SGS_CTX, sgs_StkIdx item, const sgs_RegFuncConst* list, int size );
SGS_APIFUNC SGSRESULT sgs_StoreFuncConstsP( SGS_CTX, sgs_Variable* var, const sgs_RegFuncConst* list, int size );

typedef struct _sgs_RegIntConst
{
	const char* name;
	sgs_Int value;
}
sgs_RegIntConst;
SGS_APIFUNC SGSRESULT sgs_RegIntConsts( SGS_CTX, const sgs_RegIntConst* list, int size );
SGS_APIFUNC SGSRESULT sgs_StoreIntConsts( SGS_CTX, sgs_StkIdx item, const sgs_RegIntConst* list, int size );
SGS_APIFUNC SGSRESULT sgs_StoreIntConstsP( SGS_CTX, sgs_Variable* var, const sgs_RegIntConst* list, int size );

typedef struct _sgs_RegRealConst
{
	const char* name;
	sgs_Real value;
}
sgs_RegRealConst;
SGS_APIFUNC SGSRESULT sgs_RegRealConsts( SGS_CTX, const sgs_RegRealConst* list, int size );
SGS_APIFUNC SGSRESULT sgs_StoreRealConsts( SGS_CTX, sgs_StkIdx item, const sgs_RegRealConst* list, int size );
SGS_APIFUNC SGSRESULT sgs_StoreRealConstsP( SGS_CTX, sgs_Variable* var, const sgs_RegRealConst* list, int size );

SGS_APIFUNC SGSRESULT sgs_RegisterType( SGS_CTX, const char* name, sgs_ObjInterface* iface );
SGS_APIFUNC SGSRESULT sgs_UnregisterType( SGS_CTX, const char* name );
SGS_APIFUNC sgs_ObjInterface* sgs_FindType( SGS_CTX, const char* name );

SGS_APIFUNC SGSRESULT sgs_PushInterface( SGS_CTX, sgs_CFunc igfn );
SGS_APIFUNC SGSRESULT sgs_InitInterface( SGS_CTX, sgs_Variable* var, sgs_CFunc igfn );


/* The core interface */

/*
	OBJECT CREATION
*/
SGS_APIFUNC void sgs_InitNull( sgs_Variable* out );
SGS_APIFUNC void sgs_InitBool( sgs_Variable* out, sgs_Bool value );
SGS_APIFUNC void sgs_InitInt( sgs_Variable* out, sgs_Int value );
SGS_APIFUNC void sgs_InitReal( sgs_Variable* out, sgs_Real value );
SGS_APIFUNC void sgs_InitStringBuf( SGS_CTX, sgs_Variable* out, const char* str, sgs_SizeVal size );
SGS_APIFUNC void sgs_InitString( SGS_CTX, sgs_Variable* out, const char* str );
SGS_APIFUNC void sgs_InitCFunction( sgs_Variable* out, sgs_CFunc func );
SGS_APIFUNC void sgs_InitObject( SGS_CTX, sgs_Variable* out, void* data, sgs_ObjInterface* iface );
SGS_APIFUNC void* sgs_InitObjectIPA( SGS_CTX, sgs_Variable* out, uint32_t added, sgs_ObjInterface* iface );
SGS_APIFUNC void sgs_InitPtr( sgs_Variable* out, void* ptr );
SGS_APIFUNC void sgs_InitObjectPtr( sgs_Variable* out, sgs_VarObj* obj );

SGS_APIFUNC SGSRESULT sgs_InitArray( SGS_CTX, sgs_Variable* out, sgs_SizeVal numitems );
SGS_APIFUNC SGSRESULT sgs_InitDict( SGS_CTX, sgs_Variable* out, sgs_SizeVal numitems );
SGS_APIFUNC SGSRESULT sgs_InitMap( SGS_CTX, sgs_Variable* out, sgs_SizeVal numitems );

/*
	STACK & SUB-ITEMS
*/
SGS_APIFUNC SGSONE sgs_PushNull( SGS_CTX );
SGS_APIFUNC SGSONE sgs_PushNulls( SGS_CTX, int count );
SGS_APIFUNC SGSONE sgs_PushBool( SGS_CTX, sgs_Bool value );
SGS_APIFUNC SGSONE sgs_PushInt( SGS_CTX, sgs_Int value );
SGS_APIFUNC SGSONE sgs_PushReal( SGS_CTX, sgs_Real value );
SGS_APIFUNC SGSONE sgs_PushStringBuf( SGS_CTX, const char* str, sgs_SizeVal size );
SGS_APIFUNC SGSONE sgs_PushString( SGS_CTX, const char* str );
SGS_APIFUNC SGSONE sgs_PushCFunction( SGS_CTX, sgs_CFunc func );
SGS_APIFUNC SGSONE sgs_PushObject( SGS_CTX, void* data, sgs_ObjInterface* iface );
SGS_APIFUNC void* sgs_PushObjectIPA( SGS_CTX, uint32_t added, sgs_ObjInterface* iface );
SGS_APIFUNC SGSONE sgs_PushPtr( SGS_CTX, void* ptr );
SGS_APIFUNC SGSONE sgs_PushObjectPtr( SGS_CTX, sgs_VarObj* obj );

SGS_APIFUNC SGSRESULT sgs_PushArray( SGS_CTX, sgs_SizeVal numitems );
SGS_APIFUNC SGSRESULT sgs_PushDict( SGS_CTX, sgs_SizeVal numitems );
SGS_APIFUNC SGSRESULT sgs_PushMap( SGS_CTX, sgs_SizeVal numitems );

SGS_APIFUNC SGSRESULT sgs_PushVariable( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC SGSRESULT sgs_StoreVariable( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC SGSRESULT sgs_PushItem( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC SGSRESULT sgs_StoreItem( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC SGSRESULT sgs_InsertVariable( SGS_CTX, int pos, sgs_Variable* var );

/* string generation */
SGS_APIFUNC char* sgs_PushStringAlloc( SGS_CTX, sgs_SizeVal size );
SGS_APIFUNC char* sgs_InitStringAlloc( SGS_CTX, sgs_Variable* var, sgs_SizeVal size );
SGS_APIFUNC SGSRESULT sgs_FinalizeStringAlloc( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC SGSRESULT sgs_FinalizeStringAllocP( SGS_CTX, sgs_Variable* var );

/* almost-raw access */
SGS_APIFUNC SGSRESULT sgs_GetIndexPPP( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_Variable* out, int isprop );
SGS_APIFUNC SGSRESULT sgs_SetIndexPPP( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_Variable* val, int isprop );
SGS_APIFUNC SGSRESULT sgs_PushIndexPP( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, int isprop );
SGS_APIFUNC SGSRESULT sgs_StoreIndexPP( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, int isprop );

/* intermixed stack indices */
SGS_APIFUNC SGSRESULT sgs_GetIndexIPP( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, sgs_Variable* out, int isprop );
SGS_APIFUNC SGSRESULT sgs_GetIndexPIP( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, sgs_Variable* out, int isprop );
SGS_APIFUNC SGSRESULT sgs_GetIndexIIP( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, sgs_Variable* out, int isprop );
SGS_APIFUNC SGSRESULT sgs_GetIndexPPI( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_StkIdx out, int isprop );
SGS_APIFUNC SGSRESULT sgs_GetIndexIPI( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, sgs_StkIdx out, int isprop );
SGS_APIFUNC SGSRESULT sgs_GetIndexPII( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, sgs_StkIdx out, int isprop );
SGS_APIFUNC SGSRESULT sgs_GetIndexIII( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, sgs_StkIdx out, int isprop );

SGS_APIFUNC SGSRESULT sgs_SetIndexIPP( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, sgs_Variable* val, int isprop );
SGS_APIFUNC SGSRESULT sgs_SetIndexPIP( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, sgs_Variable* val, int isprop );
SGS_APIFUNC SGSRESULT sgs_SetIndexIIP( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, sgs_Variable* val, int isprop );
SGS_APIFUNC SGSRESULT sgs_SetIndexPPI( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_StkIdx val, int isprop );
SGS_APIFUNC SGSRESULT sgs_SetIndexIPI( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, sgs_StkIdx val, int isprop );
SGS_APIFUNC SGSRESULT sgs_SetIndexPII( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, sgs_StkIdx val, int isprop );
SGS_APIFUNC SGSRESULT sgs_SetIndexIII( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, sgs_StkIdx val, int isprop );

SGS_APIFUNC SGSRESULT sgs_PushIndexIP( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, int isprop );
SGS_APIFUNC SGSRESULT sgs_PushIndexPI( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, int isprop );
SGS_APIFUNC SGSRESULT sgs_PushIndexII( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, int isprop );
SGS_APIFUNC SGSRESULT sgs_StoreIndexIP( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, int isprop );
SGS_APIFUNC SGSRESULT sgs_StoreIndexPI( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, int isprop );
SGS_APIFUNC SGSRESULT sgs_StoreIndexII( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, int isprop );

/* raw & simplified global access */
SGS_APIFUNC SGSRESULT sgs_GetGlobalPP( SGS_CTX, sgs_Variable* idx, sgs_Variable* out );
SGS_APIFUNC SGSRESULT sgs_SetGlobalPP( SGS_CTX, sgs_Variable* idx, sgs_Variable* val );

/* special case sub-variable access */
SGS_APIFUNC SGSRESULT sgs_PushProperty( SGS_CTX, sgs_StkIdx obj, const char* name );
SGS_APIFUNC SGSRESULT sgs_StoreProperty( SGS_CTX, sgs_StkIdx obj, const char* name );
SGS_APIFUNC SGSRESULT sgs_PushNumIndex( SGS_CTX, sgs_StkIdx obj, sgs_Int idx );
SGS_APIFUNC SGSRESULT sgs_StoreNumIndex( SGS_CTX, sgs_StkIdx obj, sgs_Int idx );
SGS_APIFUNC SGSRESULT sgs_PushGlobal( SGS_CTX, const char* name );
SGS_APIFUNC SGSRESULT sgs_StoreGlobal( SGS_CTX, const char* name );

SGS_APIFUNC SGSRESULT sgs_PushPropertyP( SGS_CTX, sgs_Variable* var, const char* name );
SGS_APIFUNC SGSRESULT sgs_StorePropertyP( SGS_CTX, sgs_Variable* var, const char* name );

SGS_APIFUNC void sgs_GetEnv( SGS_CTX, sgs_Variable* out );
SGS_APIFUNC SGSRESULT sgs_SetEnv( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC void sgs_PushEnv( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_StoreEnv( SGS_CTX );

SGS_APIFUNC SGSRESULT sgs_PushPath( SGS_CTX, sgs_StkIdx item, const char* path, ... );
SGS_APIFUNC SGSRESULT sgs_StorePath( SGS_CTX, sgs_StkIdx item, const char* path, ... );

SGS_APIFUNC int sgs_ArgErrorExt( SGS_CTX, int argid, int gotthis, const char* expect, const char* expfx );
SGS_APIFUNC int sgs_ArgError( SGS_CTX, int argid, int gotthis, int expect, int is_strict );

#define sgs_FuncArgError( C, argid, expect, strict ) sgs_ArgError( C, argid, 0, expect, strict )
#define sgs_MethodArgError( C, argid, expect, strict ) sgs_ArgError( C, argid, 1, expect, strict )

typedef int (*sgs_ArgCheckFunc)
(
	sgs_Context* /* ctx / SGS_CTX */,
	int /* argid */,
	va_list* /* args */,
	int /* flags */
);
#define SGS_LOADARG_STRICT 0x01
#define SGS_LOADARG_WRITE 0x02
#define SGS_LOADARG_OPTIONAL 0x04
#define SGS_LOADARG_INTSIGN 0x08
#define SGS_LOADARG_INTRANGE 0x10
#define SGS_LOADARG_INTCLAMP 0x20

SGS_APIFUNC SGSMIXED sgs_LoadArgsExtVA( SGS_CTX, int from, const char* cmd, va_list* args );
SGS_APIFUNC SGSMIXED sgs_LoadArgsExt( SGS_CTX, int from, const char* cmd, ... );
SGS_APIFUNC SGSBOOL sgs_LoadArgs( SGS_CTX, const char* cmd, ... );
SGS_APIFUNC SGSBOOL sgs_ParseMethod( SGS_CTX, sgs_ObjInterface* iface, void** ptrout,
	const char* method_name, const char* func_name );
#define SGS_PARSE_METHOD( C, iface, ptr, objname, methodname ) \
	sgs_ParseMethod( C, iface, (void**) &ptr, #objname "." #methodname, #objname "_" #methodname )

SGS_APIFUNC int sgs_ArgCheck_Object( SGS_CTX, int argid, va_list* args, int flags );

SGS_APIFUNC SGSRESULT sgs_Pop( SGS_CTX, sgs_StkIdx count );
SGS_APIFUNC SGSRESULT sgs_PopSkip( SGS_CTX, sgs_StkIdx count, sgs_StkIdx skip );

SGS_APIFUNC sgs_StkIdx sgs_StackSize( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_SetStackSize( SGS_CTX, sgs_StkIdx size );
SGS_APIFUNC SGSRESULT sgs_SetDeltaSize( SGS_CTX, sgs_StkIdx diff );
SGS_APIFUNC sgs_StkIdx sgs_AbsIndex( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC SGSBOOL sgs_IsValidIndex( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC SGSBOOL sgs_PeekStackItem( SGS_CTX, sgs_StkIdx item, sgs_Variable* out );
SGS_APIFUNC SGSBOOL sgs_GetStackItem( SGS_CTX, sgs_StkIdx item, sgs_Variable* out );
SGS_APIFUNC SGSRESULT sgs_SetStackItem( SGS_CTX, sgs_StkIdx item, sgs_Variable* val );
SGS_APIFUNC uint32_t sgs_ItemType( SGS_CTX, sgs_StkIdx item );

SGS_APIFUNC void sgs_Assign( SGS_CTX, sgs_Variable* var_to, sgs_Variable* var_from );
SGS_APIFUNC SGSRESULT sgs_ArithOp( SGS_CTX, sgs_Variable* out, sgs_Variable* A, sgs_Variable* B, int op );
SGS_APIFUNC SGSRESULT sgs_IncDec( SGS_CTX, sgs_Variable* out, sgs_Variable* A, int inc );

/*
	CLOSURES
*/
SGS_APIFUNC SGSRESULT sgs_ClPushNulls( SGS_CTX, sgs_StkIdx num );
SGS_APIFUNC SGSRESULT sgs_ClPushItem( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC SGSRESULT sgs_ClPop( SGS_CTX, sgs_StkIdx num );
SGS_APIFUNC SGSRESULT sgs_MakeClosure( SGS_CTX, sgs_Variable* func, sgs_StkIdx clcount, sgs_Variable* out );
SGS_APIFUNC SGSRESULT sgs_ClGetItem( SGS_CTX, sgs_StkIdx item, sgs_Variable* out );
SGS_APIFUNC SGSRESULT sgs_ClSetItem( SGS_CTX, sgs_StkIdx item, sgs_Variable* var );

/*
	OPERATIONS
*/
SGS_APIFUNC SGSRESULT sgs_XFCallP( SGS_CTX, sgs_Variable* callable, int args, int* outrvc, int gotthis );
#define sgs_XCallP( C, callable, args, outrvc ) sgs_XFCallP( C, callable, args, outrvc, 0 )
#define sgs_XThisCallP( C, callable, args, outrvc ) sgs_XFCallP( C, callable, args, outrvc, 1 )
SGS_APIFUNC SGSRESULT sgs_XFCall( SGS_CTX, int args, int* outrvc, int gotthis );
#define sgs_XCall( C, args, outrvc ) sgs_XFCall( C, args, outrvc, 0 )
#define sgs_XThisCall( C, args, outrvc ) sgs_XFCall( C, args, outrvc, 1 )
SGS_APIFUNC SGSRESULT sgs_FCallP( SGS_CTX, sgs_Variable* callable, int args, int expect, int gotthis );
#define sgs_CallP( C, callable, args, expect ) sgs_FCallP( C, callable, args, expect, 0 )
#define sgs_ThisCallP( C, callable, args, expect ) sgs_FCallP( C, callable, args, expect, 1 )
SGS_APIFUNC SGSRESULT sgs_FCall( SGS_CTX, int args, int expect, int gotthis );
#define sgs_Call( C, args, expect ) sgs_FCall( C, args, expect, 0 )
#define sgs_ThisCall( C, args, expect ) sgs_FCall( C, args, expect, 1 )
SGS_APIFUNC SGSRESULT sgs_GlobalCall( SGS_CTX, const char* name, int args, int expect );
SGS_APIFUNC SGSRESULT sgs_TypeOf( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC SGSRESULT sgs_DumpVar( SGS_CTX, int maxdepth );
SGS_APIFUNC SGSRESULT sgs_GCExecute( SGS_CTX );

SGS_APIFUNC const char* sgs_DebugDumpVarExt( SGS_CTX, sgs_StkIdx item, int maxdepth );
#define sgs_DebugDumpVar( C, item ) sgs_DebugDumpVarExt( C, item, 5 )
#define sgs_DebugPrintVar( C, item ) sgs_DebugDumpVarExt( C, item, -1 )
SGS_APIFUNC const char* sgs_DebugDumpVarExtP( SGS_CTX, sgs_Variable* var, int maxdepth );
#define sgs_DebugDumpVarP( C, var ) sgs_DebugDumpVarExtP( C, var, 5 )
#define sgs_DebugPrintVarP( C, var ) sgs_DebugDumpVarExtP( C, var, -1 )

SGS_APIFUNC SGSRESULT sgs_PadString( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_ToPrintSafeString( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_StringConcat( SGS_CTX, sgs_StkIdx args );
SGS_APIFUNC SGSRESULT sgs_CloneItem( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC SGSMIXED sgs_ObjectAction( SGS_CTX, sgs_StkIdx item, int act, int arg );

SGS_APIFUNC SGSRESULT sgs_Serialize( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_SerializeObject( SGS_CTX, sgs_StkIdx args, const char* func );
SGS_APIFUNC SGSRESULT sgs_Unserialize( SGS_CTX );

SGS_APIFUNC SGSRESULT sgs_SerializeV1( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_UnserializeV1( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_SerializeV2( SGS_CTX );
SGS_APIFUNC SGSRESULT sgs_UnserializeV2( SGS_CTX );

SGS_APIFUNC int sgs_Compare( SGS_CTX, sgs_Variable* v1, sgs_Variable* v2 );
SGS_APIFUNC SGSBOOL sgs_EqualTypes( sgs_Variable* v1, sgs_Variable* v2 );

/*
	CONVERSION / RETRIEVAL
*/
/* pointer versions */
SGS_APIFUNC sgs_Bool sgs_GetBoolP( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC sgs_Int sgs_GetIntP( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC sgs_Real sgs_GetRealP( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC void* sgs_GetPtrP( SGS_CTX, sgs_Variable* var );

SGS_APIFUNC sgs_Bool sgs_ToBoolP( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC sgs_Int sgs_ToIntP( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC sgs_Real sgs_ToRealP( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC void* sgs_ToPtrP( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC char* sgs_ToStringBufP( SGS_CTX, sgs_Variable* var, sgs_SizeVal* outsize );
#define sgs_ToStringP( ctx, var ) sgs_ToStringBufP( ctx, var, NULL )
SGS_APIFUNC char* sgs_ToStringBufFastP( SGS_CTX, sgs_Variable* var, sgs_SizeVal* outsize );
#define sgs_ToStringFastP( ctx, var ) sgs_ToStringBufFast( ctx, var, NULL )

SGS_APIFUNC SGSBOOL sgs_IsObjectP( sgs_Variable* var, sgs_ObjInterface* iface );
#define sgs_IsTypeP( C, var, name ) sgs_IsObjectP( var, sgs_FindType( C, name ) )
SGS_APIFUNC SGSBOOL sgs_IsCallableP( sgs_Variable* var );
SGS_APIFUNC SGSBOOL sgs_ParseBoolP( SGS_CTX, sgs_Variable* var, sgs_Bool* out );
SGS_APIFUNC SGSBOOL sgs_ParseIntP( SGS_CTX, sgs_Variable* var, sgs_Int* out );
SGS_APIFUNC SGSBOOL sgs_ParseRealP( SGS_CTX, sgs_Variable* var, sgs_Real* out );
SGS_APIFUNC SGSBOOL sgs_ParseStringP( SGS_CTX, sgs_Variable* var, char** out, sgs_SizeVal* size );
SGS_APIFUNC SGSBOOL sgs_ParseObjectPtrP( SGS_CTX, sgs_Variable* var,
	sgs_ObjInterface* iface, sgs_VarObj** out, int strict );
SGS_APIFUNC SGSBOOL sgs_ParsePtrP( SGS_CTX, sgs_Variable* var, void** out );
SGS_APIFUNC SGSMIXED sgs_ArraySizeP( SGS_CTX, sgs_Variable* var );

/* index versions */
SGS_APIFUNC sgs_Bool sgs_GetBool( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC sgs_Int sgs_GetInt( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC sgs_Real sgs_GetReal( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC void* sgs_GetPtr( SGS_CTX, sgs_StkIdx item );

SGS_APIFUNC sgs_Bool sgs_ToBool( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC sgs_Int sgs_ToInt( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC sgs_Real sgs_ToReal( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC void* sgs_ToPtr( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC char* sgs_ToStringBuf( SGS_CTX, sgs_StkIdx item, sgs_SizeVal* outsize );
#define sgs_ToString( ctx, item ) sgs_ToStringBuf( ctx, item, NULL )
SGS_APIFUNC char* sgs_ToStringBufFast( SGS_CTX, sgs_StkIdx item, sgs_SizeVal* outsize );
#define sgs_ToStringFast( ctx, item ) sgs_ToStringBufFast( ctx, item, NULL )

SGS_APIFUNC SGSBOOL sgs_IsObject( SGS_CTX, sgs_StkIdx item, sgs_ObjInterface* iface );
#define sgs_IsType( C, item, name ) sgs_IsObject( C, item, sgs_FindType( C, name ) )
SGS_APIFUNC SGSBOOL sgs_IsCallable( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC SGSBOOL sgs_ParseBool( SGS_CTX, sgs_StkIdx item, sgs_Bool* out );
SGS_APIFUNC SGSBOOL sgs_ParseInt( SGS_CTX, sgs_StkIdx item, sgs_Int* out );
SGS_APIFUNC SGSBOOL sgs_ParseReal( SGS_CTX, sgs_StkIdx item, sgs_Real* out );
SGS_APIFUNC SGSBOOL sgs_ParseString( SGS_CTX, sgs_StkIdx item, char** out, sgs_SizeVal* size );
SGS_APIFUNC SGSBOOL sgs_ParseObjectPtr( SGS_CTX, sgs_StkIdx item,
	sgs_ObjInterface* iface, sgs_VarObj** out, int strict );
SGS_APIFUNC SGSBOOL sgs_ParsePtr( SGS_CTX, sgs_StkIdx item, void** out );
SGS_APIFUNC SGSMIXED sgs_ArraySize( SGS_CTX, sgs_StkIdx item );

/* global versions */
SGS_APIFUNC sgs_Bool sgs_GlobalBool( SGS_CTX, const char* name );
SGS_APIFUNC sgs_Int sgs_GlobalInt( SGS_CTX, const char* name );
SGS_APIFUNC sgs_Real sgs_GlobalReal( SGS_CTX, const char* name );
SGS_APIFUNC char* sgs_GlobalStringBuf( SGS_CTX, const char* name, sgs_SizeVal* outsize );
#define sgs_GlobalString( C, name ) sgs_GlobalStringBuf( C, name, NULL )

/* iterator interface fns - pointer sources */
SGS_APIFUNC SGSRESULT sgs_PushIteratorP( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC SGSRESULT sgs_GetIteratorP( SGS_CTX, sgs_Variable* var, sgs_Variable* out );
SGS_APIFUNC SGSMIXED sgs_IterAdvanceP( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC SGSMIXED sgs_IterPushDataP( SGS_CTX, sgs_Variable* var, int key, int value );
SGS_APIFUNC SGSMIXED sgs_IterGetDataP( SGS_CTX, sgs_Variable* var, sgs_Variable* key, sgs_Variable* value );

/* iterator interface fns - stack sources */
SGS_APIFUNC SGSRESULT sgs_PushIterator( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC SGSRESULT sgs_GetIterator( SGS_CTX, sgs_StkIdx item, sgs_Variable* out );
SGS_APIFUNC SGSMIXED sgs_IterAdvance( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC SGSMIXED sgs_IterPushData( SGS_CTX, sgs_StkIdx item, int key, int value );
SGS_APIFUNC SGSMIXED sgs_IterGetData( SGS_CTX, sgs_StkIdx item, sgs_Variable* key, sgs_Variable* value );

SGS_APIFUNC SGSBOOL sgs_IsNumericString( const char* str, sgs_SizeVal size );

/*
	EXTENSION UTILITIES
*/
SGS_APIFUNC SGSBOOL sgs_Method( SGS_CTX );
SGS_APIFUNC SGSBOOL sgs_HideThis( SGS_CTX );
SGS_APIFUNC SGSBOOL sgs_ForceHideThis( SGS_CTX );

SGS_APIFUNC void sgs_Acquire( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC void sgs_AcquireArray( SGS_CTX, sgs_Variable* var, sgs_SizeVal count );
SGS_APIFUNC void sgs_Release( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC void sgs_ReleaseArray( SGS_CTX, sgs_Variable* var, sgs_SizeVal count );
SGS_APIFUNC SGSRESULT sgs_GCMark( SGS_CTX, sgs_Variable* var );
SGS_APIFUNC SGSRESULT sgs_GCMarkArray( SGS_CTX, sgs_Variable* var, sgs_SizeVal count );
SGS_APIFUNC void sgs_ObjAcquire( SGS_CTX, sgs_VarObj* obj );
SGS_APIFUNC void sgs_ObjRelease( SGS_CTX, sgs_VarObj* obj );
SGS_APIFUNC SGSRESULT sgs_ObjGCMark( SGS_CTX, sgs_VarObj* obj );
SGS_APIFUNC void sgs_ObjAssign( SGS_CTX, sgs_VarObj** dest, sgs_VarObj* src );
SGS_APIFUNC void sgs_ObjCallDtor( SGS_CTX, sgs_VarObj* obj );
SGS_APIFUNC void sgs_ObjSetMetaObj( SGS_CTX, sgs_VarObj* obj, sgs_VarObj* metaobj );
SGS_APIFUNC sgs_VarObj* sgs_ObjGetMetaObj( sgs_VarObj* obj );
SGS_APIFUNC void sgs_ObjSetMetaMethodEnable( sgs_VarObj* obj, SGSBOOL enable );
SGS_APIFUNC SGSBOOL sgs_ObjGetMetaMethodEnable( sgs_VarObj* obj );

SGS_APIFUNC char* sgs_GetStringPtrP( sgs_Variable* var );
SGS_APIFUNC sgs_SizeVal sgs_GetStringSizeP( sgs_Variable* var );
SGS_APIFUNC sgs_VarObj* sgs_GetObjectStructP( sgs_Variable* var );
SGS_APIFUNC void* sgs_GetObjectDataP( sgs_Variable* var );
SGS_APIFUNC sgs_ObjInterface* sgs_GetObjectIfaceP( sgs_Variable* var );
SGS_APIFUNC SGSBOOL sgs_SetObjectDataP( sgs_Variable* var, void* data );
SGS_APIFUNC SGSBOOL sgs_SetObjectIfaceP( sgs_Variable* var, sgs_ObjInterface* iface );

SGS_APIFUNC char* sgs_GetStringPtr( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC sgs_SizeVal sgs_GetStringSize( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC sgs_VarObj* sgs_GetObjectStruct( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC void* sgs_GetObjectData( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC sgs_ObjInterface* sgs_GetObjectIface( SGS_CTX, sgs_StkIdx item );
SGS_APIFUNC SGSBOOL sgs_SetObjectData( SGS_CTX, sgs_StkIdx item, void* data );
SGS_APIFUNC SGSBOOL sgs_SetObjectIface( SGS_CTX, sgs_StkIdx item, sgs_ObjInterface* iface );

SGS_APIFUNC int sgs_HasFuncName( SGS_CTX );
SGS_APIFUNC void sgs_FuncName( SGS_CTX, const char* fnliteral );

#define SGSFN( x ) sgs_FuncName( C, x )
#define SGSBASEFN( x ) if( !sgs_HasFuncName( C ) ) sgs_FuncName( C, x )

static SGS_INLINE int sgs_Errno( SGS_CTX, int clear )
{
	sgs_Cntl( C, SGS_CNTL_ERRNO, clear );
	return clear;
}
#define sgs_SetErrno( C, err ) sgs_Cntl( C, SGS_CNTL_SET_ERRNO, err )
#define sgs_GetLastErrno( C ) sgs_Cntl( C, SGS_CNTL_GET_ERRNO, 0 )


/* predefined output / messaging functions */

static SGS_INLINE void sgs_StdOutputFunc( void* userdata, SGS_CTX, const void* ptr, size_t size )
{
	SGS_UNUSED( C );
	fwrite( ptr, 1, size, (FILE*) userdata );
}

static SGS_INLINE void sgs_StdMsgFunc_NoAbort( void* ctx, SGS_CTX, int type, const char* msg )
{
	SGS_UNUSED( ctx );
	sgs_WriteErrorInfo( C, SGS_ERRORINFO_FULL, (sgs_ErrorOutputFunc) sgs_ErrWritef, C, type, msg );
}

static SGS_INLINE void sgs_StdMsgFunc( void* ctx, SGS_CTX, int type, const char* msg )
{
	sgs_StdMsgFunc_NoAbort( ctx, C, type, msg );
	if( type >= SGS_ERROR )
		sgs_Abort( C );
}

static SGS_INLINE SGSRESULT sgs_StdScriptFSFunc( void* ctx, SGS_CTX, int op, sgs_ScriptFSData* data )
{
	SGS_UNUSED( ctx );
	SGS_UNUSED( C );
	switch( op )
	{
	case SGS_SFS_FILE_EXISTS:
		{
			FILE* f = fopen( data->filename, "rb" );
			if( f )
				fclose( f );
			return f ? SGS_SUCCESS : SGS_ENOTFND;
		}
	case SGS_SFS_FILE_OPEN:
		{
			long sz;
			FILE* f = fopen( data->filename, "rb" );
			if( f == NULL )
				return SGS_ENOTFND;
			fseek( f, 0, SEEK_END );
			sz = ftell( f );
			if( sz < 0 )
			{
				fclose( f );
				return SGS_EINPROC;
			}
			data->userhandle = f;
			data->size = (size_t) sz;
			return SGS_SUCCESS;
		}
	case SGS_SFS_FILE_READ:
		fseek( (FILE*) data->userhandle, 0, SEEK_SET );
		return fread( data->output, 1, data->size, (FILE*) data->userhandle ) == data->size ? SGS_SUCCESS : SGS_EINPROC;
	case SGS_SFS_FILE_CLOSE:
		fclose( (FILE*) data->userhandle );
		data->userhandle = NULL;
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}


/* utility macros */
#define SGS_MODULE_CHECK_VERSION( C ) if( SGS_VERSION_INT != sgs_Stat( C, SGS_STAT_VERSION ) ) \
	{ sgs_Msg( C, SGS_ERROR, "SGScript version mismatch: module compiled for %06X, loaded in %06X", \
	(int) SGS_VERSION_INT, (int) sgs_Stat( C, SGS_STAT_VERSION ) ); return SGS_ENOTSUP; }

#define SGS_RETURN_THIS( C ) sgs_Method( C ); sgs_SetStackSize( C, 1 ); return 1;

#define SGS_ARGS_GETINDEXFUNC SGS_CTX, sgs_VarObj* obj, sgs_Variable* key, int isprop
#define SGS_ARGS_SETINDEXFUNC SGS_CTX, sgs_VarObj* obj, sgs_Variable* key, sgs_Variable* val, int isprop
#define SGS_BEGIN_INDEXFUNC char* str; SGS_UNUSED( isprop ); if( sgs_ParseStringP( C, key, &str, NULL ) ){
#define SGS_END_INDEXFUNC } return SGS_ENOTFND;
#define SGS_CASE( name ) if( !strcmp( str, name ) )

#define SGS_RETURN_NULL() return sgs_PushNull( C );
#define SGS_RETURN_BOOL( value ) return sgs_PushBool( C, value );
#define SGS_RETURN_INT( value ) return sgs_PushInt( C, value );
#define SGS_RETURN_REAL( value ) return sgs_PushReal( C, value );
#define SGS_RETURN_CFUNC( value ) return sgs_PushCFunction( C, value );
#define SGS_RETURN_OBJECT( value ) return sgs_PushObjectPtr( C, value );
#define SGS_RETURN_PTR( value ) return sgs_PushPtr( C, value );
#define SGS_RETURN_VAR( value ) return sgs_PushVariable( C, value );

#define SGS_PARSE_BOOL( out ) { sgs_Bool V; if( sgs_ParseBoolP( C, val, &V ) ){ out = V; return SGS_SUCCESS; } return SGS_EINVAL; }
#define SGS_PARSE_INT( out ) { sgs_Int V; if( sgs_ParseIntP( C, val, &V ) ){ out = V; return SGS_SUCCESS; } return SGS_EINVAL; }
#define SGS_PARSE_REAL( out ) { sgs_Real V; if( sgs_ParseRealP( C, val, &V ) ){ out = V; return SGS_SUCCESS; } return SGS_EINVAL; }
#define SGS_PARSE_OBJECT( iface, out, nonull ) { return sgs_ParseObjectPtrP( C, val, iface, &out, nonull ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_OBJECT_IF( iface, out, nonull, cond ) if( ( !(nonull) && val->type == SGS_VT_NULL ) || ( val->type == SGS_VT_OBJECT && (cond) ) ) \
	{ return sgs_ParseObjectPtrP( C, val, iface, &out, nonull ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_PTR( out ) { void* V; if( sgs_ParsePtrP( C, val, &V ) ){ out = V; return SGS_SUCCESS; } return SGS_EINVAL; }


#ifdef __cplusplus
}
#endif

#endif /* SGSCRIPT_H_INCLUDED */
