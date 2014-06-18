

#ifndef __SGS_CPPBC_H__
#define __SGS_CPPBC_H__

#include <sgscript.h>
#include <new>
#include <assert.h>

#ifdef SGS_CPPBC_WITH_STD_STRING
#  include <string>
#endif


#ifndef SGS_CPPBC_PROCESS
# define SGS_OBJECT_LITE \
	static int _sgs_destruct( SGS_CTX, sgs_VarObj* obj ); \
	static int _sgs_convert( SGS_CTX, sgs_VarObj* obj, int type ); \
	static int _sgs_getindex( SGS_CTX, sgs_VarObj* obj, sgs_Variable* key, int isprop ); \
	static int _sgs_setindex( SGS_CTX, sgs_VarObj* obj, sgs_Variable* key, sgs_Variable* val, int isprop ); \
	static sgs_ObjInterface _sgs_interface[1];
# define SGS_OBJECT \
	SGS_OBJECT_LITE \
	sgs_VarObj* m_sgsObject; \
	SGS_CTX;
# define SGS_METHOD
# define SGS_MULTRET int
# define SGS_PROPERTY
# define SGS_PROPERTY_FUNC( funcs )
# define VARNAME
# define READ
# define WRITE
# define READ_CALLBACK
# define WRITE_CALLBACK
# define SGS_IFUNC( type ) static
# define SGS_ALIAS( func )

#define SGS_DEFAULT_LITE_OBJECT_INTERFACE( name ) \
	template<> inline void sgs_PushVar<name>( SGS_CTX, const name& v ){ sgs_PushLiteClassFrom( C, &v ); } \
	template<> struct sgs_GetVar<name> { name operator () ( SGS_CTX, sgs_StkIdx item ){ \
		if( sgs_IsObject( C, item, name::_sgs_interface ) ) return *(name*)sgs_GetObjectData( C, -1 ); return name(); }}; \
	template<> struct sgs_GetVarP<name> { name operator () ( SGS_CTX, sgs_Variable* val ){ \
		if( sgs_IsObjectP( val, name::_sgs_interface ) ) return *(name*)sgs_GetObjectDataP( val ); return name(); }};
#endif


template< class T >
class sgsMaybe /* nullable PODs */
{
public:
	sgsMaybe() : isset(false) {};
	sgsMaybe( const T& val ) : data(val), isset(true) {}
	
	void set( const T& val ){ data = val; isset = true; }
	void unset(){ isset = false; }
	
	T data;
	bool isset;
};


template< class OwningClass >
class sgsArrayIterator
{
public:
	sgsArrayIterator( OwningClass* owner ) : m_owner(owner), m_origsize(owner->size()), m_offset(-1){ sgs_ObjAcquire( m_owner->C, m_owner->m_sgsObject ); }
	sgsArrayIterator( const sgsArrayIterator& other ) : m_owner(other.m_owner), m_origsize(other.m_origsize), m_offset(other.m_offset){ sgs_ObjAcquire( m_owner->C, m_owner->m_sgsObject ); }
	const sgsArrayIterator& operator = ( const sgsArrayIterator& other )
	{
		sgs_ObjRelease( m_owner->C, m_owner->m_sgsObject );
		m_owner = other.m_owner; m_origsize = other.m_origsize; m_offset = other.m_offset;
		sgs_ObjAcquire( m_owner->C, m_owner->m_sgsObject );
	}
	~sgsArrayIterator(){ sgs_ObjRelease( m_owner->C, m_owner->m_sgsObject ); }
	
	int gcmark(){ return sgs_ObjGCMark( m_owner->C, m_owner->m_sgsObject ); }
	int convert( SGS_CTX, sgs_VarObj* obj, int to )
	{
		if( to == SGS_CONVOP_TOITER ){ sgs_PushObjectPtr( C, obj ); return SGS_SUCCESS; }
		if( to == SGS_CONVOP_CLONE ){ sgs_PushLiteClassFrom( C, this ); return SGS_SUCCESS; }
		return SGS_ENOTSUP;
	}
	int getnext( SGS_CTX, int mode )
	{
		if( m_origsize != m_owner->size() )
			return SGS_EINPROC;
		if( !mode ) // incr
		{
			m_offset++;
			return m_offset >= 0 && m_offset < m_origsize;
		}
		if( mode & SGS_GETNEXT_KEY ) sgs_PushInt( C, m_offset );
		if( mode & SGS_GETNEXT_VALUE ) sgs_PushVar( C, (*m_owner)[ m_offset ] );
		return SGS_SUCCESS;
	}
	
