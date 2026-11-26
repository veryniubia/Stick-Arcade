package dev.stickarcade.player;

public final class InputCheck {
    static void check(boolean ok) { if (!ok) throw new AssertionError(); }
    public static void main(String[] args) {
        TouchState s = new TouchState();
        s.down(10,TouchState.STICK); s.move(10,100,100);
        s.down(16,TouchState.A); s.down(3,TouchState.B); s.down(9,TouchState.JUMP);
        check(s.x==71 && s.y==71 && s.held(TouchState.A) && s.held(TouchState.B));
        s.up(3); check(s.held(TouchState.JUMP) && !s.held(TouchState.B) && s.held(TouchState.A));
        s.down(22,TouchState.STICK); s.move(22,-90,0); check(s.x==71);
        s.up(10); check(s.x==0 && s.y==0);
        s.down(20,TouchState.A); s.up(16); check(s.held(TouchState.A));
        s.clear(); check(!s.held(TouchState.A) && !s.held(TouchState.JUMP) && s.x==0 && s.y==0);
        float[] out=new float[3];
        // Device sensor vectors for lowering the current screen's RIGHT edge.
        float[][] bankRight={{-1,0},{0,1},{1,0},{0,-1}};
        for (int r=0; r<4; ++r) {
            float[][] right={{1,0},{0,1},{-1,0},{0,-1}};
            float x=right[r][0], y=right[r][1];
            MotionAxes.convert(out,9.80665f*x,9.80665f*y,0,r,false,false);
            check(Math.abs(out[1]-1)<.001 && out[0]==0);
            MotionAxes.convert(out,-x,-y,1,r,true,true);
            check(Math.abs(out[0]-57.2958f)<.001 && Math.abs(out[2]-57.2958f)<.001);
            for (int direction=-1;direction<=1;++direction) {
                MotionAxes.convert(out,4.9f*bankRight[r][0]*direction,4.9f*bankRight[r][1]*direction,8.5f,r,true,false);
                float wheel=MotionAxes.invertWheelAxis(r) ? -out[0] : out[0];
                check(Math.abs(wheel-direction*.5f)<.001);
            }
        }
        System.out.println("PASS multi-touch, neutral reset, four rotations and SI conversion");
    }
}
