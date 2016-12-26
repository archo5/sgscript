using System;
using System.Diagnostics;
using System.Collections.Generic;
using System.Reflection;
using System.Threading;
using SGScript;

namespace APITest
{
	public class APITest
	{
		static bool keepCollecting = true;
		static void GCNagThread()
		{
			while( keepCollecting )
			{
				Thread.Sleep( 20 );
				GC.Collect();
			}
		}

		[System.Runtime.InteropServices.DllImport( "user32.dll" )]
		public static extern short GetAsyncKeyState( int vKey );

		static int failCount = 0;
		static int testCount = 0;
		static void Main(string[] args)
		{
			bool gctest = false;

			for( int i = 0; i < args.Length; ++i )
			{
				if( args[ i ] == "-gctest" )
					gctest = true;
				else if( args[ i ] == "-dump_ovrnum" )
				{
					DumpOverloadNumbers( null );
					return;
				}
				else if( args[ i ].StartsWith( "-dump_ovrnum=" ) )
				{
					DumpOverloadNumbers( args[ i ].Substring( "-dump_ovrnum=".Length ) );
					return;
				}
			}

			Console.WriteLine( "SGS.NET API tests" );

			Thread t = null;
			if( gctest )
			{
				t = new Thread( new ThreadStart( GCNagThread ) );
				t.Name = "GC nagging thread";
				t.Start();
			}

			int count = 0;
			do
			{
				count++;
				Console.WriteLine( "\n\nTEST #" + count + " ---" );
				RunAllTests();
				if( ( GetAsyncKeyState( 0x11 ) & 0x8000 ) != 0 && // VK_CONTROL
					( GetAsyncKeyState( 0x10 ) & 0x8000 ) != 0 && // VK_SHIFT
					( GetAsyncKeyState( 0x12 ) & 0x8000 ) != 0 && // VK_MENU (Alt)
					( GetAsyncKeyState( 0x74 ) & 0x8000 ) != 0 ) // VK_F5
				{
					Console.WriteLine( "Test stopped" );
					break;
				}
			}
			while( gctest );

			if( gctest )
			{
				keepCollecting = false;
				t.Join();
			}

			Console.WriteLine( "\n\n--- Testing finished! ---" );
			Console.WriteLine( failCount > 0 ?
				string.Format( "\n  ERROR: {0} tests failed!\n", failCount ) :
				string.Format( "\n  SUCCESS! All tests passed! ({0})\n", testCount ) );
		}

		static void DumpOverloadNumbers( string typePrefix, Assembly asm )
		{
			foreach( var type in asm.GetTypes() )
			{
				if( typePrefix != null && type.FullName.StartsWith( typePrefix ) == false )
					continue;
				foreach( var mi in type.GetMethods() )
				{
					try
					{
						type.GetMethod( mi.Name );
					}
					catch( AmbiguousMatchException )
					{
						// ... type.GetMethod will throw AmbiguousMatchException
						// which will mean that this function is overloaded
						Console.WriteLine( string.Format( "[{0}] {1}: {2}",
							type.ToString(), mi.ToString(), IObjectBase.CalcOverloadPriority( mi ) ) );
					}
				}
			}
		}
		static void DumpOverloadNumbers( string typePrefix )
		{
			Assembly[] asms = AppDomain.CurrentDomain.GetAssemblies();
			for( int i = 0; i < asms.Length; ++i )
			{
				DumpOverloadNumbers( typePrefix, asms[ i ] );
			}
		}

		static void RunAllTests()
		{
			CreateAndDestroy();
			CoreFeatures();
			ArrayMem();
			Stack101();
			StackMoreTypes();
			StackInsert();
			StackArrayDict();
			StackPush();
			StackPropIndex();
			StackNegIdx();
			Indexing();
			Globals101();
			Libraries();
			FunctionCalls();
			SimpleFunctionCalls();
			ComplexGC();
			Iterators();
			ForkState();
			YieldResume();
			MiscAPIs();
			CSharpObjects();
			XRefObjects();
			AdvancedBinding();
		}