	static int _sgs_destruct( SGS_CTX, sgs_VarObj* obj ){ ((sgsArrayIterator*)obj->data)->~sgsArrayIterator(); return SGS_SUCCESS; }
	static int _sgs_gcmark( SGS_CTX, sgs_VarObj* obj ){ return ((sgsArrayIterator*)obj->data)->gcmark(); }
	static int _sgs_convert( SGS_CTX, sgs_VarObj* obj, int type ){ return ((sgsArrayIterator*)obj->data)->convert( C, obj, type ); }
	static int _sgs_getnext( SGS_CTX, sgs_VarObj* obj, int type ){ return ((sgsArrayIterator*)obj->data)->getnext( C, type ); }
	static sgs_ObjInterface _sgs_interface[1];
	
	OwningClass* m_owner;
	sgs_SizeVal m_origsize, m_offset;
};

template< class OwningClass >
sgs_ObjInterface sgsArrayIterator<OwningClass>::_sgs_interface[1] =
{
	OwningClass::IteratorTypeName,
	sgsArrayIterator::_sgs_destruct, sgsArrayIterator::_sgs_gcmark,
	NULL, NULL,
	sgsArrayIterator::_sgs_convert, NULL, NULL, sgsArrayIterator::_sgs_getnext,
	NULL, NULL
};


template< class T >
class sgsHandle
{
public:
	
	sgsHandle() : object(NULL), C(NULL) {};
	sgsHandle( const sgsHandle& h ) : object(h.object), C(h.C)
	{
		if( object )
			_acquire();
	}
	sgsHandle( sgs_Context* c, sgs_VarObj* obj ) : object(NULL), C(NULL)
	{
		if( obj && obj->iface == T::_sgs_interface )
		{
			object = obj;
			C = c;
			_acquire();
		}
	}
	sgsHandle( sgs_Context* c, sgs_StkIdx item ) : object(NULL), C(c)
	{
		if( sgs_IsObject( C, item, T::_sgs_interface ) )
		{
			object = sgs_GetObjectStruct( C, item );
			_acquire();
		}
	}
	sgsHandle( sgs_Context* c, sgs_Variable* var ) : object(NULL), C(c)
	{
		if( sgs_IsObjectP( var, T::_sgs_interface ) )
		{
			object = var->data.O;
			_acquire();
		}
	}
	explicit sgsHandle( T* obj ) : object(NULL), C(NULL)
	{
		if( obj )
		{
			object = obj->m_sgsObject;
			C = obj->C;
			_acquire();
		}
	}
	~sgsHandle(){ _release(); }
	
	const sgsHandle& operator = ( const sgsHandle& h )
	{
		_release();
		if( h.object && h.object->iface == T::_sgs_interface )
		{
			object = h.object;
			C = h.C;
			_acquire();
		}
		return *this;
	}
	
	operator T*(){ return object ? static_cast<T*>( object->data ) : NULL; }
	operator const T*() const { return object ? static_cast<T*>( object->data ) : NULL; }
	
	T* operator -> (){ return object ? static_cast<T*>( object->data ) : NULL; }
	const T* operator -> () const { return object ? static_cast<T*>( object->data ) : NULL; }
	
	bool operator < ( const sgsHandle& h ) const { return object < h.object; }
	bool operator == ( const sgsHandle& h ) const { return object == h.object; }
	bool operator != ( const sgsHandle& h ) const { return object != h.object; }
	
	SGSRESULT gcmark() const { if( !object ){ return SGS_SUCCESS; } return sgs_ObjGCMark( C, object ); }
	
	void push( sgs_Context* c = NULL ) const { if( C ){ c = C; assert( C ); } else { assert( c ); }
		sgs_Variable v; v.type = object ? SGS_VT_OBJECT : SGS_VT_NULL; v.data.O = object; sgs_PushVariable( c, &v ); }
	bool not_null(){ return !!object; }
	class sgsVariable get_variable();
	
	sgs_VarObj* object;
	SGS_CTX;
	
	void _acquire(){ if( object ) sgs_ObjAcquire( C, object ); }
	void _release(){ if( object ){ sgs_ObjRelease( C, object ); object = NULL; } }
};


