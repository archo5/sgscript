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
			StackInsert();
			StackArrayDict();
			StackPush();
			StackPropIndex();
			StackNegIdx();
			Indexing();
			Globals101();
			Libraries();
			FunctionCalls();
			ComplexGC();

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
		static void AssertN<T>( T a, T b )
		{
			testCount++;
			if( EqualityComparer<T>.Default.Equals( a, b ) )
			{
				Error( string.Format( "{0} equals {1}", a, b ), new StackTrace(true) );
			}
		}
		static void AssertC( bool cond, string test )
		{
			testCount++;
			if( !cond )
			{
				Error( string.Format( "Assertion failed: \"{0}\"", test ), new StackTrace(true) );
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
			engine.PushArray( 0 );
			engine.Push( (IntPtr) 1 );
			engine.Push( engine );
			engine.Push( sgs_dummy_var );

			Assert( engine.StackSize(), 9 );
			Assert( engine.ItemType( 0 ), VarType.Null );
			Assert( engine.ItemType( 1 ), VarType.Bool );
			Assert( engine.ItemType( 2 ), VarType.Int );
			Assert( engine.ItemType( 3 ), VarType.Real );
			Assert( engine.ItemType( 4 ), VarType.String );
			Assert( engine.ItemType( 5 ), VarType.Object );
			Assert( engine.ItemType( 6 ), VarType.Ptr );
			Assert( engine.ItemType( 7 ), VarType.Thread );
			Assert( engine.ItemType( 8 ), VarType.Null );

			Assert( engine.StackItem( 1 ).var.data.B, 1 );
			Assert( engine.StackItem( 2 ).var.data.I, 1337 );
			Assert( engine.StackItem( 3 ).var.data.R, 13.37 );
			Assert( NI.GetString( engine.StackItem( 4 ).var ), "what is this" );
			Assert( engine.StackItem( 6 ).var.data.P, (IntPtr) 1 );
			Assert( engine.StackItem( 7 ).var.data.T, engine.ctx );
			try
			{
				testCount++;
				engine.Pop( 10 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			engine.Pop( 9 );
			Assert( engine.StackSize(), 0 );
			engine.Release();
		}

		static void StackInsert()
		{
			Engine engine = new Engine();
			Variable sgs_dummy_var = engine.NullVar();
			engine.Push( 1 );
			engine.Push( 2 );
			Assert( engine.StackSize(), 2 );

			try
			{
				testCount++;
				engine.InsertVar( 3, sgs_dummy_var );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			
			try
			{
				testCount++;
				engine.InsertVar( -4, sgs_dummy_var );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }

			engine.InsertVar( -3, sgs_dummy_var );
			engine.InsertVar( 3, sgs_dummy_var );
			engine.InsertVar( 2, sgs_dummy_var );
			
			Assert( engine.ItemType( 0 ), VarType.Null );
			Assert( engine.ItemType( 1 ), VarType.Int );
			Assert( engine.ItemType( 2 ), VarType.Null );
			Assert( engine.ItemType( 3 ), VarType.Int );
			Assert( engine.ItemType( 4 ), VarType.Null );
			
			engine.Pop( 5 );
			Assert( engine.StackSize(), 0 );

			engine.Release();
		}

		static void StackArrayDict()
		{
			Engine engine = new Engine();

			engine.PushNull();
			engine.Push( "key-one" );
			engine.Push( 5 );
			engine.Push( "key-two" );

			try
			{
				testCount++;
				engine.PushArray( 5 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			engine.PushArray( 0 );
			engine.Pop( 1 );

			engine.PushNull();
			engine.Push( "key-one" );
			engine.Push( 5 );
			engine.Push( "key-two" );

			engine.PushArray( 4 );
			Assert( engine.StackSize(), 5 );

			try
			{
				testCount++;
				engine.PushDict( 6 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			try
			{
				testCount++;
				engine.PushDict( 5 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EINVAL ); }
			engine.PushDict( 0 );
			engine.Pop( 1 );
			engine.PushDict( 4 );
			
			Assert( engine.StackSize(), 2 );
			engine.Pop( 2 );
			Assert( engine.StackSize(), 0 );

			engine.Release();
		}

		static void StackPush()
		{
			Engine engine = new Engine();

			engine.PushNull();
			engine.Push( 5 );

			try
			{
				testCount++;
				engine.PushItem( 2 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			Assert( engine.StackSize(), 2 );
			
			try
			{
				testCount++;
				engine.PushItem( -3 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			Assert( engine.StackSize(), 2 );

			engine.PushItem( -2 );
			Assert( engine.StackSize(), 3 );
			engine.Pop( 1 );

			engine.PushItem( 1 );
			Assert( engine.StackSize(), 3 );
			engine.Pop( 1 );

			Assert( engine.ItemType( 1 ), VarType.Int );
			engine.Pop( 2 );
			Assert( engine.StackSize(), 0 );
			
			engine.Release();
		}

		static void StackPropIndex()
		{
			Engine engine = new Engine();

			engine.Push( "key-one" );
			Assert( engine.PushIndex( engine.StackItem( 0 ), engine.StackItem( 0 ), false ), false );
			engine.Pop( 1 );

			engine.Push( 15 );
			Assert( engine.PushIndex( engine.StackItem( -2 ), engine.StackItem( -1 ), false ), false );
			engine.Pop( 2 );
			
			engine.Push( 5 );
			Assert( engine.PushIndex( engine.StackItem( -2 ), engine.StackItem( -1 ), false ), true );
			engine.Pop( 1 );
			
			engine.Push( "key-two" );
			engine.PushNull();
			engine.PushDict( 4 );
			Assert( engine.ItemType( -1 ), VarType.Object );
			Assert( engine.StackSize(), 1 );

			engine.Release();
		}

		static void StackNegIdx()
		{
			Engine engine = new Engine();

			engine.Push( "test" );
			engine.Push( 42 );
			engine.PushDict( 2 );
			Assert( engine.ItemType( -1 ), VarType.Object );
			Assert( engine.PushProperty( engine.StackItem( -1 ), "test" ), true );
			Assert( engine.StackItem( -1 ).GetInt(), 42 );
			engine.Pop( 1 );
			Assert( engine.StackSize(), 1 );
			
			engine.Release();
		}

		static void Indexing()
		{
			Engine engine = new Engine();

			engine.PushDict( 0 );
			Assert( engine.StackSize(), 1 );
			Assert( engine.ItemType( -1 ), VarType.Object );
			engine.Push( "test" );
			Assert( engine.StackSize(), 2 );

			Assert( engine.SetIndex( engine.StackItem( 0 ), engine.StackItem( 1 ), engine.StackItem( 1 ), false ), true );
			Variable var = engine.GetIndex( engine.StackItem( 0 ), engine.StackItem( 1 ), false );
			AssertN( var, null );
			Assert( var.type, VarType.String );
			
			engine.Release();
		}

		static void Globals101()
		{
			Engine engine = new Engine();

			Assert( engine.PushGlobal( "array" ), true );
			Assert( engine.StackSize(), 1 );
			Assert( engine.PushGlobal( "donut_remover" ), false );
			Assert( engine.StackSize(), 2 ); // null should be pushed as a result of not being able to find the global variable

			engine.PushArray( 1 );
			Assert( engine.ItemType( -1 ), VarType.Object );
			Assert( engine.StackSize(), 2 );
			engine.SetGlobal( "yarra", engine.StackItem( -1 ) );
			Assert( engine.StackSize(), 2 );

			engine.Release();
		}

		static void Libraries()
		{
			Engine engine = new Engine();

			engine.LoadLib_Fmt();
			engine.LoadLib_IO();
			engine.LoadLib_Math();
			engine.LoadLib_OS();
			engine.LoadLib_RE();
			engine.LoadLib_String();

			Assert( engine.PushGlobal( "fmt_text" ), true );
			Assert( engine.PushGlobal( "io_file" ), true );
			Assert( engine.PushGlobal( "pow" ), true );
			Assert( engine.PushGlobal( "os_date_string" ), true );
			Assert( engine.PushGlobal( "re_replace" ), true );
			Assert( engine.PushGlobal( "string_replace" ), true );

			engine.Release();
		}

		static void FunctionCalls()
		{
			Engine engine = new Engine();

			Assert( engine.PushGlobal( "array" ), true );
			try
			{
				testCount++;
				engine.FCall( 5, 1, false );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			Assert( engine.StackSize(), 1 );
			try
			{
				testCount++;
				engine.FCall( 1, 0, false );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			engine.FCall( 0, 0, false );

			Assert( engine.PushGlobal( "array" ), true ); // needed for future tests
			Assert( engine.PushGlobal( "array" ), true );
			engine.FCall( 0, 1 );
			Assert( engine.StackSize(), 2 );
			Assert( engine.ItemType( -1 ), VarType.Object );

			try
			{
				testCount++;
				engine.FCall( 1, 0, true );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			engine.FCall( 0, 0, true );
			Assert( engine.StackSize(), 0 );
			
			engine.Release();
		}

		static void ComplexGC()
		{
			Engine engine = new Engine();

			string str =
			"o = { a = [ { b = {}, c = 5 }, { d = { e = {} }, f = [] } ], g = [] };" +
			"o.a[0].parent = o; o.a[1].d.parent = o; o.a[1].d.e.parent = o.a[1].d;" +
			"o.a[1].f.push(o); o.g.push( o.a[1].f ); o.a.push( o.a );";

			int objcount = engine.Stat( Stat.ObjCount ).ToInt32();
			engine.Exec( str );
			AssertC( engine.Stat( Stat.ObjCount ).ToInt32() > objcount, "new objcount > old objcount" );
			engine.GCExecute();
			Assert( engine.Stat( Stat.ObjCount ).ToInt32(), objcount );
			
			engine.Release();
		}
	}
}
