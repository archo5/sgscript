

/*
	Parallel processing module

	works by creating a system for managing script execution jobs
	
	pproc_init( num_threads = null )
	- creates the job system and returns it
	- default num_threads = max(1,num_free_threads)
	
	pproc
	{
		> add_job( code )
	}
	
	ppjob
	{
		> start
		> wait
		> set( key, val )
		> get( key )
		- state
	}
*/

#include <pthread.h>


#define SGS_INTERNAL

#include <sgs_int.h>

#define IFN( x ) { sgs_PushCFunction( C, x ); return SGS_SUCCESS; }
#define STDLIB_WARN( warn ) { sgs_Printf( C, SGS_WARNING, warn ); return 0; }


#define sgsmutex_t pthread_mutex_t
#define sgsthread_t pthread_t

#define sgsthread_create( T, func, data ) pthread_create( &T, NULL, func, data )
#define sgsthread_self( toT ) toT = pthread_self()
#define sgsthread_join( T ) pthread_join( T, NULL )
#define sgsthread_equal( T1, T2 ) pthread_equal( T1, T2 )

#define sgsmutex_init( M ) pthread_mutex_init( &M, NULL )
#define sgsmutex_destroy( M ) pthread_mutex_destroy( &M )
#define sgsmutex_lock( M ) pthread_mutex_lock( &M )
#define sgsmutex_unlock( M ) pthread_mutex_unlock( &M )


#ifdef WIN32
#  include <windows.h>
#  define sgsthread_sleep( ms ) Sleep( ms )
#else

#  include <unistd.h>

static void sgsthread_sleep( int ms )
{
	if( ms >= 1000 )
	{
		sleep( ms / 1000 );
		ms %= 1000;
	}
	if( ms > 0 )
	{
		usleep( ms * 1000 );
	}
}

#endif



#define PPJOB_STATE_INIT    0
#define PPJOB_STATE_RUNNING 1
#define PPJOB_STATE_DONE    2

typedef struct _ppmapitem_t ppmapitem_t;
struct _ppmapitem_t
{
	char* data;
	sgs_SizeVal keysize;
	sgs_SizeVal datasize;
	ppmapitem_t* next;
};

typedef struct _ppjob_t
{
	volatile int state;
	
	sgsthread_t self;
	sgsthread_t thread;
	sgsmutex_t mutex;
	
	sgs_MemFunc mf;
	void* mfud;
	
	char* code;
	sgs_SizeVal codesize;
	SGS_CTX;
	
	ppmapitem_t* data;
}
ppjob_t;


static ppmapitem_t* ppjob_map_find(
	ppjob_t* job, char* key, sgs_SizeVal keysize )
{
	ppmapitem_t* item = job->data;
	while( item )
	{
		if( item->keysize == keysize && !memcmp( item->data, key, keysize ) )
			return item;
		item = item->next;
	}
	return NULL;
}

static void ppjob_map_set( ppjob_t* job,
	char* key, sgs_SizeVal keysize, char* data, sgs_SizeVal datasize )
{
	ppmapitem_t* item = ppjob_map_find( job, key, keysize );
	if( item )
	{
		char* nd = (char*) job->mf( job->mfud, NULL, keysize + datasize );
		memcpy( nd, key, keysize );
		memcpy( nd + keysize, data, datasize );
		job->mf( job->mfud, item->data, 0 );
		item->data = nd;
		item->datasize = datasize;
	}
	else
	{
		item = (ppmapitem_t*) job->mf( job->mfud, NULL, sizeof( ppmapitem_t ) );
		item->keysize = keysize;
		item->datasize = datasize;
		item->data = (char*) job->mf( job->mfud, NULL, keysize + datasize );
		memcpy( item->data, key, keysize );
		memcpy( item->data + keysize, data, datasize );
		item->next = job->data;
		job->data = item;
	}
}

static void ppjob_map_free( ppjob_t* job )
{
	ppmapitem_t* item = job->data;
	while( item )
	{
		ppmapitem_t* N = item;
		item = item->next;
		
		job->mf( job->mfud, N->data, 0 );
		job->mf( job->mfud, N, 0 );
	}
	job->data = NULL;
}


#define PPJOB_HDR ppjob_t* job = (ppjob_t*) data->data