class sgsString
{
public:
	
	sgsString() : str(NULL), C(NULL) {};
	sgsString( const sgsString& h ) : str(h.str), C(h.C) { _acquire(); }
	sgsString( sgs_Context* c, sgs_iStr* s ) : str(s), C(c) { _acquire(); }
	sgsString( sgs_Context* c, sgs_StkIdx item ) : str(NULL), C(c)
	{
		if( sgs_ParseString( C, item, NULL, NULL ) )
		{
			sgs_Variable v;
			sgs_GetStackItem( C, item, &v );
			str = v.data.S;
		}
	}
	sgsString( sgs_Context* c, sgs_Variable* var ) : str(NULL), C(c)
	{
		if( sgs_ParseStringP( C, var, NULL, NULL ) )
		{
			str = var->data.S;
			_acquire();
		}
	}
	sgsString( sgs_Context* c, const char* s, size_t sz ) : str(NULL), C(c)
	{
		assert( sz <= 0x7fffffff );
		sgs_Variable v;
		sgs_InitStringBuf( C, &v, s, (sgs_SizeVal) sz );
		str = v.data.S;
	}
	sgsString( sgs_Context* c, const char* s ) : str(NULL), C(c)
	{
		sgs_Variable v;
		sgs_InitString( C, &v, s );
		str = v.data.S;
	}
	~sgsString(){ _release(); }
	
	const sgsString& operator = ( const sgsString& s )
	{
		_release();
		if( s.str )
		{
			str = s.str;
			C = s.C;
			_acquire();
		}
		return *this;
	}
	
	const char* c_str() const { return str ? sgs_str_cstr( str ) : NULL; }
	size_t size() const { return str ? (size_t) str->size : 0; }
#ifdef SGS_CPPBC_WITH_STD_STRING
	bool get_string( std::string& out ){ if( str ){ out = std::string( sgs_str_cstr( str ), str->size ); return true; } return false; }
#endif
	
	bool operator < ( const sgsString& s ) const { return str < s.str; }
	bool operator == ( const sgsString& s ) const { return str == s.str; }
	bool operator != ( const sgsString& s ) const { return str != s.str; }
	
	void push( sgs_Context* c = NULL ) const { if( C ){ c = C; assert( C ); } else { assert( c ); }
		sgs_Variable v; v.type = str ? SGS_VT_STRING : SGS_VT_NULL; v.data.S = str; sgs_PushVariable( c, &v ); }
	bool not_null(){ return !!str; }
	class sgsVariable get_variable();
	
	sgs_iStr* str;
	SGS_CTX;
	
	void _acquire(){ if( str ){ sgs_Variable v; v.type = SGS_VT_STRING; v.data.S = str; sgs_Acquire( C, &v ); } }
	void _release(){ if( str ){ sgs_Variable v; v.type = SGS_VT_STRING; v.data.S = str; sgs_Release( C, &v ); str = NULL; } }
};


class sgsVariable
{
public:
	
	sgsVariable() : C(NULL) { var.type = SGS_VT_NULL; };
	sgsVariable( const sgsVariable& h ) : var(h.var), C(h.C)
	{
		if( h.var.type != SGS_VT_NULL )
		{
			var = h.var;
			C = h.C;
			_acquire();
		}
	}
	sgsVariable( sgs_Context* c ) : C(c) { var.type = SGS_VT_NULL; }
	sgsVariable( sgs_Context* c, sgs_StkIdx item ) : C(c)
	{
		var.type = SGS_VT_NULL;
		sgs_PeekStackItem( C, item, &var );
		_acquire();
	}
	sgsVariable( sgs_Context* c, sgs_Variable* v ) : C(c)
	{
		if( v && v->type != SGS_VT_NULL )
		{
			var = *v;
			_acquire();
		}
	}
	~sgsVariable(){ _release(); }
	
	const sgsVariable& operator = ( const sgsVariable& h )
	{
		_release();
		if( h.var.type != SGS_VT_NULL )
		{
			var = h.var;
			C = h.C;
			_acquire();
		}
		return *this;
	}
	
	bool operator < ( const sgsVariable& h ) const { return var.type < h.var.type || var.data.I < h.var.data.I; }
	bool operator == ( const sgsVariable& h ) const { return var.type == h.var.type && var.data.I == h.var.data.I; }
	bool operator != ( const sgsVariable& h ) const { return var.type != h.var.type && var.data.I != h.var.data.I; }
	
