

#include <errno.h>
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifndef _MSC_VER
#    define EADDRINUSE      100
#    define EADDRNOTAVAIL   101
#    define EAFNOSUPPORT    102
#    define EALREADY        103
#    define EBADMSG         104
#    define ECANCELED       105
#    define ECONNABORTED    106
#    define ECONNREFUSED    107
#    define ECONNRESET      108
#    define EDESTADDRREQ    109
#    define EHOSTUNREACH    110
#    define EIDRM           111
#    define EINPROGRESS     112
#    define EISCONN         113
#    define ELOOP           114
#    define EMSGSIZE        115
#    define ENETDOWN        116
#    define ENETRESET       117
#    define ENETUNREACH     118
#    define ENOBUFS         119
#    define ENODATA         120
#    define ENOLINK         121
#    define ENOMSG          122
#    define ENOPROTOOPT     123
#    define ENOSR           124
#    define ENOSTR          125
#    define ENOTCONN        126
#    define ENOTRECOVERABLE 127
#    define ENOTSOCK        128
#    define ENOTSUP         129
#    define EOPNOTSUPP      130
#    define EOTHER          131
#    define EOVERFLOW       132
#    define EOWNERDEAD      133
#    define EPROTO          134
#    define EPROTONOSUPPORT 135
#    define EPROTOTYPE      136
#    define ETIME           137
#    define ETIMEDOUT       138
#    define ETXTBSY         139
#    define EWOULDBLOCK     140
#  endif
#else
#  include <unistd.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  define closesocket close
#endif


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


#ifdef _WIN32
#  define sockerror WSAGetLastError()
#else
#  define sockerror errno
#endif


#define SCKERRVN "__socket_error"

int sockassert( SGS_CTX, int test )
{
	int err = test ? 0 : sockerror;
	sgs_PushInt( C, err );
	sgs_StoreGlobal( C, SCKERRVN );
	return test;
}

int socket_error( SGS_CTX )
{
	int astext = 0, e = 0, ssz = sgs_StackSize( C );
	SGSFN( "socket_error" );
	if( ssz < 0 || ssz > 1 ||
		( ssz >= 1 && !sgs_ParseBool( C, 0, &astext ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 0-1 arguments: [bool]" )
	
	if( sgs_PushGlobal( C, SCKERRVN ) == SGS_SUCCESS )
		e = (int) sgs_GetInt( C, -1 );
	else if( !astext )
		sgs_PushInt( C, 0 );
	
	if( !astext )
		return 1;
	
#ifdef _WIN32
	{
		char buf[ 1024 ];
		int numwr = FormatMessageA
		(
			FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e,
			MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
			buf, 1024, NULL
		);
		if( !numwr )
			STDLIB_WARN( "failed to retrieve error message" )
		sgs_PushStringBuf( C, buf, numwr );
	}
#else
	sgs_PushString( C, strerror( e ) );
#endif
	return 1;
}

#define SOCKERR sockassert( C, 1 )
#define SOCKCLEARERR sockassert( C, 0 )


static void* socket_iface[];
#define SOCK_IHDR( name ) \
	sgs_VarObj* data; \
	SGSFN( "socket." #name ); \
	if( !sgs_Method( C ) || !sgs_IsObject( C, 0, socket_iface ) ) \
		STDLIB_WARN( "not called on a socket" ) \
	data = sgs_GetObjectData( C, 0 );

#define GET_SCK ((int)(size_t)data->data)

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
	
	sgs_PushBool( C, sockassert( C, ret == 0 ) );
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
	
	sgs_PushBool( C, sockassert( C, listen( GET_SCK, (int) queuesize ) == 0 ) );
	return 1;
}

static int socketI_accept( SGS_CTX )
{
	struct sockaddr sa;
#ifdef _WIN32
	int sa_size;
#else
	unsigned int sa_size;
#endif
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
		{
			SOCKERR;
			STDLIB_WARN( "failed to accept connection" )
		}
		SOCKCLEARERR;
		sgs_PushObject( C, (void*) (size_t) S, socket_iface );
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
	sockassert( C, ret >= 0 );
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
	
	sgs_PushBool( C, sockassert( C, shutdown( GET_SCK, (int) flags ) == 0 ) );
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
	else if( type == VT_BOOL )
	{
		sgs_PushBool( C, GET_SCK != -1 );
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
	{
		SOCKERR;
		STDLIB_WARN( "failed to create socket" )
	}
	SOCKCLEARERR;
	
	sgs_PushObject( C, (void*) (size_t) S, socket_iface );
	return 1;
}


static sgs_RegFuncConst f_sock[] =
{
	{ "socket", sgs_socket },
	{ "socket_error", socket_error },
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
#ifdef _WIN32
	WSADATA wsadata;
#endif
	
	sgs_PushInt( C, 0 );
	sgs_StoreGlobal( C, SCKERRVN );
	
	ret = sgs_RegFuncConsts( C, f_sock, sizeof(f_sock) / sizeof(f_sock[0]) );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegIntConsts( C, i_sock, sizeof(i_sock) / sizeof(i_sock[0]) );
	if( ret != SGS_SUCCESS ) return ret;
	
#ifdef _WIN32
	ret = WSAStartup( MAKEWORD( 2, 0 ), &wsadata );
	if( ret != 0 )
		return SGS_EINPROC;
#endif
	
	return SGS_SUCCESS;
}
