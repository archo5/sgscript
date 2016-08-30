package com.sgscript.sample;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
import android.widget.LinearLayout;
import android.text.method.ScrollingMovementMethod;

public class Main extends Activity
{
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        TextView label = new TextView(this);
        label.setText(initAndDumpGlobals());
        label.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.MATCH_PARENT));
        label.setMovementMethod(new ScrollingMovementMethod());
        setContentView(label);
    }
    
    public static native String initAndDumpGlobals();
    static
    {
    	System.loadLibrary("sgscript");
    	System.loadLibrary("sgs_sample");
    }
}