	void push( sgs_Context* c = NULL ) const { if( C ){ c = C; assert( C ); } else { assert( c ); } sgs_PushVariable( c, const_cast<sgs_Variable*>( &var ) ); }
	SGSRESULT gcmark() { if( !C ) return SGS_SUCCESS; return sgs_GCMark( C, &var ); }
	bool not_null(){ return var.type != SGS_VT_NULL; }
	bool is_object( sgs_ObjInterface* iface ){ return sgs_IsObjectP( &var, iface ); }
	template< class T > bool is_handle(){ return sgs_IsObjectP( &var, T::_sgs_interface ); }
	template< class T > T* get_object_data(){ return (T*) sgs_GetObjectDataP( &var ); }
	template< class T > sgsHandle<T> get_handle(){ return sgsHandle<T>( &var ); }
	bool is_string(){ return var.type == SGS_VT_STRING; }
	sgsString get_string(){ return is_string() ? sgsString( C, var.data.S ) : sgsString(); }
	
	sgs_Variable var;
	SGS_CTX;
	
	void _acquire(){ if( C ){ sgs_Acquire( C, &var ); } }
	void _release(){ if( C ){ sgs_Release( C, &var ); var.type = SGS_VT_NULL; } }
	
};

template<class T>
inline sgsVariable sgsHandle<T>::get_variable()
{
	sgs_Variable v;
	v.type = object ? SGS_VT_OBJECT : SGS_VT_NULL;
	v.data.O = object;
	return sgsVariable( C, &v );
}

inline sgsVariable sgsString::get_variable()
{
	sgs_Variable v;
	v.type = str ? SGS_VT_STRING : SGS_VT_NULL;
	v.data.S = str;
	return sgsVariable( C, &v );
}


/* PushVar [stack] */
template< class T > void sgs_PushVar( SGS_CTX, const T& );
template< class T > inline void sgs_PushVar( SGS_CTX, T* v ){ sgs_PushClass( C, v ); }
template< class T > inline void sgs_PushVar( SGS_CTX, sgsMaybe<T> v ){ if( v.isset ) sgs_PushVar( C, v.data ); else sgs_PushNull( C ); }
template< class T > inline void sgs_PushVar( SGS_CTX, sgsHandle<T> v ){ v.push( C ); }
template<> inline void sgs_PushVar<sgsVariable>( SGS_CTX, const sgsVariable& v ){ v.push( C ); }
#define SGS_DECL_PUSHVAR( type, parsefn ) template<> inline void sgs_PushVar<type>( SGS_CTX, const type& v ){ parsefn( C, v ); }
SGS_DECL_PUSHVAR( bool, sgs_PushBool );
#define SGS_DECL_PUSHVAR_INT( type ) template<> inline void sgs_PushVar<type>( SGS_CTX, const type& v ){ sgs_PushInt( C, (sgs_Int) v ); }
SGS_DECL_PUSHVAR_INT( signed char );
SGS_DECL_PUSHVAR_INT( unsigned char );
SGS_DECL_PUSHVAR_INT( signed short );
SGS_DECL_PUSHVAR_INT( unsigned short );
SGS_DECL_PUSHVAR_INT( signed int );
SGS_DECL_PUSHVAR_INT( unsigned int );
SGS_DECL_PUSHVAR_INT( signed long );
SGS_DECL_PUSHVAR_INT( unsigned long );
SGS_DECL_PUSHVAR_INT( signed long long );
SGS_DECL_PUSHVAR_INT( unsigned long long );
SGS_DECL_PUSHVAR( float, sgs_PushReal );
SGS_DECL_PUSHVAR( double, sgs_PushReal );
template<> inline void sgs_PushVar<sgsString>( SGS_CTX, const sgsString& v ){ v.push( C ); }
SGS_DECL_PUSHVAR( sgs_CFunc, sgs_PushCFunction );
#ifdef SGS_CPPBC_WITH_STD_STRING
template<> inline void sgs_PushVar<std::string>( SGS_CTX, const std::string& v ){ sgs_PushStringBuf( C, v.c_str(), (sgs_SizeVal) v.size() ); }
#endif


