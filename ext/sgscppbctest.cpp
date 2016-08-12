

#include "sgscppbctest.h"



const char* outfile = "cppbctests-output.log";
const char* outfile_errors = "cppbctests-errors.log";

#include "sgsapitest_core.h"



Account::Handle pushAccount( SGS_CTX, sgsString name )
{
	Account* acc = SGS_CREATECLASS( C, NULL, Account, () );
	if( name.not_null() )
		acc->name = name;
	return Account::Handle( acc );
}

static void test_core_features()
{
	printf("> ");
	puts( testname = "core feature tests" );
	SGS_CTX = get_context();
	{
		sgsMaybe<int> mbnt = sgsMaybeNot;
		SGS_UNUSED( mbnt );
		
		sgsString sa, sb, sc( C, "test" );
		atf_assert( sa == sb );
		atf_assert( !( sa != sb ) );
		atf_assert( sa.equals( "" ) );
		atf_assert( !sa.equals( "c" ) );
		atf_assert( sa.same_as( sb ) );
		atf_assert( !sa.same_as( sc ) );
		atf_assert( !sa.not_null() );
		atf_assert( sc.not_null() );
		
		sgsVariable va, vb;
		atf_assert( va == vb );
		
		atf_check_errors( "" );
	}
	destroy_context( C );
}

static void test_object_vec3()
{
	printf("> ");
	puts( testname = "object test - vec3" );
	SGS_CTX = get_context_( REDIR_BUF );
	{
		atf_assert( sgs_StackSize( C ) == 0 );
		Vec3* obj = SGS_CREATELITECLASS( C, NULL, Vec3, (1,2,3) );
		atf_assert( sgs_StackSize( C ) == 1 );
		atf_assert( sgs_IsObject( C, 0, Vec3::_sgs_interface ) );
		
		// print
		atf_clear_output();
		sgs_PushItem( C, -1 );
		sgs_GlobalCall( C, "print", 1, 0 );
		atf_check_output( "Vec3(1;2;3)" );
		// dump
		atf_clear_output();
		sgs_PushItem( C, -1 );
		sgs_GlobalCall( C, "printvar", 1, 0 );
		{
			char compstr[ 256 ];
			sprintf( compstr, "Vec3 (%p) \n{\n"
				"  x = real (%g)\n"
				"  y = real (%g)\n"
				"  z = real (%g)\n"
				"  length = real (%g)\n}\n",
				obj, obj->x, obj->y, obj->z, obj->getLength() );
			atf_check_output( compstr );
		}
		// print .length
		sgs_PushProperty( C, sgs_StackItem( C, -1 ), "length" );
		sgs_GlobalCall( C, "print", 1, 0 );
		// print .getLength()
		sgs_PushProperty( C, sgs_StackItem( C, -1 ), "getLength" );
		sgs_PushItem( C, -2 );
		sgs_ThisCall( C, 0, 1 );
		sgs_GlobalCall( C, "print", 1, 0 );
		// call .setLength(4.5), print
		sgs_PushProperty( C, sgs_StackItem( C, -1 ), "setLength" );
		sgs_PushItem( C, -2 );
		sgs_PushReal( C, 4.5 );
		sgs_ThisCall( C, 1, 0 );
		sgs_GlobalCall( C, "print", 1, 0 );
	}
	destroy_context( C );
}