		static void NoteTest()
		{
			testCount++;
			Console.Write( "." );
		}
		static void Error( string err, StackTrace st )
		{
			failCount++;
			Console.Error.WriteLine( string.Format( "At {0}: {1}", st.GetFrame(1).GetFileLineNumber(), err ) );
		}
		static void Assert<T>( T a, T b )
		{
			NoteTest();
			if( !EqualityComparer<T>.Default.Equals( a, b ) )
			{
				Error( string.Format( "\"{0}\" does not equal \"{1}\"", a, b ), new StackTrace(true) );
			}
		}
		static void AssertVar( Variable a, Variable b )
		{
			NoteTest();
			if( a != b )
			{
				Error( string.Format( "\"{0}\" does not equal \"{1}\"", a, b ), new StackTrace(true) );
			}
		}
		static void AssertN<T>( T a, T b )
		{
			NoteTest();
			if( EqualityComparer<T>.Default.Equals( a, b ) )
			{
				Error( string.Format( "\"{0}\" equals \"{1}\"", a, b ), new StackTrace(true) );
			}
		}
		static void AssertVarN( Variable a, Variable b )
		{
			NoteTest();
			if( a == b )
			{
				Error( string.Format( "\"{0}\" equals \"{1}\"", a, b ), new StackTrace(true) );
			}
		}
		static void AssertC( bool cond, string test )
		{
			NoteTest();
			if( !cond )
			{
				Error( string.Format( "Assertion failed: \"{0}\"", test ), new StackTrace(true) );
			}
		}
		static void ExpectUnreached()
		{
			NoteTest();
			Error( "Exception was not thrown", new StackTrace(true) );
		}

		// core functions
		static Engine CreateEngine()
		{
			return new Engine();
		}
		static void DestroyEngine( Engine engine )
		{
			engine.Release();
			engine = null;

			MDL.CheckStateAndClear();
            HDL.CheckStateAndClear();

            Assert( Engine._engines.Count, 0 );
		}

		// TESTS
		static void CreateAndDestroy()
		{
			Console.Write( "\nCreate and destroy " );
			Engine engine = CreateEngine();
			DestroyEngine( engine );
		}

		static void CoreFeatures()
		{
			Console.Write( "\nCore feature tests " );
			Engine engine = CreateEngine();

			Variable va = engine.NullVar(), vb = engine.NullVar(), vc = engine.Var( "test" );
			Assert( va == vb, true );
			Assert( va != vb, false );
			Assert( va == vc, false );
			Assert( va != vc, true );
			Assert( va.isNull, true );
			Assert( va.notNull, false );
			Assert( vc.isNull, false );
			Assert( vc.notNull, true );
			
			DestroyEngine( engine );
		}
		
		static void ArrayMem()
		{
			Console.Write( "\nBasic array test " );
			Engine engine = CreateEngine();
			engine.PushArray( 0 );
			DestroyEngine( engine );
		}

		static void Stack101()
		{
			Console.Write( "\nStack - 101 " );
			Engine engine = CreateEngine();

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
				NoteTest();
				engine.Pop( 10 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, RC.EBOUNDS ); }
			engine.Pop( 9 );
			Assert( engine.StackSize(), 0 );

			DestroyEngine( engine );
		}

		static void SMT_CheckObj<T>( Engine e, T val, VarType type ) where T : struct
		{
			T? v = val;
			T? n = null;
			e.PushObj( val );
			e.PushObj( v );
			e.PushObj( n );
			Assert( e.ItemType( -3 ), type );
			Assert( e.ItemType( -2 ), type );
			Assert( e.ItemType( -1 ), VarType.Null );
			e.Pop( 3 );
		}
		public struct CustomStruct : IGetVariable
		{
			public float c;
			public int d;

