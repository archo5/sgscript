
#include <windows.h>
#include <sgscript.h>


#define APPNAME "SGSVM Executable Stub"


#define FILE_READ 1
#define FILE_WRITE 2
HANDLE CreateFileFast( const CHAR* name, DWORD rw, DWORD mode )
{
	DWORD access = 0;
	DWORD share = 0;
	if( rw & FILE_READ )
	{
		access |= GENERIC_READ;
		share |= FILE_SHARE_READ;
	}
	if( rw & FILE_WRITE )
	{
		access |= GENERIC_WRITE;
		share |= FILE_SHARE_WRITE;
	}
	return CreateFile( name, access, share, NULL, mode, FILE_ATTRIBUTE_NORMAL, NULL );
}


int main( int argc, char* argv[] )
{
	int i;
	BYTE buf[ 4096 ], *path, *data;
	DWORD read, written, filesize;
	HANDLE fh;

	path = buf;

	/* open EXE file */
	read = GetModuleFileName( NULL, (CHAR*) buf, sizeof( buf ) );
	if( read >= sizeof( buf ) )
	{
		path = malloc( read + 1 );
		GetModuleFileName( NULL, (CHAR*) buf, sizeof( buf ) );
	}
	fh = CreateFileFast( (CHAR*) buf, FILE_READ, OPEN_EXISTING );
	if( path != buf )
		free( path );
	if( fh == INVALID_HANDLE_VALUE )
		return E_FAIL;
	ReadFile( fh, buf, sizeof( buf ), &read, NULL );
	IMAGE_DOS_HEADER* dosheader = (IMAGE_DOS_HEADER*) buf;

	/* locate PE header */
	IMAGE_NT_HEADERS32* header = (IMAGE_NT_HEADERS32*)( buf + dosheader->e_lfanew );
	if( dosheader->e_magic != IMAGE_DOS_SIGNATURE || header->Signature != IMAGE_NT_SIGNATURE )
	{
		CloseHandle( fh );
		return E_UNEXPECTED;
	}

	/* locate end of last section */
	IMAGE_SECTION_HEADER* sectiontable = (IMAGE_SECTION_HEADER*)( (BYTE*) header + sizeof( IMAGE_NT_HEADERS32 ) );
	DWORD exesize = 0;
	for( i = 0; i < header->FileHeader.NumberOfSections; i++ )
	{
		DWORD nesize = sectiontable->PointerToRawData + sectiontable->SizeOfRawData;
		if( exesize < nesize )
			exesize = nesize;
		sectiontable++;
	}

	/* read the following data */
	filesize = GetFileSize( fh, NULL );
	if( filesize <= exesize )
	{
		if( argc == 3 )
		{
			HANDLE hsgs, hout = CreateFileFast( argv[ 1 ], FILE_WRITE, CREATE_ALWAYS );
			if( !hout )
			{
				MessageBox( 0, "Could not open executable file for writing", APPNAME, MB_ICONERROR );
				CloseHandle( fh );
				return E_FAIL;
			}
			hsgs = CreateFileFast( argv[ 2 ], FILE_READ, OPEN_EXISTING );
			if( !hsgs )
			{
				MessageBox( 0, "Could not open script file for reading", APPNAME, MB_ICONERROR );
				CloseHandle( hout );
				CloseHandle( fh );
				return E_FAIL;
			}
			SetFilePointer( fh, 0, NULL, FILE_BEGIN );
			while( ReadFile( fh, buf, sizeof( buf ), &read, NULL ) && read )
				WriteFile( hout, buf, read, &written, NULL );
			while( ReadFile( hsgs, buf, sizeof( buf ), &read, NULL ) && read )
				WriteFile( hout, buf, read, &written, NULL );
			CloseHandle( fh );
			CloseHandle( hsgs );
			CloseHandle( hout );
			MessageBox( 0, "File saved!", APPNAME, MB_ICONINFORMATION );
			return S_OK;
		}
		else
		{
			const char* info = "To create an executable from .sgs"
				", run sgsexe <output-file.exe> <script-file.sgs>."
				"\n\nglobal 'argv' will be the array of arguments";
			MessageBox( 0, info, APPNAME, MB_ICONINFORMATION );
			CloseHandle( fh );
			return E_ABORT;
		}
	}

	data = malloc( filesize - exesize );
	SetFilePointer( fh, exesize, NULL, FILE_BEGIN );
	ReadFile( fh, data, filesize - exesize, &read, NULL );
	CloseHandle( fh );
	
	{
		SGS_CTX = sgs_CreateEngine();

		for( i = 0; i < argc; ++i )
			sgs_PushString( C, argv[ i ] );

		sgs_PushGlobal( C, "array" );
		sgs_Call( C, argc, 1 );
		
		sgs_StoreGlobal( C, "argv" );

		sgs_ExecBuffer( C, (char*) data, filesize - exesize );

		sgs_DestroyEngine( C );
	}

	free( data );

	return 0;
}
