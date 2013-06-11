

#include <winsock2.h>
#include <ws2tcpip.h>


#ifndef MSG_CONFIRM
#define MSG_CONFIRM 0
#endif
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif
#ifndef MSG_EOR
#define MSG_EOR 0
#endif
#ifndef MSG_MORE
#define MSG_MORE 0
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif


#define SGS_INTERNAL

#include <sgs_int.h>

#define DF( x ) { #x, x }
#define STREQ( a, b ) (0==strcmp(a,b))
#define IFN( x ) { sgs_PushCFunction( C, x ); return SGS_SUCCESS; }
#define STDLIB_WARN( warn ) return sgs_Printf( C, SGS_WARNING, warn );


static void* socket_iface[];
#define SOCK_IHDR( name ) \
	sgs_VarObj* data; \
	SGSFN( "socket." #name ); \
	if( !sgs_Method( C ) || !sgs_IsObject( C, 0, socket_iface ) ) \
		STDLIB_WARN( "not called on a socket" ) \
	data = sgs_GetObjectData( C, 0 );

#define GET_SCK ((int)data->data)

static int socketI_bind_port( SGS_CTX )
{
	sgs_Integer port;
	struct sockaddr_in sa;
	int ret = 0, ssz = sgs_StackSize( C );
	
	SOCK_IHDR( bind_port );
	
	if( ssz != 1 || !sgs_ParseInt( C, 1, &port ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: int" )
	
	memset( &sa, 0, sizeof(sa) );
	sa.sin_family = AF_INET;
	sa.sin_port = htons( port );
	sa.sin_addr.s_addr = htonl( INADDR_ANY );
	ret = bind( GET_SCK, (struct sockaddr*) &sa, sizeof(sa) );
	
	sgs_PushBool( C, ret == 0 );
	return 1;
}

static int socketI_listen( SGS_CTX )
{
	sgs_Integer queuesize;
	int ssz = sgs_StackSize( C );
	
	SOCK_IHDR( listen );
	
	if( ssz != 1 || !sgs_ParseInt( C, 1, &queuesize ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: int" )
	
	sgs_PushBool( C, listen( GET_SCK, (int) queuesize ) == 0 );
	return 1;
}

static int socketI_accept( SGS_CTX )
{
	struct sockaddr sa;
	int sa_size;
	int S, more = 0, ssz = sgs_StackSize( C );
	
	SOCK_IHDR( accept );
	
	if( ssz < 0 || ssz > 1 ||
		( ssz >= 1 && !sgs_ParseBool( C, 1, &more ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 0-1 arguments: [bool]" )
	
	if( !more )
	{
		S = accept( GET_SCK, NULL, NULL );
		if( S == -1 )
			STDLIB_WARN( "failed to accept connection" )
		sgs_PushObject( C, (void*) S, socket_iface );
		return 1;
	}
	else
	{
		STDLIB_WARN( "TODO" )
		S = accept( GET_SCK, &sa, &sa_size );
	}
}

static int socketI_send( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	sgs_Integer flags = 0;
	int ret, ssz = sgs_StackSize( C );
	
	SOCK_IHDR( send );
	
	if( ssz < 1 || ssz > 2 ||
		!sgs_ParseString( C, 1, &str, &size ) ||
		( ssz >= 2 && !sgs_ParseInt( C, 2, &flags ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-2 arguments: string[, int]" )
	
	ret = send( GET_SCK, str, size, (int) flags );
	if( ret < 0 )
		sgs_PushBool( C, 0 );
	else
		sgs_PushInt( C, ret );
	return 1;
}

static int socketI_shutdown( SGS_CTX )
{
	sgs_Integer flags;
	int ssz = sgs_StackSize( C );
	
	SOCK_IHDR( shutdown );
	
	if( ssz != 1 || !sgs_ParseInt( C, 1, &flags ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: int" )
	
	sgs_PushBool( C, shutdown( GET_SCK, (int) flags ) == 0 );
	return 1;
}

static int socketI_close( SGS_CTX )
{
	int ssz = sgs_StackSize( C );
	
	SOCK_IHDR( close );
	
	if( ssz != 0 )
		STDLIB_WARN( "unexpected arguments" )
	
	sgs_PushBool( C, GET_SCK != -1 && closesocket( GET_SCK ) == 0 );
	data->data = (void*) -1;
	return 1;
}

static int socket_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* name;
	if( !sgs_ParseString( C, 0, &name, NULL ) )
		return SGS_ENOTSUP;
	
	if( STREQ( name, "bind_port" ) ) IFN( socketI_bind_port )
	if( STREQ( name, "listen" ) ) IFN( socketI_listen )
	if( STREQ( name, "accept" ) ) IFN( socketI_accept )
	if( STREQ( name, "send" ) ) IFN( socketI_send )
	if( STREQ( name, "shutdown" ) ) IFN( socketI_shutdown )
	if( STREQ( name, "close" ) ) IFN( socketI_close )
	
	return SGS_ENOTFND;
}

static int socket_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	if( type == SGS_CONVOP_TOTYPE || type == VT_STRING )
	{
		sgs_PushString( C, "socket" );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int socket_destruct( SGS_CTX, sgs_VarObj* data, int dco )
{
	UNUSED( dco );
	if( GET_SCK != -1 )
		closesocket( GET_SCK );
	return SGS_SUCCESS;
}

static void* socket_iface[] =
{
	SOP_GETINDEX, socket_getindex,
	SOP_CONVERT, socket_convert,
	SOP_DESTRUCT, socket_destruct,
	SOP_END,
};

static int sgs_socket( SGS_CTX )
{
	int S;
	sgs_Integer domain, type, protocol;
	
	SGSFN( "socket" );
	
	if( sgs_StackSize( C ) != 3 ||
		!sgs_ParseInt( C, 0, &domain ) ||
		!sgs_ParseInt( C, 1, &type ) ||
		!sgs_ParseInt( C, 2, &protocol ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 3 arguments: int, int, int" )
	
	S = socket( domain, type, protocol );
	if( S < 0 )
		STDLIB_WARN( "failed to create socket" )
	
	sgs_PushObject( C, (void*) S, socket_iface );
	return 1;
}


static sgs_RegFuncConst f_sock[] =
{
	{ "socket", sgs_socket },
};

static sgs_RegIntConst i_sock[] =
{
	DF( PF_INET ),
	DF( AF_INET ), DF( AF_INET6 ), DF( AF_UNIX ),
	DF( SOCK_STREAM ), DF( SOCK_DGRAM ), DF( SOCK_SEQPACKET ), DF( SOCK_RAW ),
	DF( IPPROTO_TCP ), DF( IPPROTO_UDP ),
	
	DF( MSG_CONFIRM ), DF( MSG_DONTROUTE ), DF( MSG_DONTWAIT ), DF( MSG_EOR ),
	DF( MSG_MORE ), DF( MSG_NOSIGNAL ), DF( MSG_OOB ),
	
	{ "SHUT_RD", 0 }, { "SHUT_WR", 1 }, { "SHUT_RDWR", 2 },
	{ "SD_RECEIVE", 0 }, { "SD_SEND", 1 }, { "SD_BOTH", 2 },
};


#ifdef SGS_COMPILE_MODULE
#  define sockets_module_entry_point sgscript_main
#endif


#ifdef WIN32
__declspec(dllexport)
#endif
int sockets_module_entry_point( SGS_CTX )
{
	int ret;
	WSADATA wsadata;
	
	ret = sgs_RegFuncConsts( C, f_sock, sizeof(f_sock) / sizeof(f_sock[0]) );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegIntConsts( C, i_sock, sizeof(i_sock) / sizeof(i_sock[0]) );
	if( ret != SGS_SUCCESS ) return ret;
	
#ifdef WIN32
	ret = WSAStartup( 0x0202, &wsadata );
	if( ret != 0 )
		return SGS_EINPROC;
#endif
	
	return SGS_SUCCESS;
}
