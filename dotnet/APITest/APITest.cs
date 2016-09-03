using System;
using System.Diagnostics;
using System.Collections.Generic;
using SGScript;

namespace APITest
{
	class APITest
	{
		static int failCount = 0;
		static int testCount = 0;
		static void Main(string[] args)
		{
			Console.WriteLine( "SGS.NET API tests" );

			CreateAndDestroy();
			ArrayMem();
			Stack101();

			Console.WriteLine( "\n--- Testing finished! ---" );
			Console.WriteLine( failCount > 0 ?
				string.Format( "\n  ERROR: {0} tests failed!\n", failCount ) :
				string.Format( "\n  SUCCESS! All tests passed! ({0})\n", testCount ) );
		}

		static void Error( string err, StackTrace st )
		{
			failCount++;
			Console.Error.WriteLine( string.Format( "At {0}: {1}", st.GetFrame(1).GetFileLineNumber(), err ) );
		}
		static void Assert<T>( T a, T b )
		{
			testCount++;
			if( !EqualityComparer<T>.Default.Equals( a, b ) )
			{
				Error( string.Format( "{0} does not equal {1}", a, b ), new StackTrace(true) );
			}
		}
		static void ExpectUnreached()
		{
			testCount++;
			Error( "Exception was not thrown", new StackTrace(true) );
		}

		// TESTS
		static void CreateAndDestroy()
		{
			Engine engine = new Engine();
			engine.Release();
		}

		static void ArrayMem()
		{
			Engine engine = new Engine();
			engine.PushArray( 0 );
			engine.Release();
		}

		static void Stack101()
		{
			Engine engine = new Engine();
			Variable sgs_dummy_var = engine.NullVar();
			engine.PushNull();
			engine.Push( true );
			engine.Push( 1337 );
			engine.Push( 13.37 );
			engine.Push( "what is this" );
			engine.Push( (IntPtr) 1 );
			engine.Push( engine );
			engine.Push( sgs_dummy_var );

			Assert( engine.StackSize(), 8 );
			Assert( engine.ItemType( 0 ), VarType.Null );
			Assert( engine.ItemType( 1 ), VarType.Bool );
			Assert( engine.ItemType( 2 ), VarType.Int );
			Assert( engine.ItemType( 3 ), VarType.Real );
			Assert( engine.ItemType( 4 ), VarType.String );
			// TODO push object
			Assert( engine.ItemType( 5 ), VarType.Ptr );
			Assert( engine.ItemType( 6 ), VarType.Thread );
			Assert( engine.ItemType( 7 ), VarType.Null );

			Assert( engine.StackItem( 1 ).var.data.B, 1 );
			Assert( engine.StackItem( 2 ).var.data.I, 1337 );
			Assert( engine.StackItem( 3 ).var.data.R, 13.37 );
			// TODO validate string
			Assert( engine.StackItem( 5 ).var.data.P, (IntPtr) 1 );
			Assert( engine.StackItem( 6 ).var.data.T, engine.ctx );
			try
			{
				testCount++;
				engine.Pop( 9 );
				ExpectUnreached();
			}
			catch( SGSException e )
			{
				Assert( e.resultCode, NI.EBOUNDS );
			}
			engine.Pop( 8 );
			Assert( engine.StackSize(), 0 );
			engine.Release();
		}
	}
}
