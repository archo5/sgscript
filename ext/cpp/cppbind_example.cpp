
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
		if( !sgs_LoadArgs( C, "i", &I ) )
			return 0;
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
		char* name; sgs_SizeVal namelen;
		if( !sgs_ParseStringP( C, val, &name, &namelen ) )
			return SGS_EINVAL;
		item->name.assign( name, namelen );
	)
SGS_END_GENERIC_SETINDEXFUNC;
SGS_GENERIC_DESTRUCTOR;
SGS_DEFINE_IFACE
	"Account",
	SGS_IFACE_DESTRUCT, NULL,
	SGS_IFACE_GETINDEX, SGS_IFACE_SETINDEX,
	NULL, NULL, NULL, NULL,
	NULL, NULL
SGS_DEFINE_IFACE_END;
SGS_DEFINE_EMPTY_CTORFUNC;
#undef SGS_CLASS


const char* code =
"\n\
a = Account();\n\
a.Add( 1337 );\n\
print 'reading from property: ' $ a.coins $ '\\n';\n\
a.name = 'special';\n\
printvar( a.name, a.transaction_count, a.coins );\n\
";

int main( int argc, char* argv[] )
{
	printf( "\n//\n/// SGScript / CPPBIND test\n//\n" );
	
	SGS_CTX = sgs_CreateEngine();
	
	SGS_REGISTER( Account );
	
	sgs_ExecString( C, code );
	
	sgs_DestroyEngine( C );
	
	return 0;
}