/* GETVAR [stack] */
template< class T > struct sgs_GetVar {
	T operator () ( SGS_CTX, sgs_StkIdx item );
};
template< class T > struct sgs_GetVarObj { T* operator () ( SGS_CTX, sgs_StkIdx item )
{
	if( sgs_IsObject( C, item, T::_sgs_interface ) )
		return static_cast<T*>( sgs_GetObjectData( C, item ) );
	return NULL;
}};
template<> struct sgs_GetVar<bool> { bool operator () ( SGS_CTX, sgs_StkIdx item )
{
	sgs_Bool v;
	if( sgs_ParseBool( C, item, &v ) )
		return v;
	return false;
}};
#define SGS_DECL_GETVAR_INT( type ) \
	template<> struct sgs_GetVar<type> { type operator () ( SGS_CTX, sgs_StkIdx item ){ \
		sgs_Int v; if( sgs_ParseInt( C, item, &v ) ) return (type) v; return 0; }};
SGS_DECL_GETVAR_INT( signed char );
SGS_DECL_GETVAR_INT( unsigned char );
SGS_DECL_GETVAR_INT( signed short );
SGS_DECL_GETVAR_INT( unsigned short );
SGS_DECL_GETVAR_INT( signed int );
SGS_DECL_GETVAR_INT( unsigned int );
SGS_DECL_GETVAR_INT( signed long );
SGS_DECL_GETVAR_INT( unsigned long );
SGS_DECL_GETVAR_INT( signed long long );
template<> struct sgs_GetVar<float> { float operator () ( SGS_CTX, sgs_StkIdx item ){
	sgs_Real v; if( sgs_ParseReal( C, item, &v ) ) return (float) v; return 0; }};
template<> struct sgs_GetVar<double> { double operator () ( SGS_CTX, sgs_StkIdx item ){
	sgs_Real v; if( sgs_ParseReal( C, item, &v ) ) return (double) v; return 0; }};
template<> struct sgs_GetVar<char*> { char* operator () ( SGS_CTX, sgs_StkIdx item ){
	char* str = NULL; sgs_ParseString( C, item, &str, NULL ); return str; }};
template<> struct sgs_GetVar<sgsString> { sgsString operator () ( SGS_CTX, sgs_StkIdx item ){ return sgsString( C, item ); }};
#ifdef SGS_CPPBC_WITH_STD_STRING
template<> struct sgs_GetVar<std::string> { std::string operator () ( SGS_CTX, sgs_StkIdx item ){
	char* str; sgs_SizeVal size; if( sgs_ParseString( C, item, &str, &size ) )
		return std::string( str, (size_t) size ); return std::string(); }};
#endif
template< class O >
struct sgs_GetVar< sgsMaybe<O> >
{
	sgsMaybe<O> operator () ( SGS_CTX, sgs_StkIdx item ) const
	{
		if( sgs_ItemType( C, item ) != SGS_VT_NULL )
			return sgsMaybe<O>( sgs_GetVar<O>()( C, item ) );
		return sgsMaybe<O>();
	}
};
template< class O >
struct sgs_GetVar< sgsHandle<O> > { sgsHandle<O> operator () ( SGS_CTX, sgs_StkIdx item ) const {
	return sgsHandle<O>( C, item ); } };
template<> struct sgs_GetVar< sgsVariable > { sgsVariable operator () ( SGS_CTX, sgs_StkIdx item ) const {
	return sgsVariable( C, item ); } };


/* GETVARP [pointer] */
template< class T > struct sgs_GetVarP {
	T operator () ( SGS_CTX, sgs_Variable* var );
};
template< class T > struct sgs_GetVarObjP { T* operator () ( SGS_CTX, sgs_Variable* var )
{
	if( sgs_IsObjectP( var, T::_sgs_interface ) )
		return static_cast<T*>( sgs_GetObjectDataP( var ) );
	return NULL;
}};
template<> struct sgs_GetVarP<bool> { bool operator () ( SGS_CTX, sgs_Variable* var )
{
	sgs_Bool v;
	if( sgs_ParseBoolP( C, var, &v ) )
		return v;
	return false;
}};
#define SGS_DECL_GETVARP_INT( type ) \
	template<> struct sgs_GetVarP<type> { type operator () ( SGS_CTX, sgs_Variable* var ){ \
		sgs_Int v; if( sgs_ParseIntP( C, var, &v ) ) return (type) v; return 0; }};