static void* ppjob_iface[];
#define PPJOB_IHDR( name ) \
	sgs_VarObj* data; \
	ppjob_t* job; \
	if( !sgs_Method( C ) \
		|| !( sgs_IsObject( C, 0, ppjob_iface ) \
		|| sgs_IsObject( C, 0, ppjob_iface_job ) ) \
		){ sgs_Printf( C, SGS_ERROR, "ppjob." #name \
			"() isn't called on a ppjob" ); return 0; } \
	data = sgs_GetObjectData( C, 0 ); \
	job = (ppjob_t*) data->data; \
	UNUSED( job );


static void* ppjob_iface_job[];
static int pproc_sleep( SGS_CTX );
static void* ppjob_threadfunc( void* arg )
{
	ppjob_t* job = (ppjob_t*) arg;
	
	job->C = sgs_CreateEngineExt( job->mf, job->mfud );
	
	sgs_PushCFunction( job->C, pproc_sleep );
	sgs_StoreGlobal( job->C, "sleep" );
	
	sgs_PushObject( job->C, job, ppjob_iface_job );
	sgs_StoreGlobal( job->C, "_T" );
	
	sgs_ExecBuffer( job->C, job->code, job->codesize );
	
	sgs_DestroyEngine( job->C );
	
	sgsmutex_lock( job->mutex );
	job->state = PPJOB_STATE_DONE;
	sgsmutex_unlock( job->mutex );
	
	return 0;
}


static ppjob_t* ppjob_create( SGS_CTX, char* code, sgs_SizeVal codesize )
{
	sgs_MemFunc mf = C->memfunc;
	void* mfud = C->mfuserdata;
	
	ppjob_t* job = (ppjob_t*) mf( mfud, NULL, sizeof( ppjob_t ) );
	
	sgsthread_self( job->self );
	sgsthread_self( job->thread );
	sgsmutex_init( job->mutex );
	
	job->code = (char*) mf( mfud, NULL, codesize );
	memcpy( job->code, code, codesize );
	job->codesize = codesize;
	
	job->state = PPJOB_STATE_INIT;
	
	job->mf = mf;
	job->mfud = mfud;
	
	job->data = NULL;
	
	return job;
}

static void ppjob_destroy( ppjob_t* job )
{
	sgs_MemFunc mf = job->mf;
	void* mfud = job->mfud;
	/* SGS_CTX should already be destroyed here */
	mf( mfud, job->code, 0 );
	
	ppjob_map_free( job );
	
	sgsmutex_destroy( job->mutex );
	mf( mfud, job, 0 );
}

static int ppjob_start( ppjob_t* job )
{
	int ret;
	sgsmutex_lock( job->mutex );
	
	if( !sgsthread_equal( job->self, job->thread ) )
		ret = 0;
	else if( job->state == PPJOB_STATE_RUNNING )
		ret = 0;
	else
	{
		job->state = PPJOB_STATE_RUNNING;
		sgsthread_create( job->thread, ppjob_threadfunc, job );
		ret = 1;
	}
	
	sgsmutex_unlock( job->mutex );
	return ret;
}

static int ppjob_wait( ppjob_t* job )
{
	if( !sgsthread_equal( job->self, job->thread ) )
	{
		sgsthread_join( job->thread );
		return 1;
	}
	return 0;
}


static int ppjobI_start( SGS_CTX )
{
	int ssz = sgs_StackSize( C );
	PPJOB_IHDR( start );
	
	if( ssz != 0 )
		STDLIB_WARN( "ppjob.start(): unexpected arguments" )
	
	sgs_PushBool( C, ppjob_start( job ) );
	return 1;
}

static int ppjobI_wait( SGS_CTX )
{
	int ssz = sgs_StackSize( C );
	PPJOB_IHDR( wait );
	
	if( ssz != 0 )
		STDLIB_WARN( "ppjob.wait(): unexpected arguments" )
	
	sgs_PushBool( C, ppjob_wait( job ) );
	return 1;
}

static int ppjobI_has( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	int ssz = sgs_StackSize( C );
	PPJOB_IHDR( has );
	
	if( ssz != 1 ||
		!sgs_ParseString( C, 1, &str, &size ) ||
		size == 0 )
		STDLIB_WARN( "ppjob.has(): unexpected arguments; "
			"function expects 1 argument: string (size > 0)" )
	
	sgsmutex_lock( job->mutex );
	{
		ppmapitem_t* item = ppjob_map_find( job, str, size );
		sgs_PushBool( C, !!item );
	}
	sgsmutex_unlock( job->mutex );
	
	return 1;
}

