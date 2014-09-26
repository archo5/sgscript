

#include "sgscppbctest.h"

void pushVec3( SGS_CTX, float x, float y, float z )
{
	SGS_PUSHLITECLASS( C, Vec3, (x,y,z) );
}

Account::Handle pushAccount( SGS_CTX, sgsString name )
{
	Account* acc = SGS_PUSHCLASS( C, Account, () );
	if( name.not_null() )
		acc->name = name;
	return Account::Handle( acc );
}

int main( int argc, char** argv )
{
	printf( "\n//\n/// SGScript / CPPBC test\n//\n" );
	
	SGS_CTX = sgs_CreateEngine();
	
	printf( "\n> push Vec3(1,2,3)" );
	pushVec3( C, 1, 2, 3 );
	
	printf( "\n> print object: " );
	sgs_PushItem( C, -1 );
	sgs_GlobalCall( C, "print", 1, 0 );
	
	printf( "\n> dump object: " );
	sgs_PushItem( C, -1 );
	sgs_GlobalCall( C, "printvar", 1, 0 );
	
	printf( "\n> print property 'Vec3.length': " );
	sgs_PushProperty( C, -1, "length" );
	sgs_GlobalCall( C, "print", 1, 0 );
	
	printf( "\n> print result of method 'Vec3.getLength()': " );
	sgs_PushItem( C, -1 );
	sgs_PushProperty( C, -1, "getLength" );
	sgs_ThisCall( C, 0, 1 );
	sgs_GlobalCall( C, "print", 1, 0 );
	
	printf( "\n> print object after method 'Vec3.setLength(4.5)': " );
	sgs_PushItem( C, -1 );
	sgs_PushReal( C, 4.5 );
	sgs_PushProperty( C, -2, "setLength" );
	sgs_ThisCall( C, 1, 0 );
	sgs_GlobalCall( C, "print", 1, 0 );
	
	printf( "\n> push accounts A and B: " );
	Account::Handle aA = pushAccount( C, sgsString( C, "A for 'Artist'" ) );
	Account::Handle aB = pushAccount( C, sgsString( C, "B for 'Benefactor'" ) );
	
	printf( "\n> print objects:\n" );
	{
		SGS_SCOPE;
		{
			SGS_CSCOPE( C ); // just for testing
			aA.push( C );
			aB.push( C );
			sgs_GlobalCall( C, "printlns", 2, 0 );
		}
	}
	
	printf( "\n> perform a transaction:\n" );
	{
		sgsScope scope1( C );
		aB.push( C );
		aA.push( C );
		sgs_PushReal( C, 3.74 );
		sgs_PushString( C, "EUR" );
		sgs_PushProperty( C, -3, "sendMoney" );
		sgs_ThisCall( C, 3, 1 ); // 1 `this`, 3 arguments, 1 function on stack
		sgs_GlobalCall( C, "print", 1, 0 ); // 1 argument on stack
		
		puts( scope1.is_restored() ? "\n- stack restored" : "! stack NOT RESTORED" );
		sgs_PushNull( C );
		puts( scope1.is_restored() ? "\n! stack restore state UNCHANGED" : "- stack restore state changed" );
	}
	
	printf( "\n" );
	sgs_DestroyEngine( C );
	
	return 0;
}