SGS_DECL_GETVARP_INT( signed char );
SGS_DECL_GETVARP_INT( unsigned char );
SGS_DECL_GETVARP_INT( signed short );
SGS_DECL_GETVARP_INT( unsigned short );
SGS_DECL_GETVARP_INT( signed int );
SGS_DECL_GETVARP_INT( unsigned int );
SGS_DECL_GETVARP_INT( signed long );
SGS_DECL_GETVARP_INT( unsigned long );
SGS_DECL_GETVARP_INT( signed long long );
template<> struct sgs_GetVarP<float> { float operator () ( SGS_CTX, sgs_Variable* var ){
	sgs_Real v; if( sgs_ParseRealP( C, var, &v ) ) return (float) v; return 0; }};
template<> struct sgs_GetVarP<double> { double operator () ( SGS_CTX, sgs_Variable* var ){
	sgs_Real v; if( sgs_ParseRealP( C, var, &v ) ) return (double) v; return 0; }};
template<> struct sgs_GetVarP<char*> { char* operator () ( SGS_CTX, sgs_Variable* var ){
	char* str = NULL; sgs_ParseStringP( C, var, &str, NULL ); return str; }};
template<> struct sgs_GetVarP<sgsString> { sgsString operator () ( SGS_CTX, sgs_Variable* var ){
	sgsString S; if( sgs_ParseStringP( C, var, NULL, NULL ) ) S = sgsString( C, var->data.S );
	return S; }};
#ifdef SGS_CPPBC_WITH_STD_STRING
template<> struct sgs_GetVarP<std::string> { std::string operator () ( SGS_CTX, sgs_Variable* var ){
	char* str; sgs_SizeVal size; if( sgs_ParseStringP( C, var, &str, &size ) )
		return std::string( str, (size_t) size ); return std::string(); }};
#endif
template< class O >
struct sgs_GetVarP< sgsMaybe<O> >
{
	sgsMaybe<O> operator () ( SGS_CTX, sgs_Variable* var ) const
	{
		if( var->type != SGS_VT_NULL )
			return sgsMaybe<O>( sgs_GetVarP<O>()( C, var ) );
		return sgsMaybe<O>();
	}
};
template< class O >
struct sgs_GetVarP< sgsHandle<O> > { sgsHandle<O> operator () ( SGS_CTX, sgs_Variable* var ) const {
	return sgsHandle<O>( C, var ); } };
template<> struct sgs_GetVarP< sgsVariable > { sgsVariable operator () ( SGS_CTX, sgs_Variable* var ) const {
	return sgsVariable( C, var ); } };


template< class T > void sgs_PushClass( SGS_CTX, T* inst )
{
	sgs_PushObject( C, inst, T::_sgs_interface );
	inst->m_sgsObject = sgs_GetObjectStruct( C, -1 );
	inst->C = C;
}
template< class T > T* sgs_PushClassIPA( SGS_CTX )
{
	T* data = static_cast<T*>( sgs_PushObjectIPA( C, (sgs_SizeVal) sizeof(T), T::_sgs_interface ) );
	data->m_sgsObject = sgs_GetObjectStruct( C, -1 );
	data->C = C;
	return data;
}
template< class T> T* sgs_InitPushedClass( T* inst, SGS_CTX )
{
	inst->C = C;
	inst->m_sgsObject = sgs_GetObjectStruct( C, -1 );
	return inst;
}
#define SGS_PUSHCLASS( C, name, args ) sgs_InitPushedClass(new (sgs_PushClassIPA< name >( C )) name args, C )
template< class T > T* sgs_PushClassFrom( SGS_CTX, T* inst )
{
	T* data = SGS_PUSHCLASS( C, T, ( *inst ) );
	return sgs_InitPushedClass( data, C );
}


template< class T > void sgs_PushLiteClass( SGS_CTX, T* inst ){ sgs_PushObject( C, inst, T::_sgs_interface ); }
template< class T > T* sgs_PushLiteClassIPA( SGS_CTX ){ return static_cast<T*>( sgs_PushObjectIPA( C, (sgs_SizeVal) sizeof(T), T::_sgs_interface ) ); }
#define SGS_PUSHLITECLASS( C, name, args ) (new (sgs_PushLiteClassIPA< name >( C )) name args )
template< class T > T* sgs_PushLiteClassFrom( SGS_CTX, const T* inst )
{
	T* data = SGS_PUSHLITECLASS( C, T, ( *inst ) );
	return data;
}


#endif // __SGS_CPPBC_H__

