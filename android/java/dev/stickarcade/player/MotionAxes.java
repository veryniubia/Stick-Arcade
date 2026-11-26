package dev.stickarcade.player;

// Sensor axes follow the natural display, including devices with a landscape natural display.
final class MotionAxes {
    // Portrait racing reads body X (minus the legacy right component).
    static boolean invertWheelAxis(int rotation) { return (rotation & 1) != 0; }

    static void convert(float[] out, float x, float y, float z, int rotation, boolean portrait, boolean gyro) {
        float right, up;
        switch (rotation) {
            case 1: right = y; up = -x; break;
            case 2: right = -x; up = -y; break;
            case 3: right = -y; up = x; break;
            default: right = x; up = y;
        }
        float scale = gyro ? (float)(180 / Math.PI) : 1f / 9.80665f;
        // Stick right-edge elevation uses +Y in landscape. Portrait tilt uses
        // atan2(X,Y), positive clockwise. GyroLook projects rate onto gravity.
        out[0] = (portrait ? -right : up) * scale;
        out[1] = (portrait ? up : right) * scale;
        out[2] = z * scale;
    }
}
