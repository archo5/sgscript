using System;
using SGScript;

class VM
{
	static bool printVersion = false;
	static bool printStats = false;
	static bool separate = false;
	static Engine engine;

	static void PrintHelp()
	{
		Console.WriteLine( "syntax:" );
		Console.WriteLine( "\tsgsvm [files|options]" );
		Console.WriteLine( "\tsgsvm [options] -p <file>[, <arguments>]" );
		Console.WriteLine( "" );
		Console.WriteLine( "options:" );
		Console.WriteLine( "\t-h, --help: print this text" );
		Console.WriteLine( "\t-v, --version: print version info" );
		Console.WriteLine( "\t-s, --separate: restart the engine between scripts" );
		// Console.WriteLine( "\t-d, --debug: enable interactive debugging on errors" );
		Console.WriteLine( "\t-p, --program: translate the following arguments into a SGS program call" );
		Console.WriteLine( "\t--stats: print VM stats after running the scripts" );
		// Console.WriteLine( "\t--profile: enable profiling by collecting call stack timings" );
		// Console.WriteLine( "\t--profile-ops: enable low-level VM instruction profiling" );
		// Console.WriteLine( "\t--profile-mem: enable memory usage profiling" );
	}
	
	static void PrintVersion()
	{
		if( printVersion )
			Console.WriteLine( "SGSVM.NET [SGScript v{0}]", NI.Version );
	}

	static void Init()
	{
		engine = new Engine();
	}
	static void Free()
	{
		if( printStats )
			engine.Stat( Stat.DumpStats );
		engine.Release();
	}

	static void PrintErr( string err )
	{
		Console.Error.WriteLine( "SGSVM.NET Error: " + err );
	}
	
	static int Main(string[] args)
	{
		if( args.Length < 1 )
		{
			PrintErr( "need to specify at least one file" );
			PrintHelp();
			return 1;
		}
		
		for( int i = 0; i < args.Length; ++i )
		{
			if( args[ i ] == "--separate" || args[ i ] == "-s" ){ separate = true; args[ i ] = null; }
			// else if( args[ i ] == "--debug" || args[ i ] == "-d" ){ idbg = 1; args[ i ] = null; }
			// else if( args[ i ] == "--profile" ){ prof = 1; args[ i ] = null; }
			// else if( args[ i ] == "--profile-ops" ){ prof = 2; args[ i ] = null; }
			// else if( args[ i ] == "--profile-mem" ){ prof = 3; args[ i ] = null; }
			else if( args[ i ] == "--help" || args[ i ] == "-h" ){ PrintHelp(); return 0; }
			else if( args[ i ] == "--version" || args[ i ] == "-v" ){ printVersion = true; args[ i ] = null; }
			else if( args[ i ] == "--stats" ){ printStats = true; args[ i ] = null; }
			else if( args[ i ] == "--program" || args[ i ] == "-p" )
			{
				i++;
				if( i == args.Length )
				{
					PrintErr( "file name expected" );
					return 1;
				}

				PrintVersion();
				Init();

				for( int j = i; j < args.Length; ++j )
					engine.Push( args[ j ] );
				engine.PushArray( args.Length - i );
				engine.SetGlobal( "argv", engine.StackItem( -1 ) );
				engine.Pop( 1 );

				engine.SetGlobal( "argc", engine.Var( args.Length - i ) );

				try
				{
					engine.Include( args[ i ] );
				}
				catch( SGSException ex )
				{
					PrintErr( string.Format( "failed to run \"{0}\": {1}", args[ i ], ex.Message ) );
				}
				Free();
				return 0;
			}
		}

		PrintVersion();
		Init();
		for( int i = 0; i < args.Length; ++i )
		{
			if( args[ i ] != null )
			{
				try
				{
					engine.ExecFile( args[ i ] );
				}
				catch( SGSException ex )
				{
					PrintErr( string.Format( "failed to execute \"{0}\": {1}", args[ i ], ex.Message ) );
				}
				if( separate )
				{
					Free();
					Init();
				}
			}
		}
		
		Free();
		return 0;
	}
}

