package dev.stickarcade.player;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;

final class Pad extends View {
    private final TouchState state = new TouchState();
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float[] centers = new float[5];
    private boolean extended;
    private float radius, cy;

    Pad(Context c) { super(c); setContentDescription("Game controls"); }
    void setExtended(boolean on) {
        if (on != extended) { reset(); extended = on; layoutPad(); invalidate(); }
    }
    void reset() { state.clear(); Arcade.clearControls(); invalidate(); }
    private void layoutPad() {
        cy = getHeight()/2f;
        radius = Math.min(cy*.86f, getWidth()/(extended ? 9.3f : 5f));
        centers[TouchState.A] = getWidth()*(extended ? .86f : .66f);
        centers[TouchState.B] = getWidth()*(extended ? .64f : .34f);
        centers[TouchState.JUMP] = getWidth()*.41f;
        centers[TouchState.STICK] = getWidth()*.16f;
    }
    @Override protected void onSizeChanged(int w,int h,int oldW,int oldH) { reset(); layoutPad(); }
    @Override protected void onDraw(Canvas c) {
        for (int i=1; i <= (extended ? 4 : 2); i++) {
            float cx = centers[i];
            paint.setColor(state.held(i) ? 0xff555d87 : 0xff353950);
            c.drawCircle(cx,cy,radius,paint);
            if (i == TouchState.STICK) {
                paint.setColor(0xfff8bb56);
                c.drawCircle(cx+state.x*radius/160,cy-state.y*radius/160,radius*.38f,paint);
            } else {
                paint.setColor(i == TouchState.A ? 0xffffcf68 : 0xffc8ddff);
                paint.setTextSize(radius*(i == TouchState.JUMP ? .51f : .86f));
                paint.setTextAlign(Paint.Align.CENTER);
                String label = i==TouchState.A ? "A" : i==TouchState.B ? "B" : "JUMP";
                c.drawText(label,cx,cy-(paint.descent()+paint.ascent())/2,paint);
            }
        }
    }
    private int hit(float x,float y) {
        for (int i=1; i<=(extended?4:2); i++)
            if (Math.hypot(x-centers[i],y-cy) <= radius*1.12f) return i;
        return TouchState.NONE;
    }
    @Override public boolean onTouchEvent(MotionEvent e) {
        int action=e.getActionMasked(), idx=e.getActionIndex();
        if (action==MotionEvent.ACTION_CANCEL) { reset(); return true; }
        int id=e.getPointerId(idx);
        if (action==MotionEvent.ACTION_DOWN || action==MotionEvent.ACTION_POINTER_DOWN)
            state.down(id,hit(e.getX(idx),e.getY(idx)));
        for (int p=0; p<e.getPointerCount() && radius>0; p++) {
            state.move(e.getPointerId(p), Math.round((e.getX(p)-centers[TouchState.STICK])*100/radius),
                    Math.round((cy-e.getY(p))*100/radius));
        }
        if (action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_POINTER_UP) state.up(id);
        Arcade.controls(state.held(TouchState.A),state.held(TouchState.B),state.held(TouchState.JUMP),state.x,state.y);
        invalidate();
        return true;
    }
}