static int ppjobI_get( SGS_CTX )
{
	int ret;
	char* str;
	sgs_SizeVal size;
	int ssz = sgs_StackSize( C );
	PPJOB_IHDR( get );
	
	if( ssz != 1 ||
		!sgs_ParseString( C, 1, &str, &size ) ||
		size == 0 )
		STDLIB_WARN( "ppjob.get(): unexpected arguments; "
			"function expects 1 argument: string (size > 0)" )
	
	sgsmutex_lock( job->mutex );
	{
		ppmapitem_t* item = ppjob_map_find( job, str, size );
		if( !item )
		{
			sgs_Printf( C, SGS_WARNING, "ppjob.get(): "
				"could not find item \"%.*s\"", size, str );
			ret = 0;
		}
		else
		{
			sgs_PushStringBuf( C, item->data + item->keysize, item->datasize );
			ret = sgs_Unserialize( C );
			if( ret == SGS_SUCCESS )
				ret = 1;
			else
			{
				sgs_Printf( C, SGS_WARNING, "ppjob.get(): "
					"failed to unserialize item (error %s)",
					sgs_CodeString( SGS_CODE_ER, ret ) );
				ret = 0;
			}
		}
	}
	sgsmutex_unlock( job->mutex );
	
	return ret;
}

static int ppjobI_set( SGS_CTX )
{
	char* str, *var;
	sgs_SizeVal size, varsize;
	int ssz = sgs_StackSize( C );
	PPJOB_IHDR( set );
	
	if( ssz != 2 ||
		!sgs_ParseString( C, 1, &str, &size ) ||
		size == 0 ||
		sgs_Serialize( C ) != SGS_SUCCESS ||
		!sgs_ParseString( C, -1, &var, &varsize ) )
		STDLIB_WARN( "ppjob.set(): unexpected arguments; "
			"function expects 2 arguments: string (size > 0), serializable" )
	
	sgsmutex_lock( job->mutex );
	ppjob_map_set( job, str, size, var, varsize );
	sgsmutex_unlock( job->mutex );
	
	sgs_PushBool( C, 1 );
	return 1;
}

static int ppjobI_set_if( SGS_CTX )
{
	char* str, *var, *var2;
	sgs_SizeVal size, varsize, var2size;
	int ssz = sgs_StackSize( C );
	PPJOB_IHDR( set_if );
	
#define PPJOBI_SET_IF_MSG "ppjob.set_if(): unexpected arguments; " \
			"function expects 3 arguments: string (size > 0), " \
			"serializable, serializable"
	
	if( ssz != 3 ||
		!sgs_ParseString( C, 1, &str, &size ) ||
		size == 0 )
		STDLIB_WARN( PPJOBI_SET_IF_MSG )
	
	sgs_PushItem( C, 2 );
	if( sgs_Serialize( C ) != SGS_SUCCESS ||
		!sgs_ParseString( C, -1, &var, &varsize ) )
		STDLIB_WARN( PPJOBI_SET_IF_MSG )
	
	sgs_PushItem( C, 3 );
	if( sgs_Serialize( C ) != SGS_SUCCESS ||
		!sgs_ParseString( C, -1, &var2, &var2size ) )
		STDLIB_WARN( PPJOBI_SET_IF_MSG )
	
	sgsmutex_lock( job->mutex );
	{
		ppmapitem_t* item = ppjob_map_find( job, str, size );
		if( !item )
		{
			sgs_Printf( C, SGS_WARNING, "ppjob.set_if(): "
				"could not find item \"%.*s\"", size, str );
			return 0;
		}
		else if( item->datasize == var2size && 
			memcmp( item->data + item->keysize, var2, var2size ) == 0 )
		{
			ppjob_map_set( job, str, size, var, varsize );
			sgs_PushBool( C, 1 );
		}
		else
			sgs_PushBool( C, 0 );
	}
	sgsmutex_unlock( job->mutex );
	
	return 1;
}

static int ppjobP_state( SGS_CTX, sgs_VarObj* data )
{
	int state;
	PPJOB_HDR;
	state = job->state;
	sgs_PushInt( C, state );
	return SGS_SUCCESS;
}


static int ppjob_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal size;
	
	if( !sgs_ParseString( C, 0, &str, &size ) )
		return SGS_ENOTFND;
	
	if( 0 == strcmp( str, "start" ) ) IFN( ppjobI_start )
	else if( 0 == strcmp( str, "wait" ) ) IFN( ppjobI_wait )
	
	else if( 0 == strcmp( str, "has" ) ) IFN( ppjobI_has )
	else if( 0 == strcmp( str, "get" ) ) IFN( ppjobI_get )
	else if( 0 == strcmp( str, "set" ) ) IFN( ppjobI_set )
	else if( 0 == strcmp( str, "set_if" ) ) IFN( ppjobI_set_if )
	
	else if( 0 == strcmp( str, "state" ) ) return ppjobP_state( C, data );
	
	return SGS_ENOTFND;
}

