

#include "sgscppbctest.h"

void pushVec3( SGS_CTX, float x, float y, float z )
{
	SGS_PUSHCLASS( C, Vec3, (x,y,z) );
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
	
	printf( "\n" );
	sgs_DestroyEngine( C );
	
	return 0;
}

