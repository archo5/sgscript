

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
		> abort
		> set( key, val )
		> get( key )
		- state
	}
*/

#include <unistd.h>
#include <pthread.h>


#define SGS_INTERNAL

#include <sgscript.h>
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

#define sgsthread_sleep( ms ) usleep( ms * 1000 )



#define PPJOB_STATE_INIT    0
#define PPJOB_STATE_RUNNING 1
#define PPJOB_STATE_DONE    2

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
}
ppjob_t;

#define PPJOB_HDR ppjob_t* job = (ppjob_t*) data->data

static void* ppjob_iface[];
#define PPJOB_IHDR( name ) \
	sgs_VarObj* data; \
	ppjob_t* job; \
	if( !sgs_Method( C ) || !sgs_IsObject( C, 0, ppjob_iface ) ) \
		{ sgs_Printf( C, SGS_ERROR, "ppjob." #name "() isn't called on an array" ); return 0; } \
	data = sgs_GetObjectData( C, 0 ); \
	job = (ppjob_t*) data->data; \
	UNUSED( job );


static int pproc_sleep( SGS_CTX );
static void* ppjob_threadfunc( void* arg )
{
	ppjob_t* job = (ppjob_t*) arg;
	
	job->C = sgs_CreateEngineExt( job->mf, job->mfud );
	
	sgs_PushCFunction( job->C, pproc_sleep );
	sgs_StoreGlobal( job->C, "sleep" );
	
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
	
	return job;
}

static void ppjob_destroy( ppjob_t* job )
{
	/* SGS_CTX should already be destroyed here */
	job->mf( job->mfud, job->code, 0 );
	
	sgsmutex_destroy( job->mutex );
}

static int ppjob_start( ppjob_t* job )
{
	int ret;
	sgsmutex_lock( job->mutex );
	
	if( job->state == PPJOB_STATE_RUNNING )
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


static int ppjob_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal size;
	
	if( !sgs_ParseString( C, 0, &str, &size ) )
		return SGS_ENOTFND;
	
	if( 0 == strcmp( str, "start" ) ) IFN( ppjobI_start )
	else if( 0 == strcmp( str, "wait" ) ) IFN( ppjobI_wait )
	
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

static int pprocI_add_job( SGS_CTX )
{
	char* str;
	int ssz = sgs_StackSize( C ), size;
	
	if( ssz != 1 || !sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "pproc.add_job: unexpected arguments; "
			"function expects 1 argument: string" )
	
	sgs_PushObject( C, ppjob_create( C, str, size ), ppjob_iface );
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