static int ppjob_destruct( SGS_CTX, sgs_VarObj* data, int prop )
{
	PPJOB_HDR;
	
	if( job->state == PPJOB_STATE_RUNNING )
	{
		sgsthread_join( job->thread );
	}
	
	ppjob_destroy( job );
	
	return SGS_SUCCESS;
}

static void* ppjob_iface[] =
{
	SOP_GETINDEX, ppjob_getindex,
	SOP_DESTRUCT, ppjob_destruct,
	SOP_END
};

static void* ppjob_iface_job[] =
{
	SOP_GETINDEX, ppjob_getindex,
	SOP_END
};

static int pproc_serialize_function( SGS_CTX,
	sgs_iFunc* func, char** out, sgs_SizeVal* outsize )
{
	int ret;
	MemBuf B = membuf_create();
	sgs_CompFunc F =
	{
		membuf_create(),
		membuf_create(),
		membuf_create(),
		func->gotthis,
		func->numargs,
	};
	
	membuf_appbuf( &F.consts, C, ((char*)(func+1)), func->instr_off );
	membuf_appbuf( &F.code, C, ((char*)(func+1)) +
		func->instr_off, func->size - func->instr_off );
	membuf_appbuf( &F.lnbuf, C, func->lineinfo,
		( func->size - func->instr_off ) / 2 );
	
	ret = sgsBC_Func2Buf( C, &F, &B );
	
	membuf_destroy( &F.consts, C );
	membuf_destroy( &F.code, C );
	membuf_destroy( &F.lnbuf, C );
	
	if( ret )
	{
		*out = B.ptr;
		*outsize = B.size;
	}
	else
	{
		membuf_destroy( &B, C );
	}
	return ret;
}

static int pprocI_add_job( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	int ssz = sgs_StackSize( C ), type;
	
	if( ssz != 1 || !( type = sgs_ItemType( C, 0 ) ) ||
		( type != SVT_FUNC && type != SVT_STRING ) ||
		( type == SVT_STRING && !sgs_ParseString( C, 0, &str, &size ) ) )
		STDLIB_WARN( "pproc.add_job(): unexpected arguments; "
			"function expects 1 argument: string|function" )
	
	if( type == SVT_FUNC )
	{
		sgs_Variable var;
		if( !sgs_GetStackItem( C, 0, &var ) ||
			!pproc_serialize_function( C, var.data.F, &str, &size ) )
			STDLIB_WARN( "pproc.add_job(): failed to serialize function" )
	}
	else
	{
		char* code;
		sgs_SizeVal codesize;
		if( sgs_Compile( C, str, size, &code, &codesize ) != SGS_SUCCESS )
			STDLIB_WARN( "pproc.add_job(): failed to compile the code" )
		str = code;
		size = codesize;
	}
	sgs_PushObject( C, ppjob_create( C, str, size ), ppjob_iface );
	sgs_Dealloc( str );
	return 1;
}


static int pproc_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal size;
	
	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		return SGS_ENOTFND;
	
	if( 0 == strcmp( str, "add_job" ) ) IFN( pprocI_add_job )
	
	return SGS_ENOTFND;
}

static int pproc_destruct( SGS_CTX, sgs_VarObj* data, int dco )
{
	return SGS_SUCCESS;
}

static void* pproc_iface[] =
{
	SOP_GETINDEX, pproc_getindex,
	SOP_DESTRUCT, pproc_destruct,
	SOP_END
};

static int pproc_create( SGS_CTX )
{
	sgs_PushObject( C, NULL, pproc_iface );
	return 1;
}

static int pproc_sleep( SGS_CTX )
{
	sgs_Integer ms;
	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseInt( C, 0, &ms ) )
		STDLIB_WARN( "sleep(): unexpected arguments; "
			"function expects 1 argument: int" )
	
	sgsthread_sleep( ms );
	return 0;
}


#ifdef SGS_COMPILE_MODULE
#  define pproc_module_entry_point sgscript_main
#endif


#ifdef WIN32
__declspec(dllexport)
#endif
int pproc_module_entry_point( SGS_CTX )
{
	sgs_PushCFunction( C, pproc_create );
	sgs_StoreGlobal( C, "pproc_create" );
	sgs_PushCFunction( C, pproc_sleep );
	sgs_StoreGlobal( C, "sleep" );
	return SGS_SUCCESS;
}
