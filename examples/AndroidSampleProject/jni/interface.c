
#include <jni.h>
#include <sgscript.h>


JNIEXPORT jstring JNICALL Java_com_sgscript_sample_Main_initAndDumpGlobals( JNIEnv* env, jobject self )
{
	SGS_CTX = sgs_CreateEngine();
	sgs_PushEnv( C );
	sgs_GlobalCall( C, "dumpvar", 1, 1 );
	jstring out = (*env)->NewStringUTF( env, sgs_ToString( C, -1 ) );
	sgs_DestroyEngine( C );
	return out;
}