			public Variable GetVariable( Context ctx )
			{
				ctx.Push( "c" );
				ctx.Push( c );
				ctx.Push( "d" );
				ctx.Push( d );
				return ctx.DictVar( 4 );
			}
		};
		static void StackMoreTypes()
		{
			Console.Write( "\nStack - more types " );
			Engine engine = CreateEngine();

			bool? v00 = false;
			bool? n00 = null;
			engine.Push(v00.Value);
			engine.Push(v00);
			engine.Push(n00);
			Assert( engine.ItemType( -3 ), VarType.Bool );
			Assert( engine.ItemType( -2 ), VarType.Bool );
			Assert( engine.ItemType( -1 ), VarType.Null );
			engine.Pop( 3 );
			SMT_CheckObj( engine, v00.Value, VarType.Bool );
			
			byte? v01 = 5;
			byte? n01 = null;
			engine.Push(v01.Value);
			engine.Push(v01);
			engine.Push(n01);
			Assert( engine.ItemType( -3 ), VarType.Int );
			Assert( engine.ItemType( -2 ), VarType.Int );
			Assert( engine.ItemType( -1 ), VarType.Null );
			engine.Pop( 3 );
			SMT_CheckObj( engine, v01.Value, VarType.Int );
			
			sbyte? v02 = 5;
			sbyte? n02 = null;
			engine.Push(v02.Value);
			engine.Push(v02);
			engine.Push(n02);
			Assert( engine.ItemType( -3 ), VarType.Int );
			Assert( engine.ItemType( -2 ), VarType.Int );
			Assert( engine.ItemType( -1 ), VarType.Null );
			engine.Pop( 3 );
			SMT_CheckObj( engine, v02.Value, VarType.Int );
			
			short? v03 = 5;
			short? n03 = null;
			engine.Push(v03.Value);
			engine.Push(v03);
			engine.Push(n03);
			Assert( engine.ItemType( -3 ), VarType.Int );
			Assert( engine.ItemType( -2 ), VarType.Int );
			Assert( engine.ItemType( -1 ), VarType.Null );
			engine.Pop( 3 );
			SMT_CheckObj( engine, v03.Value, VarType.Int );
			
			ushort? v04 = 5;
			ushort? n04 = null;
			engine.Push(v04.Value);
			engine.Push(v04);
			engine.Push(n04);
			Assert( engine.ItemType( -3 ), VarType.Int );
			Assert( engine.ItemType( -2 ), VarType.Int );
			Assert( engine.ItemType( -1 ), VarType.Null );
			engine.Pop( 3 );
			SMT_CheckObj( engine, v04.Value, VarType.Int );
			
			int? v05 = 5;
			int? n05 = null;
			engine.Push(v05.Value);
			engine.Push(v05);
			engine.Push(n05);
			Assert( engine.ItemType( -3 ), VarType.Int );
			Assert( engine.ItemType( -2 ), VarType.Int );
			Assert( engine.ItemType( -1 ), VarType.Null );
			engine.Pop( 3 );
			SMT_CheckObj( engine, v05.Value, VarType.Int );
			
			uint? v06 = 5;
			uint? n06 = null;
			engine.Push(v06.Value);
			engine.Push(v06);
			engine.Push(n06);
			Assert( engine.ItemType( -3 ), VarType.Int );
			Assert( engine.ItemType( -2 ), VarType.Int );
			Assert( engine.ItemType( -1 ), VarType.Null );
			engine.Pop( 3 );
			SMT_CheckObj( engine, v06.Value, VarType.Int );
			
			long? v07 = 5;
			long? n07 = null;
			engine.Push(v07.Value);
			engine.Push(v07);
			engine.Push(n07);
			Assert( engine.ItemType( -3 ), VarType.Int );
			Assert( engine.ItemType( -2 ), VarType.Int );
			Assert( engine.ItemType( -1 ), VarType.Null );
			engine.Pop( 3 );
			SMT_CheckObj( engine, v07.Value, VarType.Int );
			
			ulong? v08 = 5;
			ulong? n08 = null;
			engine.Push(v08.Value);
			engine.Push(v08);
			engine.Push(n08);
			Assert( engine.ItemType( -3 ), VarType.Int );
			Assert( engine.ItemType( -2 ), VarType.Int );
			Assert( engine.ItemType( -1 ), VarType.Null );
			engine.Pop( 3 );
			try
			{
				NoteTest();
				SMT_CheckObj( engine, v08.Value, VarType.Int );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, RC.ENOTSUP ); }
			
			float? v09 = 5;
			float? n09 = null;
			engine.Push(v09.Value);
			engine.Push(v09);
			engine.Push(n09);
			Assert( engine.ItemType( -3 ), VarType.Real );
			Assert( engine.ItemType( -2 ), VarType.Real );
			Assert( engine.ItemType( -1 ), VarType.Null );
			engine.Pop( 3 );
			SMT_CheckObj( engine, v09.Value, VarType.Real );
			
			double? v10 = 5;
			double? n10 = null;
			engine.Push(v10.Value);
			engine.Push(v10);
			engine.Push(n10);
			Assert( engine.ItemType( -3 ), VarType.Real );
			Assert( engine.ItemType( -2 ), VarType.Real );
			Assert( engine.ItemType( -1 ), VarType.Null );
			engine.Pop( 3 );
			SMT_CheckObj( engine, v10.Value, VarType.Real );

			CustomStruct? v11 = new CustomStruct(){ c = 12.3f, d = 123 };
			CustomStruct? n11 = null;
			engine.Push( v11.Value );
			engine.Push( v11 );
			engine.Push( n11 );
			Assert( engine.ItemType( -3 ), VarType.Object );
			Assert( engine.ItemType( -2 ), VarType.Object );
			Assert( engine.ItemType( -1 ), VarType.Null );
			engine.Pop( 3 );
			SMT_CheckObj( engine, v11.Value, VarType.Object );
			
