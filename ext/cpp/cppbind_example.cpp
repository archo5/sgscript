
#include <stdio.h>
#include <string>

#include "cppbind.h"


class Account
{
public:
	int coins;
	int numtransactions;
	std::string name;
	
	Account() : coins( 0 ), numtransactions( 0 )
	{
		printf( "Created a new account!\n" );
	}
	~Account()
	{
		printf( "Deleted the account.\n" );
	}
	
	int getter_coins( SGS_CTX ){ sgs_PushInt( C, coins ); return SGS_SUCCESS; }
	
	int Add( SGS_CTX )
	{
		int orig = coins;
		sgs_Int I;
		if( sgs_StackSize( C ) != 2 ||
			!sgs_ParseInt( C, 1, &I ) )
			return sgs_Msg( C, SGS_WARNING, "unexpected arguments" );
		coins += (int) I;
		numtransactions++;
		printf( "Transaction #%04d | before=%d after=%d\n",
			numtransactions, orig, coins );
		return 0;
	}
};


#define SGS_CLASS   Account
SGS_DECLARE_IFACE;
SGS_METHOD_WRAPPER( Add );
SGS_BEGIN_GENERIC_GETINDEXFUNC
	SGS_GIF_METHOD( Add )
	SGS_GIF_GETTER( coins )
	SGS_GIF_CUSTOM( name, sgs_PushString(C,item->name.c_str()) )
	SGS_GIF_CUSTOM( transaction_count, sgs_PushInt(C,item->numtransactions) )
SGS_END_GENERIC_GETINDEXFUNC;
SGS_BEGIN_GENERIC_SETINDEXFUNC
	SGS_GIF_CUSTOM( name,
		char* str; sgs_SizeVal strlen;
		if( !sgs_ParseString( C, 1, &str, &strlen ) )
			return SGS_EINVAL;
		item->name.assign( str, strlen );
	)
SGS_END_GENERIC_SETINDEXFUNC;
SGS_GENERIC_DESTRUCTOR;
SGS_DEFINE_IFACE
	SGS_IFACE_GETINDEX,
	SGS_IFACE_SETINDEX,
	SGS_IFACE_DESTRUCT,
SGS_DEFINE_IFACE_END;
SGS_DEFINE_EMPTY_CTORFUNC;
#undef SGS_CLASS


const char* code =
"\n\
a = Account();\n\
a.Add( 1337 );\n\
print 'reading from property: ' $ a.coins $ '\\n';\n\
a.name = 'special';\n\
printvars( a.name, a.transaction_count, a.coins );\n\
";

int main( int argc, char* argv[] )
{
	printf( "CPP binding test...\n" );
	
	SGS_CTX = sgs_CreateEngine();
	
	SGS_REGISTER( Account );
	
	sgs_ExecString( C, code );
	
	sgs_DestroyEngine( C );
	
	return 0;
}

