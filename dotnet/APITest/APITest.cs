using System;
using System.Diagnostics;
using System.Collections.Generic;
using SGScript;

namespace APITest
{
	public class APITest
	{
		static int failCount = 0;
		static int testCount = 0;
		static void Main(string[] args)
		{
			Console.WriteLine( "SGS.NET API tests" );

			CreateAndDestroy();
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
			CoreFeatures();
			CSharpObjects();

			Console.WriteLine( "\n\n--- Testing finished! ---" );
			Console.WriteLine( failCount > 0 ?
				string.Format( "\n  ERROR: {0} tests failed!\n", failCount ) :
				string.Format( "\n  SUCCESS! All tests passed! ({0})\n", testCount ) );
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

		// TESTS
		static void CreateAndDestroy()
		{
			Console.Write( "\nCreate and destroy " );
			Engine engine = new Engine();
			engine.Release();
		}

		static void ArrayMem()
		{
			Console.Write( "\nBasic array test " );
			Engine engine = new Engine();
			engine.PushArray( 0 );
			engine.Release();
		}

		static void Stack101()
		{
			Console.Write( "\nStack - 101 " );
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
				NoteTest();
				engine.Pop( 10 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			engine.Pop( 9 );
			Assert( engine.StackSize(), 0 );

			engine.Release();
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
			Engine engine = new Engine();

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
			catch( SGSException e ){ Assert( e.resultCode, NI.ENOTSUP ); }
			
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
			
			engine.Release();
		}

		static void StackInsert()
		{
			Console.Write( "\nStack - insert " );
			Engine engine = new Engine();
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
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			
			try
			{
				NoteTest();
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
			Console.Write( "\nStack - array/dict " );
			Engine engine = new Engine();

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
				NoteTest();
				engine.PushDict( 6 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			try
			{
				NoteTest();
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
			Console.Write( "\nStack - push item " );
			Engine engine = new Engine();

			engine.PushNull();
			engine.Push( 5 );

			try
			{
				NoteTest();
				engine.PushItem( 2 );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			Assert( engine.StackSize(), 2 );
			
			try
			{
				NoteTest();
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
			Console.Write( "\nStack - properties/indices " );
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
			Console.Write( "\nStack - negative indices " );
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
			Console.Write( "\nIndexing " );
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
			Console.Write( "\nGlobals - 101 " );
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
			Console.Write( "\nLibraries " );
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
			Console.Write( "\nFunction calls " );
			Engine engine = new Engine();

			Assert( engine.PushGlobal( "array" ), true );
			try
			{
				NoteTest();
				engine.FCall( 5, 1, false );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			Assert( engine.StackSize(), 1 );
			try
			{
				NoteTest();
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
				NoteTest();
				engine.FCall( 1, 0, true );
				ExpectUnreached();
			}
			catch( SGSException e ){ Assert( e.resultCode, NI.EBOUNDS ); }
			engine.FCall( 0, 0, true );
			Assert( engine.StackSize(), 0 );
			
			engine.Release();
		}

		static void SimpleFunctionCalls()
		{
			Console.Write( "\nSimple function calls " );
			Engine engine = new Engine();

			Assert( engine.XCall( "dumpvar", true, 5, "test" ), 1 );
			Assert( engine.StackItem( -1 ).GetString(), "bool (true)\nint (5)\nstring [4] \"test\"\n" );
			engine.Pop( 1 );
			
			Assert( engine.Exec( "function this2string(){ return tostring(this); }" ), 0 );
			Assert( engine.XThisCall( "this2string", engine.GetGlobal( "dumpvar" ) ), 1 );
			Assert( engine.StackItem( -1 ).GetString(), "C function" );
			engine.Pop( 1 );
			
			Assert( engine.ACall( "dumpvar", "roundtrip" )[0], "string [9] \"roundtrip\"\n" );
			Assert( engine.AThisCall( "this2string", "roundtrip" )[0], "roundtrip" );
			
			Assert( engine.OCall( "dumpvar", "roundtrip" ), "string [9] \"roundtrip\"\n" );
			Assert( engine.OThisCall( "this2string", "roundtrip" ), "roundtrip" );
			
			Assert( engine.Call<string>( "dumpvar", "roundtrip" ), "string [9] \"roundtrip\"\n" );
			Assert( engine.ThisCall<string>( "this2string", "roundtrip" ), "roundtrip" );
			
			engine.Release();
		}

		static void ComplexGC()
		{
			Console.Write( "\nComplex GC " );
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

		static void CoreFeatures()
		{
			Console.Write( "\nCore feature tests " );
			Engine engine = new Engine();

			Variable va = engine.NullVar(), vb = engine.NullVar(), vc = engine.Var( "test" );
			Assert( va == vb, true );
			Assert( va != vb, false );
			Assert( va == vc, false );
			Assert( va != vc, true );
			Assert( va.isNull, true );
			Assert( va.notNull, false );
			Assert( vc.isNull, false );
			Assert( vc.notNull, true );
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
			Engine engine = new Engine();

			// test the empty interface
			{
				NI.CreateObject( engine.ctx, IntPtr.Zero, IObject.AllocInterface( new NI.ObjInterface(), "empty" ) );
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
			
			engine.Release();
		}
	}
}
