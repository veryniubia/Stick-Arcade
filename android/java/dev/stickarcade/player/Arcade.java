package dev.stickarcade.player;

import android.graphics.Bitmap;

final class Arcade {
    static { System.loadLibrary("stickarcade"); }
    static native void resume(String settings);
    static native void pause();
    static native void network(long handle);
    static native void motion(boolean gyro, float x, float y, float z, long timestampNs);
    static native void heading(float degrees, long timestampNs);
    static native void controls(boolean a, boolean b, boolean jump, int x, int y);
    static native void clearControls();
    static native void battery(int percent, boolean usb);
    static native int step();
    static native int width();
    static native boolean stick();
    static native int brightness();
    static native void blit(Bitmap bitmap);
    static native String status();
}
