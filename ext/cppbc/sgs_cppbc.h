

#ifndef __SGS_CPPBC_H__
#define __SGS_CPPBC_H__

#include <sgscript.h>
#include <new>


#ifndef SGS_CPPBC_PROCESS
# define SGS_OBJECT \
	static int _sgs_convert( SGS_CTX, sgs_VarObj* obj, int param ); \
	static int _sgs_getindex( SGS_CTX, sgs_VarObj* obj, int param ); \
	static int _sgs_setindex( SGS_CTX, sgs_VarObj* obj, int param ); \
	static int _sgs_create( SGS_CTX ); \
	static sgs_ObjCallback _sgs_interface[];
# define SGS_METHOD
# define SGS_PROPERTY
# define READ
# define WRITE
#endif


template< class T > void sgs_PushVar( SGS_CTX, T );
template< class T > inline void sgs_PushVar( SGS_CTX, T* v ){ sgs_PushClass( C, v ); }
#define SGS_DECL_PUSHVAR( type, parsefn ) template<> inline void sgs_PushVar<type>( SGS_CTX, type v ){ parsefn( C, v ); }
SGS_DECL_PUSHVAR( bool, sgs_PushBool );
#define SGS_DECL_PUSHVAR_INT( type ) SGS_DECL_PUSHVAR( type, sgs_PushInt )
SGS_DECL_PUSHVAR_INT( signed char );
SGS_DECL_PUSHVAR_INT( unsigned char );
SGS_DECL_PUSHVAR_INT( signed short );
SGS_DECL_PUSHVAR_INT( unsigned short );
SGS_DECL_PUSHVAR_INT( signed int );
SGS_DECL_PUSHVAR_INT( unsigned int );
SGS_DECL_PUSHVAR_INT( signed long );
SGS_DECL_PUSHVAR_INT( unsigned long );
SGS_DECL_PUSHVAR_INT( signed long long );
SGS_DECL_PUSHVAR( float, sgs_PushReal );
SGS_DECL_PUSHVAR( double, sgs_PushReal );
SGS_DECL_PUSHVAR( const char*, sgs_PushString );
SGS_DECL_PUSHVAR( sgs_CFunc, sgs_PushCFunction );


template< class T > T sgs_GetVar( SGS_CTX, int item );
template< class T > inline T* sgs_GetVar( SGS_CTX, int item, bool unused )
{
	(void) unused;
	if( sgs_IsObject( C, item, T::_sgs_interface ) )
		return static_cast<T*>( sgs_GetObjectData( C, item ) );
	return NULL;
}
template<> inline bool sgs_GetVar<bool>( SGS_CTX, int item )
{
	sgs_Bool v;
	if( sgs_ParseBool( C, item, &v ) )
		return v;
	return false;
}
#define SGS_DECL_GETVAR_INT( type ) \
	template<> inline type sgs_GetVar<type>( SGS_CTX, int item ){ \
		sgs_Int v; \
		if( sgs_ParseInt( C, item, &v ) ) \
			return static_cast<type>( v ); \
		return false; }
SGS_DECL_GETVAR_INT( signed char );
SGS_DECL_GETVAR_INT( unsigned char );
SGS_DECL_GETVAR_INT( signed short );
SGS_DECL_GETVAR_INT( unsigned short );
SGS_DECL_GETVAR_INT( signed int );
SGS_DECL_GETVAR_INT( unsigned int );
SGS_DECL_GETVAR_INT( signed long );
SGS_DECL_GETVAR_INT( unsigned long );
SGS_DECL_GETVAR_INT( signed long long );
template<> inline float sgs_GetVar<float>( SGS_CTX, int item ){
	sgs_Real v;
	if( sgs_ParseReal( C, item, &v ) )
		return static_cast<float>( v );
	return false; }
template<> inline double sgs_GetVar<double>( SGS_CTX, int item ){
	sgs_Real v;
	if( sgs_ParseReal( C, item, &v ) )
		return static_cast<double>( v );
	return false; }


template< class T > void sgs_PushClass( SGS_CTX, T* inst )
{
	sgs_PushObject( C, inst, T::_sgs_interface );
}

template< class T > T* sgs_PushClassIPA( SGS_CTX )
{
	return static_cast<T*>( sgs_PushObjectIPA( C, sizeof(T), T::_sgs_interface ) );
}

#define SGS_PUSHCLASS( C, name, args ) \
	(new (sgs_PushClassIPA< name >( C )) name args )


#endif // __SGS_CPPBC_H__

