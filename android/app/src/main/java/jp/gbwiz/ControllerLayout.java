package jp.gbwiz;
import java.util.LinkedHashMap;
import java.util.Map;

/** Fits the game to the safe display area before arranging the controls. */
final class ControllerLayout {
    final Map<Integer,int[]> buttons=new LinkedHashMap<>();
    int[] screen;
    private void put(int key,float x,float y,float w,float h) {
        buttons.put(key,new int[]{Math.round(x),Math.round(y),Math.max(1,Math.round(w)),Math.max(1,Math.round(h))});
    }
    ControllerLayout(int width,int height,float density) {
        this(width,height,density,0.9f,false);
    }
    ControllerLayout(int width,int height,float density,float aspect,boolean sfc) {
        float w=width,h=height,g=8*density,small=Math.min(40*density,h*.075f);
        boolean portrait=w<=h;
        float ratio=sfc?.75f:aspect;
        float sw=portrait?w:Math.min(w,h/ratio);
        float sh=portrait?Math.min(h,w*ratio):sw*ratio;
        float top=portrait?Math.min(2*(small+g),h-sh):(h-sh)/2;
        screen=new int[]{Math.round((w-sw)/2),Math.round(top),Math.round(sw),Math.round(sh)};
        if(sfc) {layoutSfc(w,h,density,g,small,top+sh,(w-sw)/2);return;}
        if(w<=h) {
            put(13,w*.375f,0,w*.25f,small);
            put(14,w*.375f,small+g,w*.25f,small);
            put(8,g,0,w*.25f,small); put(9,w-w*.25f-g,0,w*.25f,small);
            float secondary=Math.min(top+sh+g,h-small-3*Math.min(w*.12f,44*density)-2*g);
            float utility=(w-4*g)/3;
            put(12,g,secondary,utility,small);
            put(10,2*g+utility,secondary,utility,small);
            put(11,3*g+2*utility,secondary,utility,small);
            float controlsTop=secondary+small+g,available=h-g-controlsTop;
            float key=Math.min(Math.min(w*.175f,94*density),available/3.15f);
            float dx=w*.025f,dy=controlsTop+(available-3*key)/2;
            put(6,dx+key,dy,key,key);put(5,dx,dy+key,key,key);
            put(4,dx+2*key,dy+key,key,key);put(7,dx+key,dy+2*key,key,key);
            float diameter=Math.min(Math.min(w*.195f,104*density),available/2.5f);
            float right=w-g-diameter,left=right-diameter-w*.025f,cluster=controlsTop+(available-2.35f*diameter)/2;
            put(2,left,cluster+diameter*.25f,diameter,diameter);put(3,right,cluster,diameter,diameter);
            put(1,left,cluster+diameter*1.35f,diameter,diameter);put(0,right,cluster+diameter*1.1f,diameter,diameter);
        } else {
            float side=Math.max((w-sw)/2,Math.min(w*.22f,h*.24f));
            float bw=(side-3*g)/2;
            put(8,g,g,bw,small);put(9,2*g+bw,g,bw,small);
            put(13,g,small+3*g,bw,small);
            put(14,g,2*small+4*g,bw,small);
            put(10,2*g+bw,small+3*g,bw,small);
            float controlTop=3*small+5*g;
            float key=Math.min(Math.min(Math.min(Math.min(side*.32f,94*density),h*.198f),(side-g)/3),(h-g-controlTop)/3);
            float dx=(side-3*key)/2,dy=Math.min(Math.max(controlTop,h*.35f),h-g-3*key);
            put(6,dx+key,dy,key,key);put(5,dx,dy+key,key,key);put(4,dx+2*key,dy+key,key,key);put(7,dx+key,dy+2*key,key,key);
            float diameter=Math.min(side*.41f,h*.21f),rs=w-side,left=rs+side*.05f,right=rs+side*.52f,cluster=h*.34f;
            put(2,left,cluster+h*.05f,diameter,diameter);put(3,right,cluster,diameter,diameter);
            put(1,left,cluster+h*.29f,diameter,diameter);put(0,right,cluster+h*.24f,diameter,diameter);
            put(11,rs+g,g,side-2*g,small);
            put(12,rs+g,small+3*g,side-2*g,small);
        }
    }
    private void layoutSfc(float w,float h,float density,float gap,float small,float screenBottom,float side) {
        boolean portrait=w<=h;
        float bottom=h-gap;
        float controlsTop=portrait?Math.min(screenBottom+gap,h-3*small-5*gap-3*Math.min(w*.12f,40*density)):2*(small+gap);
        float rail=Math.max(side,Math.min(w*.24f,h*.25f));
        float key=Math.min((portrait?60:54)*density,Math.min(portrait?w/6.2f:(rail-2*gap)/2.8f,
            (h-controlsTop-2*small-3*gap)/2.8f));
        float pitch=.9f*key,group=key+2*pitch,left=gap,right=w-gap-group;
        float groupY=portrait?bottom-small-gap-group:controlsTop+(bottom-controlsTop-group)/2;
        float shoulderY=groupY-small-gap;
        if(portrait) {
            put(8,gap,0,w*.22f,small);put(9,w-gap-w*.22f,0,w*.22f,small);
            put(13,w*.38f,0,w*.24f,small);put(14,w*.38f,small+gap,w*.24f,small);
            put(10,gap,small+gap,w*.22f,small);put(11,w-gap-w*.22f,small+gap,w*.22f,small);
        } else {
            float column=(rail-3*gap)/2,second=2*gap+column;
            put(8,gap,gap,column,small);put(9,second,gap,column,small);
            put(13,gap,small+2*gap,column,small);put(14,second,small+2*gap,column,small);
            put(10,w-rail+gap,gap,column,small);put(11,w-rail+second,gap,column,small);
        }
        put(6,left+pitch,groupY,key,key);put(5,left,groupY+pitch,key,key);
        put(4,left+2*pitch,groupY+pitch,key,key);put(7,left+pitch,groupY+2*pitch,key,key);
        put(17,right+pitch,groupY,key,key);put(18,right,groupY+pitch,key,key);
        put(0,right+2*pitch,groupY+pitch,key,key);put(1,right+pitch,groupY+2*pitch,key,key);
        put(15,left,shoulderY,.55f*group,small);put(16,right+.45f*group,shoulderY,.55f*group,small);
        put(2,portrait?w/2-2*small-gap:left,bottom-small,2*small,small);
        put(3,portrait?w/2+gap:w-gap-2*small,bottom-small,2*small,small);
    }
}