static void test_object_account()
{
	printf("> ");
	puts( testname = "object test - account" );
	SGS_CTX = get_context();
	{
		Account::Handle aA = pushAccount( C, sgsString( C, "A for 'Artist'" ) );
		Account::Handle aB = pushAccount( C, sgsString( C, "B for 'Benefactor'" ) );
		
		// print objects
		{
			SGS_SCOPE;
			{
				SGS_CSCOPE( C ); // just for testing
				sgsEnv( C ).getprop( "printlns" ).tcall<void>( C, aA, aB );
			}
		}
		
		// dump objects
		{
			SGS_SCOPE;
			sgsEnv( C ).getprop( "printvar" ).tcall<void>( C, aA, aB );
		}
		
		// perform a transaction
		{
			sgsScope scope1( C );
			sgs_PushProperty( C, aB.get_variable().var, "sendMoney" );
			aB.push( C );
			aA.push( C );
			sgs_PushReal( C, 3.74 );
			sgs_PushString( C, "EUR" );
			sgs_ThisCall( C, 3, 1 ); // 1 `this`, 3 arguments, 1 function on stack
			sgs_GlobalCall( C, "print", 1, 0 ); // 1 argument on stack
			
			atf_assert( scope1.is_restored() );
			sgs_PushNull( C );
			atf_assert( !scope1.is_restored() );
		}
		
		// -- additional tests --
		// validate/source
		{
			SGS_SCOPE;
			sgsVariable vA = aA;
			atf_assert( vA.getprop( "attachedName" ).not_null() == false );
			// ..attaching B to A
			vA.setprop( "attached", aB );
			sgsString name = vA.getprop( "attachedName" ).get<sgsString>();
			atf_assert( name.c_str() );
			// ..detaching B from A
			vA.setprop( "attached", sgsVariable() );
		}
		// context-awareness
		{
			SGS_SCOPE;
			sgs_PushProperty( C, aA.get_variable().var, "coroAware" );
			aA.push( C );
			sgs_PushInt( C, 10 );
			sgs_PushInt( C, 20 );
			sgs_PushInt( C, 30 );
			sgs_ThisCall( C, 3, 1 ); // 1 `this`, 3 arguments, 1 function on stack
			int val = (int) sgs_GetInt( C, -1 );
			atf_assert( val == 62 );
		}
		
		// free handles before destroying the engine to destroy the objects
		aA = Account::Handle();
		aB = Account::Handle();
	}
	destroy_context( C );
}

static void test_object_xref()
{
	printf("> ");
	puts( testname = "object test - xref" );
	SGS_CTX = get_context();
	puts( "- 2 xref'd objects" );
	{
		atf_assert( sgs_StackSize( C ) == 0 );
		XRef* ra = SGS_CREATECLASS( C, NULL, XRef, () );
		XRef* rb = SGS_CREATECLASS( C, NULL, XRef, () );
		rb->other = ra;
		ra->other = rb;
		atf_assert( sgs_StackSize( C ) == 2 );
		atf_assert( sgs_GetObjectStruct( C, 0 )->refcount == 2 );
		atf_assert( sgs_GetObjectStruct( C, 1 )->refcount == 2 );
		sgs_Pop( C, 2 );
		atf_assert( sgs_StackSize( C ) == 0 );
		sgs_GCExecute( C );
	}
	puts( "- 3 xref'd [2x] objects" );
	{
		atf_assert( sgs_StackSize( C ) == 0 );
		XRef* ra = SGS_CREATECLASS( C, NULL, XRef, () );
		XRef* rb = SGS_CREATECLASS( C, NULL, XRef, () );
		XRef* rc = SGS_CREATECLASS( C, NULL, XRef, () );
		ra->other = rb; ra->other2 = rc;
		rb->other = rc; rb->other2 = ra;
		rc->other = ra; rc->other2 = rb;
		atf_assert( sgs_StackSize( C ) == 3 );
		atf_assert( sgs_GetObjectStruct( C, 0 )->refcount == 3 );
		atf_assert( sgs_GetObjectStruct( C, 1 )->refcount == 3 );
		atf_assert( sgs_GetObjectStruct( C, 2 )->refcount == 3 );
		sgs_Pop( C, 3 );
		atf_assert( sgs_StackSize( C ) == 0 );
		sgs_GCExecute( C );
	}
	puts( "- 3 xref'd [1x] objects" );
	{
		atf_assert( sgs_StackSize( C ) == 0 );
		XRef* ra = SGS_CREATECLASS( C, NULL, XRef, () );
		XRef* rb = SGS_CREATECLASS( C, NULL, XRef, () );
		XRef* rc = SGS_CREATECLASS( C, NULL, XRef, () );
		ra->other = rb;
		rb->other = rc;
		rc->other = ra;
		atf_assert( sgs_StackSize( C ) == 3 );
		atf_assert( sgs_GetObjectStruct( C, 0 )->refcount == 2 );
		atf_assert( sgs_GetObjectStruct( C, 1 )->refcount == 2 );
		atf_assert( sgs_GetObjectStruct( C, 2 )->refcount == 2 );
		sgs_Pop( C, 3 );
		atf_assert( sgs_StackSize( C ) == 0 );
		sgs_GCExecute( C );
	}
	destroy_context( C );
}

int main( int argc, char** argv )
{
	printf( "\n//\n/// SGScript / CPPBC test\n//\n" );
	
	fclose( fopen( outfile, "w" ) );
	fclose( fopen( outfile_errors, "w" ) );
	
	test_core_features();
	test_object_vec3();
	test_object_account();
	test_object_xref();
	
	puts( "SUCCESS!" );
	
	return 0;
}