			DestroyEngine( engine );
		}

		static void StackInsert()
		{
			Console.Write( "\nStack - insert " );
			Engine engine = CreateEngine();
			Variable sgs_dummy_var = engine.NullVar();
			engine.Push( 1 );
			engine.Push( 2 );
			Assert( engine.StackSize(), 2 );

			try
			{
				NoteTest();
				engine.InsertVar( 3, sgs_dummy_var );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, RC.EBOUNDS ); }
			
			try
			{
				NoteTest();
				engine.InsertVar( -4, sgs_dummy_var );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, RC.EBOUNDS ); }

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

			DestroyEngine( engine );
		}

		static void StackArrayDict()
		{
			Console.Write( "\nStack - array/dict " );
			Engine engine = CreateEngine();

			engine.PushNull();
			engine.Push( "key-one" );
			engine.Push( 5 );
			engine.Push( "key-two" );

			try
			{
				NoteTest();
				engine.PushArray( 5 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, RC.EBOUNDS ); }
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
				NoteTest();
				engine.PushDict( 6 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, RC.EBOUNDS ); }
			try
			{
				NoteTest();
				engine.PushDict( 5 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, RC.EINVAL ); }
			engine.PushDict( 0 );
			engine.Pop( 1 );
			engine.PushDict( 4 );
			
			Assert( engine.StackSize(), 2 );
			engine.Pop( 2 );
			Assert( engine.StackSize(), 0 );

			DestroyEngine( engine );
		}

		static void StackPush()
		{
			Console.Write( "\nStack - push item " );
			Engine engine = CreateEngine();

			engine.PushNull();
			engine.Push( 5 );

			try
			{
				NoteTest();
				engine.PushItem( 2 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, RC.EBOUNDS ); }
			Assert( engine.StackSize(), 2 );
			
			try
			{
				NoteTest();
				engine.PushItem( -3 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, RC.EBOUNDS ); }
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
			
			DestroyEngine( engine );
		}

		static void StackPropIndex()
		{
			Console.Write( "\nStack - properties/indices " );
			Engine engine = CreateEngine();

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

			DestroyEngine( engine );
		}

		static void StackNegIdx()
		{
			Console.Write( "\nStack - negative indices " );
			Engine engine = CreateEngine();

			engine.Push( "test" );
			engine.Push( 42 );
			engine.PushDict( 2 );
			Assert( engine.ItemType( -1 ), VarType.Object );
			Assert( engine.PushProperty( engine.StackItem( -1 ), "test" ), true );
			Assert( engine.StackItem( -1 ).GetInt(), 42 );
			engine.Pop( 1 );
			Assert( engine.StackSize(), 1 );
			
			DestroyEngine( engine );
		}

		static void Indexing()
		{
			Console.Write( "\nIndexing " );
			Engine engine = CreateEngine();

			engine.PushDict( 0 );
			Assert( engine.StackSize(), 1 );
			Assert( engine.ItemType( -1 ), VarType.Object );
			engine.Push( "test" );
			Assert( engine.StackSize(), 2 );

			Assert( engine.SetIndex( engine.StackItem( 0 ), engine.StackItem( 1 ), engine.StackItem( 1 ), false ), true );
			Variable var = engine.GetIndex( engine.StackItem( 0 ), engine.StackItem( 1 ), false );
			AssertN( var, null );
			Assert( var.type, VarType.String );
			
			DestroyEngine( engine );
		}

		static void Globals101()
		{
			Console.Write( "\nGlobals - 101 " );
			Engine engine = CreateEngine();

			Assert( engine.PushGlobal( "array" ), true );
			Assert( engine.StackSize(), 1 );
			Assert( engine.PushGlobal( "donut_remover" ), false );
			Assert( engine.StackSize(), 2 ); // null should be pushed as a result of not being able to find the global variable

			engine.PushArray( 1 );
			Assert( engine.ItemType( -1 ), VarType.Object );
			Assert( engine.StackSize(), 2 );
			engine.SetGlobal( "yarra", engine.StackItem( -1 ) );
			Assert( engine.StackSize(), 2 );

			DestroyEngine( engine );
		}

		static void Libraries()
		{
			Console.Write( "\nLibraries " );
			Engine engine = CreateEngine();

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

			DestroyEngine( engine );
		}

		static void FunctionCalls()
		{
			Console.Write( "\nFunction calls " );
			Engine engine = CreateEngine();

			Assert( engine.PushGlobal( "array" ), true );
			try
			{
				NoteTest();
				engine.FCall( 5, 1, false );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, RC.EBOUNDS ); }
			Assert( engine.StackSize(), 1 );
			try
			{
				NoteTest();
				engine.FCall( 1, 0, false );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, RC.EBOUNDS ); }
			engine.FCall( 0, 0, false );

			Assert( engine.PushGlobal( "array" ), true ); // needed for future tests
			Assert( engine.PushGlobal( "array" ), true );
			engine.FCall( 0, 1 );
			Assert( engine.StackSize(), 2 );
			Assert( engine.ItemType( -1 ), VarType.Object );

			try
			{
				NoteTest();
				engine.FCall( 1, 0, true );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, RC.EBOUNDS ); }
			engine.FCall( 0, 0, true );
			Assert( engine.StackSize(), 0 );
			
			DestroyEngine( engine );
		}

		static void SimpleFunctionCalls()
		{
			Console.Write( "\nSimple function calls " );
			Engine engine = CreateEngine();

			Assert( engine.XCall( "dumpvar", true, 5, "test" ), 1 );
			Assert( engine.StackItem( -1 ).GetString(), "bool (true)\nint (5)\nstring [4] \"test\"\n" );
			engine.Pop( 1 );
			
			Assert( engine.Exec( "function this2string(){ return tostring(this); }" ), 0 );
			Assert( engine.XThisCall( engine.GetGlobal( "this2string" ), engine.GetGlobal( "dumpvar" ) ), 1 );
			Assert( engine.StackItem( -1 ).GetString(), "C function" );
			engine.Pop( 1 );
			
			Assert( engine.ACall( "dumpvar", "roundtrip" )[0], "string [9] \"roundtrip\"\n" );
			Assert( engine.AThisCall( engine.GetGlobal( "this2string" ), "roundtrip" )[0], "roundtrip" );
			
			Assert( engine.OCall( "dumpvar", "roundtrip" ), "string [9] \"roundtrip\"\n" );
			Assert( engine.OThisCall( engine.GetGlobal( "this2string" ), "roundtrip" ), "roundtrip" );
			
			Assert( engine.Call<string>( "dumpvar", "roundtrip" ), "string [9] \"roundtrip\"\n" );
			Assert( engine.ThisCall<string>( engine.GetGlobal( "this2string" ), "roundtrip" ), "roundtrip" );
			
			DestroyEngine( engine );
		}

		static void ComplexGC()
		{
			Console.Write( "\nComplex GC " );
			Engine engine = CreateEngine();

			string str =
			"o = { a = [ { b = {}, c = 5 }, { d = { e = {} }, f = [] } ], g = [] };" +
			"o.a[0].parent = o; o.a[1].d.parent = o; o.a[1].d.e.parent = o.a[1].d;" +
			"o.a[1].f.push(o); o.g.push( o.a[1].f ); o.a.push( o.a );";

			int objcount = engine.Stat( Stat.ObjCount ).ToInt32();
			engine.Exec( str );
			AssertC( engine.Stat( Stat.ObjCount ).ToInt32() > objcount, "new objcount > old objcount" );
			engine.GCExecute();
			Assert( engine.Stat( Stat.ObjCount ).ToInt32(), objcount );
			
			DestroyEngine( engine );
		}

		static void Iterators()
		{
			Console.Write( "\nIterators " );
			Engine engine = CreateEngine();

			// test data
			engine.Push( true );
			engine.Push( 42 );
			engine.Push( "wat" );
			Variable arr = engine.ArrayVar( 3 );

			Variable iter = arr.GetIterator();
			// key/value at 0
			Assert( iter.IterAdvance(), true );
			AssertVar( iter.IterGetKey(), engine.Var( 0 ) );
			AssertVar( iter.IterGetValue(), engine.Var( true ) );
			// key/value at 1
			Assert( iter.IterAdvance(), true );
			Variable ik, iv;
			iter.IterGetKeyValue( out ik, out iv );
			AssertVar( ik, engine.Var( 1 ) );
			AssertVar( iv, engine.Var( 42 ) );
			// key/value at 2
			Assert( iter.IterAdvance(), true );
			AssertVar( iter.IterGetKey(), engine.Var( 2 ) );
			AssertVar( iter.IterGetValue(), engine.Var( "wat" ) );
			// end
			Assert( iter.IterAdvance(), false );
			Assert( iter.IterAdvance(), false );

			try
			{
				NoteTest();
				engine.Var( 5 ).GetIterator();
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, RC.ENOTSUP ); }
			
			DestroyEngine( engine );
		}

		static void ForkState()
		{
			Console.Write( "\nFork state " );
			Engine engine = CreateEngine();
			
			// fork the state
			Context f = engine.Fork( true );
			Context p = engine.Fork( false );
			AssertN( engine.ctx, f.ctx );
			AssertN( engine.ctx, p.ctx );

			// check state count
			Assert( (int) engine.Stat( Stat.StateCount ), 3 );
			Assert( (int) f.Stat( Stat.StateCount ), 3 );
			Assert( (int) p.Stat( Stat.StateCount ), 3 );

			f.Release();
			Assert( (int) p.Stat( Stat.StateCount ), 2 );
			p.Release();
			Assert( (int) engine.Stat( Stat.StateCount ), 1 );
			DestroyEngine( engine );

			// try running something on both
			engine = CreateEngine();
			f = engine.Fork( true );
			p = engine.Fork( false );
			string str =
			"global o = { a = [ { b = {}, c = 5 }, { d = { e = {} }, f = [] } ], g = [] };" +
			"o.a[0].parent = o; o.a[1].d.parent = o; o.a[1].d.e.parent = o.a[1].d;" +
			"o.a[1].f.push(o); o.g.push( o.a[1].f ); o.a.push( o.a );";
			engine.Exec( str );
			f.Exec( str );
			p.Exec( str );

			f.Release();
			p.Release();
			DestroyEngine( engine );
		}

		static void YieldResume()
		{
			Console.Write( "\nYield / resume " );
			Engine engine = CreateEngine();

			engine.Exec(
				"global m0 = true;\n" +
				"yield();\n" +
				"global m1 = true;\n" );

			// check if paused
			Assert( engine.GetGlobal( "m0" ).GetBool(), true );
			Assert( engine.GetGlobal( "m1" ), null );

			// resume
			engine.Resume();
			// check if done
			Assert( engine.GetGlobal( "m1" ).GetBool(), true );
			
			DestroyEngine( engine );
		}

		static void MiscAPIs()
		{
			Console.Write( "\nMiscellaneous " );
			Engine engine = CreateEngine();
			
			Variable fivevar = engine.Var( 5 );
			Variable ovar = engine.ArrayVar( 0 );
			Assert( fivevar.TypeOf(), "int" );
			Assert( ovar.TypeOf(), "array" );

			Assert( fivevar.Dump(), "int (5)" );
			Assert( ovar.Dump(), "array (0)\n[\n]" );

			Assert( engine.PadString( "A\nB" ), "A\n  B" );
			Assert( engine.ToPrintSafeString( "A\nB" ), "A\\x0AB" );
			Assert( engine.StringConcat( "a", fivevar, ovar ), "a5[]" );

			Variable clovar = ovar.Clone();
			AssertN( clovar, null );
			AssertVarN( clovar, ovar );
			
			Variable odata = engine.Var( 5 );
			byte[] sdata = engine.Serialize( odata );
			AssertN( sdata, null );
			Variable udata = engine.Unserialize( sdata );
			AssertVar( udata, odata );

			string ssdata = engine.SerializeSGSON( odata );
			AssertN( ssdata, null );
			udata = engine.UnserializeSGSON( ssdata );
			AssertVar( udata, odata );

			Variable dvar = engine.DictVar( 0 );
			Variable mvar = engine.MapVar( 0 );
			Assert( fivevar.IsArray(), false );
			Assert( ovar.IsArray(), true );
			Assert( dvar.IsArray(), false );
			Assert( mvar.IsArray(), false );
			Assert( fivevar.IsDict(), false );
			Assert( ovar.IsDict(), false );
			Assert( dvar.IsDict(), true );
			Assert( mvar.IsDict(), false );
			Assert( fivevar.IsMap(), false );
			Assert( ovar.IsMap(), false );
			Assert( dvar.IsMap(), false );
			Assert( mvar.IsMap(), true );
			Assert( ovar.ArraySize(), 0 );
			Assert( dvar.ArraySize(), -1 );
			ovar.ArrayPush( 5, "test" );
			Assert( ovar.ArraySize(), 2 );
			Assert( ovar.ArrayFind( fivevar ), 0 );
			Assert( ovar.ArrayFind( dvar ), -1 );
			ovar.ArrayRemove( fivevar, false );
			Assert( ovar.ArrayFind( fivevar ), -1 );
			mvar.SetProp( fivevar, dvar );
			Assert( mvar.Unset( fivevar ), true );
			Assert( mvar.Unset( fivevar ), false );

			engine.Msg( MsgLevel.INFO, "This is a test message." );
			
			DestroyEngine( engine );
		}

		public class EmptyObject : IObject
		{
			public EmptyObject( Context c ) : base( c ){}
		}
		public class FullObject1 : IObject
		{
			public FullObject1( Context c ) : base( c ){}

			public string _useProp3(){ return prop3; }

			public static string pfxstr = "PFX:";
			public static string StaticTestMethod( string arg1 ){ return pfxstr + arg1; }
			public string TestMethod( string arg1 ){ return prop3 + "|" + arg1; }
			public void TestMsgMethod( int a, [CallingThread] Context ctx, int b )
			{
				Assert( a, 121 );
				Assert( b, 754 );
				ctx.Msg( MsgLevel.INFO, string.Format( "[test message: a = {0}, b = {1}]", a, b ) );
			}

			public int prop1 = 5;
			protected float prop2 = 6.0f;
			private string prop3 = "7";
			[HideProperty()]
			public int invprop1 = 123;
			[HideProperty(CanRead=true)]
			public int invprop2 = 234;
			[HideProperty(CanWrite=true)]
			public int invprop3 = 345;

			public override void OnDestroy()
			{
				Console.WriteLine( "[FullObject1] OnDestroy()" );
			}
			public override void OnGCMark()
			{
				Console.WriteLine( "[FullObject1] OnGCMark()" );
			}
			public override bool ConvertToBool()
			{
				return false;
			}
			public override string ConvertToString()
			{
				return "[Full_Object_1]";
			}
			public override string OnDump( Context ctx, int maxdepth )
			{
				return "[this is a FULL OBJECT]";
			}
		}
		static void CSharpObjects()
		{
			Console.Write( "\nC# object exposure " );
			Engine engine = CreateEngine();

			// test the empty interface
			{
				NI.CreateObject( engine.ctx, IntPtr.Zero, IObjectBase._sgsNullObjectInterface );
				engine.Stat( Stat.XDumpStack );
				engine.Pop( 1 );
			}

			// init & disown object
			IObject obj = new EmptyObject( engine );
			engine.Push( obj );
			AssertN( obj._sgsObject, IntPtr.Zero );
			engine.Stat( Stat.XDumpStack ); // should have refcount = 2 (one for IObject and one for stack)
			Variable si0 = engine.StackItem( 0 );
			Assert( engine.Call<string>( "typeof", si0 ), "EmptyObject" );
			si0.Release(); // for accurate refcount (otherwise this reference is retained for an unknown amount of time)
			obj.DisownClassObject(); // free IObject reference
			engine.Stat( Stat.XDumpStack ); // should have refcount = 1 (stack) and name = <nullObject>
			Assert( engine.Call<string>( "typeof", engine.StackItem( 0 ) ), "<nullObject>" );
			engine.Pop( 1 );

			// test metamethods
			IObject obj1 = new FullObject1( engine );
			Variable obj1var = engine.Var( obj1 );
			Assert( engine.Call<bool>( "tobool", obj1 ), false ); // test "convert"(tobool)/ConvertToBool
			Assert( engine.Call<string>( "tostring", obj1 ), "[Full_Object_1]" ); // test "convert"(tostring)/ConvertToString
			Assert( engine.Call<string>( "dumpvar", obj1 ), "[this is a FULL OBJECT]\n" ); // test "dump"/OnDump

			Assert( obj1var.SetProp( "prop1", engine.Var( 15 ) ), true ); // test "setindex"(isprop=true)
			Assert( obj1var.SetProp( "prop2", engine.Var( 16 ) ), true );
			Assert( obj1var.SetProp( "prop3", engine.Var( 17 ) ), true );

			AssertVar( obj1var.GetProp( "prop1" ), engine.Var( 15 ) ); // test "getindex"(isprop=true)
			AssertVar( obj1var.GetProp( "prop2" ), engine.Var( 16.0f ) );
			AssertVar( obj1var.GetProp( "prop3" ), engine.Var( "17" ) );
			AssertVar( obj1var.GetProp( "invprop1" ), null ); // - HideProperty()
			AssertVar( obj1var.GetProp( "invprop2" ), engine.Var( 234 ) ); // - HideProperty()
			AssertVar( obj1var.GetProp( "invprop3" ), null ); // - HideProperty()

			// test method wrapper
			DNMethod dnm1 = new DNMethod( engine, typeof(FullObject1).GetMethod("TestMethod") );
			Variable dnm1var = engine.Var( dnm1 );
			Assert( engine.ThisCall<string>( dnm1var, obj1var, "test" ), "17|test" );
			dnm1.DisownClassObject();

			// test static method wrapper
			DNMethod dnm2 = new DNMethod( engine, typeof(FullObject1).GetMethod("StaticTestMethod") );
			Variable dnm2var = engine.Var( dnm2 );
			Assert( engine.Call<string>( dnm2var, "sTest" ), "PFX:sTest" );
			dnm2.DisownClassObject();

			// test static method dictionary
			Assert( engine.Call<string>( "tostring", IObjectBase.CreateStaticDict( engine, typeof(FullObject1) ) ),
				"{_useProp3()=DNMethod,StaticTestMethod(String)=DNMethod,TestMethod(String)=DNMethod"+
				",TestMsgMethod(Int32,SGScript.Context,Int32)=DNMethod"+
				",_useProp3=DNMethod,StaticTestMethod=DNMethod,TestMethod=DNMethod"+
				",TestMsgMethod=DNMethod}" );

			// test static (meta-)object
			Variable movar = engine._GetMetaObject( typeof(FullObject1) ).GetVariable();
			AssertVar( movar, obj1var.GetMetaObj() );
			Assert( movar.GetProp( "_useProp3" ).ConvertToString(), "SGScript.DNMethod(APITest.APITest+FullObject1._useProp3)" );
			Assert( movar.GetProp( "pfxstr" ).GetString(), "PFX:" );

			// test instance meta object lookup
			Assert( obj1var.GetProp( "_useProp3" ).ConvertToString(), "SGScript.DNMethod(APITest.APITest+FullObject1._useProp3)" );
			Assert( obj1var.GetProp( "pfxstr" ).GetString(), "PFX:" );
			Assert( obj1var.GetMetaObj().SetProp( "pfxstr", engine.Var( "[PFX]:" ) ), true );
			Assert( obj1var.GetMetaObj().GetProp( "pfxstr" ).GetString(), "[PFX]:" );
			Assert( obj1var.GetProp( "pfxstr" ).GetString(), "[PFX]:" );

			// register type
			engine.SetGlobal( "FullObject1", engine._GetMetaObject( typeof(FullObject1) ).GetVariable() );

			// test extension method
			engine.Exec( "function FullObject1.customMethod( txt ){ return this.pfxstr .. txt; }" );
			AssertN( obj1var.GetMetaObj().GetProp( "customMethod" ), null );
			Assert( engine.ThisCall<string>( "customMethod", obj1var, "cmTest" ), "[PFX]:cmTest" );

			// test method messaging
			engine.ThisCall<Nothing>( "TestMsgMethod", obj1var, 121, 754 );
			
			DestroyEngine( engine );
			FullObject1.pfxstr = "PFX:"; // restore
		}

		public class XRef : IObject
		{
			public XRef a;
			public XRef b;

			public XRef( Context c ) : base( c ){}
		}
		static void XRefObjects()
		{
			Console.Write( "\nXRef objects " );
			Engine engine = CreateEngine();

			XRef v1 = new XRef( engine );
			XRef v2 = new XRef( engine );
			XRef v3 = new XRef( engine );

			v1.a = v2;
			v1.b = v3;
			v2.a = v1;
			v2.b = v3;
			v3.a = v1;
			v3.b = v2;

			engine.GCExecute();
			v1 = null;
			v2 = null;
			v3 = null;
			GC.Collect();
			GC.WaitForPendingFinalizers();
			engine.GCExecute();
			
			DestroyEngine( engine );
		}

		public class NonCustom
		{
			static void Action(){}
			string ReturnStr(){ return string.Format( "[ToString={0}]", ToString() ); }
			string Overloads( int a ){ return string.Format( "[a={0}]", a ); }
			string Overloads( int a, int b ){ return string.Format( "[a={0},b={1}]", a, b ); }
		}
		static void AdvancedBinding()
		{
			Console.Write( "\nAdvanced bindings " );
			Engine engine = CreateEngine();
			
			// meta-object of a random class
			Variable movar = engine._GetMetaObject( typeof(NonCustom) ).GetVariable();
			Assert( movar.GetProp( "Action" ).ConvertToString(), "SGScript.DNMethod(APITest.APITest+NonCustom.Action)" );

			// overload retrieval
			Assert( movar.GetProp( "Overloads(Int32)" ).ConvertToString(), "SGScript.DNMethod(APITest.APITest+NonCustom.Overloads)" );
			Assert( movar.GetProp( "Overloads(Int32,Int32)" ).ConvertToString(), "SGScript.DNMethod(APITest.APITest+NonCustom.Overloads)" );

			// generic handle & method call
			DNHandle dnh = new DNHandle( engine, new NonCustom() );
			Assert( engine.ThisCall<string>( "ReturnStr", dnh.GetVariable() ), "[ToString=APITest.APITest+NonCustom]" );

			// overload call
			Assert( engine.ThisCall<string>( "Overloads(Int32)", dnh.GetVariable(), 314 ), "[a=314]" );
			Assert( engine.ThisCall<string>( "Overloads(Int32,Int32)", dnh.GetVariable(), 123, 456 ), "[a=123,b=456]" );

			// overload call in scripts
			Assert( engine.Exec( "function callOverload2(){ return this.\"Overloads(Int32,Int32)\"(987,654); }" ), 0 );
			Assert( engine.ThisCall<string>( engine.GetGlobal( "callOverload2" ), dnh.GetVariable() ), "[a=987,b=654]" );

			// load the Console class
			engine.BindClass( typeof(System.Console) );
			Assert( engine.Exec( "printvar(System.Console);" ), 0 );
			Assert( engine.Exec( "System.Console.'WriteLine(String)'('[!test_so!]');" ), 0 );
			// - check if default overloads of 'Write' and 'WriteLine' are good
			Assert( engine.Exec( "System.Console.Write('[!test_doW!]');" ), 0 );
			Assert( engine.Exec( "System.Console.WriteLine('[!test_doWL!]');" ), 0 );
			
			DestroyEngine( engine );
		}
	}
}
