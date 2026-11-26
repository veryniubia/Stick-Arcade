package dev.stickarcade.player;

import android.app.Activity;
import android.content.*;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.graphics.Color;
import android.hardware.*;
import android.os.*;
import android.provider.Settings;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public final class Player extends Activity implements Choreographer.FrameCallback, SensorEventListener {
    private Screen screen;
    private Pad pad;
    private TextView status;
    private SensorManager sensors;
    private Hotspot hotspot;
    private boolean running;
    private final float[] axes=new float[3];
    private final float[] attitude=new float[9];
    private final ArrayDeque<String> history=new ArrayDeque<>();
    private long deadline, reportStart;
    private int frames, brightness=-1;
    private File pendingExport() { return new File(getCacheDir(),"pending-export.txt"); }

    private int dp(int n) { return Math.round(getResources().getDisplayMetrics().density*n); }
    private void log(String s) {
        while (history.size()>=1800) history.removeFirst();
        history.addLast(System.currentTimeMillis()+" "+s);
    }
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        LinearLayout root=new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xff171925);
        root.setOnApplyWindowInsetsListener((v,in)->{
            v.setPadding(in.getSystemWindowInsetLeft(),in.getSystemWindowInsetTop(),
                    in.getSystemWindowInsetRight(),in.getSystemWindowInsetBottom());
            return in;
        });
        LinearLayout bar=new LinearLayout(this);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        status=new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setTextSize(12);
        status.setPadding(dp(8),0,0,0);
        bar.addView(status,new LinearLayout.LayoutParams(0,-2,1));
        Button wifi=new Button(this);
        wifi.setText("Wi-Fi"); wifi.setMinWidth(0); wifi.setMinimumWidth(0);
        wifi.setOnClickListener(v->startActivity(new Intent(Settings.ACTION_WIFI_SETTINGS)));
        Button logs=new Button(this);
        logs.setText("日志"); logs.setMinWidth(0); logs.setMinimumWidth(0);
        logs.setOnClickListener(v->export());
        bar.addView(wifi); bar.addView(logs);
        root.addView(bar,new LinearLayout.LayoutParams(-1,-2));
        screen=new Screen(this);
        root.addView(screen,new LinearLayout.LayoutParams(-1,0,1));
        pad=new Pad(this);
        root.addView(pad,new LinearLayout.LayoutParams(-1,dp(76)));
        setContentView(root);
        sensors=getSystemService(SensorManager.class);
        hotspot=new Hotspot(this);
        log(Build.MANUFACTURER+" "+Build.MODEL+" Android "+Build.VERSION.RELEASE+" API "+Build.VERSION.SDK_INT);
    }
    @Override protected void onResume() {
        super.onResume();
        Arcade.resume(new File(getFilesDir(),"settings.bin").getPath());
        running=true;
        pad.reset();
        Intent power=registerReceiver(null,new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (power!=null) {
            int level=power.getIntExtra(BatteryManager.EXTRA_LEVEL,-1);
            int scale=power.getIntExtra(BatteryManager.EXTRA_SCALE,100);
            Arcade.battery(scale>0?level*100/scale:-1,power.getIntExtra(BatteryManager.EXTRA_PLUGGED,0)!=0);
        }
        for (int type:new int[]{Sensor.TYPE_ACCELEROMETER,Sensor.TYPE_GYROSCOPE}) {
            Sensor sensor=sensors.getDefaultSensor(type);
            boolean ok=sensor!=null && sensors.registerListener(this,sensor,10000,0);
            log("sensor "+type+" "+(ok?sensor.getName():"unavailable"));
            if (!ok) Toast.makeText(this,"传感器不可用："+type,Toast.LENGTH_LONG).show();
        }
        Sensor orientation=sensors.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);
        if (orientation!=null) sensors.registerListener(this,orientation,10000,0);
        hotspot.start();
        reportStart=deadline=0; frames=0;
        Choreographer.getInstance().postFrameCallback(this);
        log("resume");
    }
    @Override protected void onPause() {
        running=false;
        Choreographer.getInstance().removeFrameCallback(this);
        sensors.unregisterListener(this);
        pad.reset();
        log("pause "+Arcade.status());
        Arcade.pause();
        hotspot.stop();
        super.onPause();
    }
    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (!focus && pad!=null) pad.reset();
    }
    @Override public void onConfigurationChanged(Configuration config) {
        super.onConfigurationChanged(config);
        pad.reset();
    }
    @Override public void onSensorChanged(SensorEvent e) {
        if (!running) return;
        boolean gyro=e.sensor.getType()==Sensor.TYPE_GYROSCOPE;
        boolean portrait=getResources().getConfiguration().orientation==Configuration.ORIENTATION_PORTRAIT;
        int rotation=getWindowManager().getDefaultDisplay().getRotation();
        if (e.sensor.getType()==Sensor.TYPE_ROTATION_VECTOR) {
            SensorManager.getRotationMatrixFromVector(attitude,e.values);
            // Horizontal projection of screen-right in world east/north coordinates.
            int axis=rotation%2;
            float sign=rotation<2 ? 1f : -1f;
            float east=sign*attitude[axis], north=sign*attitude[3+axis];
            if (east*east+north*north>.25f && e.accuracy!=SensorManager.SENSOR_STATUS_UNRELIABLE)
                Arcade.heading((float)Math.toDegrees(Math.atan2(east,north)),e.timestamp);
            return;
        }
        MotionAxes.convert(axes,e.values[0],e.values[1],e.values[2],rotation,portrait,gyro);
        Arcade.motion(gyro,axes[0],axes[1],axes[2],e.timestamp,MotionAxes.invertWheelAxis(rotation));
    }
    @Override public void onAccuracyChanged(Sensor sensor,int accuracy) {}
    @Override public void doFrame(long ns) {
        if (!running) return;
        if (ns+500000>=deadline) {
            int fps=Arcade.step();
            long period=1000000000L/fps;
            deadline=(deadline==0 || ns-deadline>=period) ? ns+period : deadline+period;
            boolean portrait=Arcade.width()==135;
            int orientation=portrait?ActivityInfo.SCREEN_ORIENTATION_PORTRAIT:ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
            if (getRequestedOrientation()!=orientation) setRequestedOrientation(orientation);
            pad.setExtended(Arcade.stick());
            screen.copy(portrait);
            int value=Arcade.brightness();
            if (brightness!=value) {
                WindowManager.LayoutParams params=getWindow().getAttributes();
                params.screenBrightness=value/255f;
                getWindow().setAttributes(params);
                brightness=value;
            }
            if (reportStart==0) reportStart=ns;
            frames++;
            if (ns-reportStart>=1000000000L) {
                String s=Arcade.status()+" | "+Math.round(frames*1e9/(ns-reportStart))+" fps";
                status.setText(s);
                log(s);
                frames=0; reportStart=ns;
            }
        }
        Choreographer.getInstance().postFrameCallback(this);
    }
    private void export() {
        log("export "+Arcade.status());
        try (OutputStream out=new FileOutputStream(pendingExport())) {
            out.write((String.join("\n",history)+"\n").getBytes(StandardCharsets.UTF_8));
        } catch (IOException ex) {
            Toast.makeText(this,ex.toString(),Toast.LENGTH_LONG).show();
            return;
        }
        Intent i=new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                .setType("text/plain").putExtra(Intent.EXTRA_TITLE,"stick-arcade-"+System.currentTimeMillis()+".log");
        startActivityForResult(i,1);
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if (request!=1 || result!=RESULT_OK || data==null || data.getData()==null) return;
        try (OutputStream out=getContentResolver().openOutputStream(data.getData())) {
            if (out==null) throw new IOException("No output stream");
            java.nio.file.Files.copy(pendingExport().toPath(),out);
        } catch (IOException ex) {
            Toast.makeText(this,ex.toString(),Toast.LENGTH_LONG).show();
        }
    }
}
