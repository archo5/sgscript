
#include <jni.h>
#include <sgscript.h>


JNIEXPORT jstring JNICALL Java_com_sgscript_sample_Main_initAndDumpGlobals( JNIEnv* env, jobject self, jstring packageName )
{
	SGS_CTX = sgs_CreateEngine();
	
	// set package name
	const char* nPackageName = (*env)->GetStringUTFChars( env, packageName, 0 );
	sgs_PushString( C, nPackageName );
	sgs_SetGlobalByName( C, "ANDROID_PACKAGE_NAME", sgs_StackItem( C, -1 ) );
	sgs_Pop( C, 1 );
	(*env)->ReleaseStringUTFChars( env, packageName, nPackageName );
	
	// give access to native modules
	sgs_ExecString( C,
		"foreach( p : multiply_path_ext_lists( '/data/data/' .. ANDROID_PACKAGE_NAME .. '/lib' ) )"
		" _G.SGS_PATH ..= ';' .. p;" );
	
	// load a native module
	sgs_Include( C, "sgsxgmath" );
	
	// dump environment to string
	sgs_PushEnv( C );
	sgs_GlobalCall( C, "dumpvar", 1, 1 );
	jstring out = (*env)->NewStringUTF( env, sgs_ToString( C, -1 ) );
	
	// clean up
	sgs_DestroyEngine( C );
	return out;
}

