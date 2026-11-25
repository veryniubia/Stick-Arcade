package dev.stickarcade.player;

import java.util.HashMap;

// Assign a finger on DOWN. Moving the joystick or lifting a different finger
// must never steal a held action. This class also runs in the host JVM checks.
final class TouchState {
    static final int NONE=0, A=1, B=2, JUMP=3, STICK=4;
    private final HashMap<Integer,Integer> fingers = new HashMap<>();
    int x, y;

    void down(int id, int control) {
        if (control != NONE && (control != STICK || !held(STICK))) fingers.put(id, control);
    }
    void move(int id, int dx, int dy) {
        if (fingers.getOrDefault(id,NONE) != STICK) return;
        double scale = Math.max(1, Math.hypot(dx,dy)/100);
        x = (int)Math.round(dx/scale);
        y = (int)Math.round(dy/scale);
    }
    void up(int id) {
        Integer old = fingers.remove(id);
        if (old != null && old == STICK) x = y = 0;
    }
    void clear() { fingers.clear(); x = y = 0; }
    boolean held(int key) { return fingers.containsValue(key); }
}
