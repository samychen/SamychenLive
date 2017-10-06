package com.gracefulengineer.samychen;

import android.graphics.ImageFormat;
import android.hardware.Camera;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Message;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import java.lang.ref.WeakReference;

public class LiveActivity extends AppCompatActivity implements SurfaceHolder.Callback, Camera.PreviewCallback{
    private Camera mCamera;
    private SurfaceView mSurfaceView;
    private SurfaceHolder mSurfaceHolder;
    private int mCameraId = 0;
    private int width = 720;
    private int height = 480;
    private static final int STREAMER_INIT = 0;
    private static final int STREAMER_HANDLE = 1;
    private static final int STREAMER_RELEASE = 2;
    private static final int STREAMER_FLUSH = 3;
    long startTime = System.currentTimeMillis();
    /**
     * 判断有没有初始化成功，不成功不不进行后续的编码处理
     */
    private int liveInitResult = -1;

    /**
     * 异步操作
     */
    private HandlerThread mHandlerThread;
    private LiveHandler mHandler;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_live);

        mSurfaceView = (SurfaceView) findViewById(R.id.live_sv_live);
        mSurfaceHolder = mSurfaceView.getHolder();
        mSurfaceHolder.setFixedSize(width, height);
        mSurfaceHolder.addCallback(this);

        mHandlerThread = new HandlerThread("liveHandlerThread");
        mHandlerThread.start();
        mHandler = new LiveHandler(this, mHandlerThread.getLooper());
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.menu_live, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if(item.getItemId() == R.id.checkable_menu) {
            boolean isChecked = item.isChecked();
            Log.e("LiveActivity", "checked: " + isChecked);
            item.setChecked(!isChecked);

            mCameraId = 1 - mCameraId;
            destroyCamera();
            initCamera();
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    @Override
    public void onPreviewFrame(byte[] data, Camera camera) {
        long timestamp=1000*(System.currentTimeMillis()-startTime);
        /**
         * 如果初始化成功，那就把数据发送到Handler，然后再调用native方法
         */
        if(liveInitResult == 0 && data != null && data.length > 0) {
            Message msg = Message.obtain();
            Bundle bundle = new Bundle();
            bundle.putByteArray("frame_data", data);
            bundle.putLong("timestamp",timestamp);
            msg.what = STREAMER_HANDLE;
            msg.setData(bundle);
            mHandler.sendMessage(msg);
        }
        mCamera.addCallbackBuffer(data);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        /**
         * 在surface创建的时候进行初始化，如果失败了，也是需要释放已经开辟了的资源
         */
        liveInitResult = streamerInit(width, height);
        if(liveInitResult == -1) {
            mHandler.sendEmptyMessage(STREAMER_RELEASE);
        } else {
            Log.e("LiveActivity", "streamer init result: " + liveInitResult);
        }
        initCamera();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {

    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        /**
         * 在surface销毁的时候清空缓冲帧（在直播成功开启的情况下）
         * 清空后就进行资源的释放
         * 并且把HandlerThread退出
         */
        if(liveInitResult == 0) {
            mHandler.sendEmptyMessage(STREAMER_FLUSH);
        }
        mHandler.sendEmptyMessage(STREAMER_RELEASE);
        mHandlerThread.quitSafely();
        destroyCamera();
    }

    private void initCamera() {
        try {
            mCamera = Camera.open(mCameraId);
            mCamera.setPreviewDisplay(mSurfaceHolder);
            Camera.Parameters params = mCamera.getParameters();
            //设置预览大小
            params.setPreviewSize(width, height);
            //设置生成的照片大小
            params.setPictureSize(width, height);
            params.setPreviewFormat(ImageFormat.NV21);
            mCamera.setDisplayOrientation(90);
            //params.setRotation(90);

            /*List<Camera.Size> sizes = params.getSupportedPreviewSizes();
            for(Camera.Size s : sizes) {
                Log.e("LiveActivity", s.width + " X " + s.height);
            }*/

            mCamera.setParameters(params);
//            mCamera.setPreviewCallback(this);
            int buffSize=width*height*3/2;
            mCamera.addCallbackBuffer(new byte[buffSize]);
            mCamera.addCallbackBuffer(new byte[buffSize]);
            mCamera.setPreviewCallbackWithBuffer(this);
            mCamera.startPreview();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private void destroyCamera() {
        if(mCamera == null) {
            return;
        }

        mCamera.setPreviewCallback(null);
        mCamera.stopPreview();
        mCamera.release();
        mCamera = null;
    }

    /**
     * 初始化编码的一些东西，比如编码器等
     * @param width  编码视频的宽
     * @param height 编码视频的高
     * @return 0 成功  小于0失败
     */
    private native int streamerInit(int width, int height);

    /**
     * 对每一次预览的数据进行编码推流
     * @param data NV21格式的数据
     * @return 0成功，小于0失败
     */
    private native int streamerHandle(byte[] data, long timestamp);

    /**
     * 把缓冲帧的数据清空
     * @return 0成功，小于0失败
     */
    private native int streamerFlush();

    /**
     * 释放资源，比如编码器这些
     * @return 0成功，小于0失败
     */
    private native int streamerRelease();



    //------------------------------------------------------------------------

    private static class LiveHandler extends Handler {
        private WeakReference<LiveActivity> mActivity;

        public LiveHandler(LiveActivity activity, Looper looper) {
            super(looper);
            mActivity = new WeakReference<LiveActivity>(activity);
        }

        @Override
        public void handleMessage(Message msg) {
            super.handleMessage(msg);

            LiveActivity activity = mActivity.get();
            if(activity == null) {
                return;
            }

            switch (msg.what) {
                case STREAMER_INIT:
                    break;

                case STREAMER_HANDLE:
                    Bundle bundle = msg.getData();
                    if(bundle != null) {
                        byte[] data = bundle.getByteArray("frame_data");
                        long timestamp = bundle.getLong("timestamp");
                        if(data != null && data.length > 0) {
                            activity.streamerHandle(data,timestamp);
                        } else {
                            Log.e("LiveActivity", "byte data null");
                        }
                    } else {
                        Log.e("LiveActivity", "bundle null");
                    }
                    break;

                case STREAMER_FLUSH:
                    activity.streamerFlush();
                    break;

                case STREAMER_RELEASE:
                    activity.streamerRelease();
                    break;
            }
        }
    }
}
