package dev.stickarcade.player;

import android.content.Context;
import android.graphics.*;
import android.view.View;

final class Screen extends View {
    private final Bitmap wide=Bitmap.createBitmap(240,135,Bitmap.Config.RGB_565);
    private final Bitmap tall=Bitmap.createBitmap(135,240,Bitmap.Config.RGB_565);
    private final Rect dest=new Rect();
    private final Paint pixels=new Paint();
    private Bitmap frame=wide;

    Screen(Context c) { super(c); pixels.setFilterBitmap(false); }
    void copy(boolean portrait) {
        frame=portrait?tall:wide;
        Arcade.blit(frame);
        invalidate();
    }
    @Override protected void onDraw(Canvas c) {
        float scale=Math.min((float)getWidth()/frame.getWidth(),(float)getHeight()/frame.getHeight());
        // Integer scaling when possible preserves the original pixel grid.
        if (scale>=1) scale=(float)Math.floor(scale);
        int w=Math.round(frame.getWidth()*scale), h=Math.round(frame.getHeight()*scale);
        int x=(getWidth()-w)/2, y=(getHeight()-h)/2;
        dest.set(x,y,x+w,y+h);
        c.drawBitmap(frame,null,dest,pixels);
    }
}
