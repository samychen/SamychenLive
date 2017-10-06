package com.gracefulengineer.samychen;

import android.content.Intent;
import android.os.Bundle;
import android.support.v7.app.AppCompatActivity;
import android.view.View;


public class MainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
    }
    public void clickConfigure(View v){
        clickTest();
    }
    public void live(View v){
        startActivity(new Intent(MainActivity.this,LiveActivity.class));
    }
    public void watch(View v){
        clickTest();
    }
    public static native void clickTest();
    static {
        System.loadLibrary("live_jni");
    }
}
