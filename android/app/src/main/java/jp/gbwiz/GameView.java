package jp.gbwiz;
import android.content.Context;
import android.graphics.*;
import android.view.MotionEvent;
import android.view.View;
import android.text.TextPaint;
import android.text.TextUtils;
import java.util.Map;

final class GameView extends View {
    static final int MENU_ROWS=12;
    private static final String[] CRT_MODES={"OFF","弱","標準"};
    interface Actions {void pressed(int key);void menuItem(int row,int adjust);default void itemTap(int x,int y){}}
    private Bitmap frame=Bitmap.createBitmap(160,144,Bitmap.Config.ARGB_8888);
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint videoPaint=new Paint();
    private final Paint crtPaint=new Paint();
    private final RadialGradient crtEdges=new RadialGradient(0,0,1f,0x00000000,0xff000000,Shader.TileMode.CLAMP);
    private final TextPaint menuPaint=new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private ControllerLayout layout;
    private final Actions actions;
    private final String[] labels={"A","B","Select","Start","→","←","↑","↓","ファイル","設定","SAVE 1","LOAD 1","リセット","マップ","アイテム","L","R","X","Y"};
    private boolean sfc;
    private int active;
    String[] itemLines;
    int mapFloor,mapRegion;
    private boolean blocked;
    int buttons,menuRow,slot=1,speedHalf=2,crtMode;
    boolean menu,swap,swapXY,skipLogo=true,mapMode;
    boolean supportMenu,rewardSupported,goldRewardSupported;
    int effects,effectsSupported,gameOptions,gameOptionsSupported;
    private static final String[] RACES={"人間","エルフ","ドワーフ","ノーム","ホビット","フェアリー","ドラコン","ラウルフ","ムーク","リズマン"};
    private static final String[] JOBS={"戦士","魔法使い","僧侶","盗賊","ビショップ","サムライ","ロード","ニンジャ","アルケミスト","バード","レインジャー","バルキリー"};
    private static final String[] ALIGNMENTS={"善","中立","悪"};
    boolean characterMenu;
    boolean characterSpellsSupported;
    int characterRaceCount,characterJobCount;
    int characterAlignment=-1,characterRace=-1,characterJob=-1;
    String characterName="";
    boolean bonusSupported,maxBonus,mpSupported,noMpCost,partySupported,mixedParty;
    int experienceMultiplier=1,goldMultiplier=1;
    String navigationHint="上下:選択  左右:変更  A:決定  B:戻る";
    String[] browserItems;
    String[] rosterNames;
    boolean rosterSupported;
    int rosterRow,rosterFrom=-1;
    String[] spellCharacters;
    int spellRow;
    int browserRow;
    private RectF menuBox;
    int supportRowCount(){
        return gameOptionsSupported==0?(rosterSupported?13:12):3+Integer.bitCount(gameOptionsSupported);
    }
    int gameOptionBit(int row){
        int options=gameOptionsSupported;
        for(int i=3;i<row&&options!=0;++i)options&=options-1;
        return Integer.lowestOneBit(options);
    }
    private String[] supportRows(){
        String[] rows=new String[supportRowCount()];
        rows[0]="戻る";
        rows[1]="獲得経験値   "+(rewardSupported?"‹ ×"+experienceMultiplier+" ›":"対象外");
        rows[2]="獲得お金   "+(goldRewardSupported?"‹ ×"+goldMultiplier+" ›":"対象外");
        if(gameOptionsSupported!=0){
            String[] names=gameOptionsSupported==31?
                new String[]{"仲間加入率アップ","ドロップ確率アップ","装備制限解除","アイテム非消費","仲間モンスターのレベル上限99"}:
                new String[]{gameOptionsSupported==3?"武器・魔法の熟練度最大":"ジョブ熟練度最大","MP消費なし","キャパシティ消費なし"};
            for(int row=3;row<rows.length;++row){
                int bit=gameOptionBit(row);
                rows[row]=names[Integer.numberOfTrailingZeros(bit)]+"   "+((gameOptions&bit)!=0?"‹ ON ›":"‹ OFF ›");
            }
            return rows;
        }
        rows[3]="作成ボーナス最大   "+(bonusSupported?"‹ "+(maxBonus?"ON":"OFF")+" ›":"対象外");
        rows[4]="キャラクターを選択";
        rows[5]="MP消費なし   "+(mpSupported?"‹ "+(noMpCost?"ON":"OFF")+" ›":"対象外");
        rows[6]="善悪混合   "+(partySupported?"‹ "+(mixedParty?"ON":"OFF")+" ›":"対象外");
        rows[7]="パーティーのアイテム編集";
        rows[8]=effectRow("常時 灯り",1);
        rows[9]=effectRow("常時 保護",2);
        rows[10]=effectRow("常時 識別",4);
        rows[11]=effectRow("常時 浮遊",8);
        if(rosterSupported)rows[12]="名簿の並べ替え";
        return rows;
    }
    private String[] rosterRows(){
        String[] rows=new String[rosterNames.length];
        for(int i=0;i<rows.length;i++){
            int source=i;
            if(rosterFrom>=0){
                if(i==rosterRow)source=rosterFrom;
                else if(rosterFrom<=i&&i<rosterRow)source=i+1;
                else if(rosterRow<i&&i<=rosterFrom)source=i-1;
            }
            rows[i]=(i==rosterRow&&rosterFrom>=0?"* ":"")+(i+1)+"  "+
                (rosterNames[source]==null?"空き":rosterNames[source]);
        }
        return rows;
    }
    private String effectRow(String label,int bit){
        return label+"   "+((effectsSupported&bit)==0?"対象外":((effects&bit)!=0?"‹ ON ›":"‹ OFF ›"));
    }
    private float rowTop,rowHeight;
    int characterRowCount(){return characterRaceCount>0?5:3;}
    private static String choice(int index,String[] labels){return index>=0?"‹ "+labels[index]+" ›":"対象外";}
    private String[] characterRows(){
        String[] rows=new String[characterRowCount()];
        rows[0]="対象   ‹ "+characterName+" ›";
        rows[1]=characterSpellsSupported?"全呪文習得":"全呪文習得  対象外";
        rows[2]="属性   "+choice(characterAlignment,ALIGNMENTS);
        if(characterRaceCount>0){
            rows[3]="種族   "+choice(characterRace,RACES);
            rows[4]="職業   "+choice(characterJob,JOBS);
        }
        return rows;
    }
    GameView(Context context,Actions actions) {super(context);this.actions=actions;setFocusable(true);setContentDescription("WizPlayer ゲーム画面とタッチコントローラー");}
    void frameSize(int width,int height) {
        if(frame.getWidth()!=width||frame.getHeight()!=height) {
            frame=Bitmap.createBitmap(width,height,Bitmap.Config.ARGB_8888);
            updateLayout(getWidth(),getHeight());
        }
    }
    void sfcControls(boolean enabled) {
        if(sfc!=enabled) {sfc=enabled;clearInput();updateLayout(getWidth(),getHeight());}
    }
    void frame(int[] pixels) {
        int width=frame.getWidth(),height=frame.getHeight();
        frame.setPixels(pixels,0,width,0,0,width,height);invalidate();
    }
    void clearInput() {if(active!=0)blocked=true;active=buttons=0;invalidate();}
    private RectF rect(int[] b) {return new RectF(b[0],b[1],b[0]+b[2],b[1]+b[3]);}
    private void updateLayout(int w,int h) {
        if(w>0&&h>0) layout=new ControllerLayout(w,h,getResources().getDisplayMetrics().density,
            (float)frame.getHeight()/frame.getWidth(),sfc);
    }
    @Override protected void onSizeChanged(int w,int h,int ow,int oh) {clearInput();updateLayout(w,h);}
    private void text(Canvas c,String text,RectF area,int color,float fraction) {
        paint.setColor(color);paint.setTypeface(Typeface.create("sans-serif",Typeface.BOLD));paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(area.height()*fraction);
        float width=paint.measureText(text);if(width>area.width()*.88f)paint.setTextSize(paint.getTextSize()*area.width()*.88f/width);
        c.drawText(text,area.centerX(),area.centerY()-(paint.ascent()+paint.descent())/2,paint);
    }
    private void menuText(Canvas canvas,String label,RectF area,int color,float size) {
        menuPaint.setColor(color);menuPaint.setTypeface(Typeface.create("sans-serif",Typeface.BOLD));
        menuPaint.setTextAlign(Paint.Align.CENTER);menuPaint.setTextSize(size);
        int marker=label.indexOf("@ROM:");
        if(marker>=0&&(characterMenu||spellCharacters!=null||rosterNames!=null)) {
            int start=marker+5,end=start;
            while(end<label.length()&&Character.digit(label.charAt(end),16)>=0)end++;
            int count=(end-start)/16;
            if(count>0&&(end-start)%16==0) {
                String prefix=label.substring(0,marker),suffix=label.substring(end);
                float width=menuPaint.measureText(prefix)+count*size+menuPaint.measureText(suffix);
                size*=Math.min(1f,area.width()*.88f/width);
                menuPaint.setTextSize(size);menuPaint.setTextAlign(Paint.Align.LEFT);
                float prefixWidth=menuPaint.measureText(prefix),suffixWidth=menuPaint.measureText(suffix);
                float x=area.centerX()-(prefixWidth+count*size+suffixWidth)/2;
                float baseline=area.centerY()-(menuPaint.ascent()+menuPaint.descent())/2;
                canvas.drawText(prefix,x,baseline,menuPaint);
                x+=prefixWidth;
                paint.setColor(color);paint.setAntiAlias(false);
                float dot=size/8,top=area.centerY()-size/2;
                for(int tile=0;tile<count;tile++)for(int y=0;y<8;y++) {
                    int pos=start+tile*16+y*2;
                    int bits=Character.digit(label.charAt(pos),16)*16+Character.digit(label.charAt(pos+1),16);
                    for(int col=0;col<8;col++)if((bits&(128>>col))!=0) {
                        float px=x+tile*size+col*dot,py=top+y*dot;
                        canvas.drawRect(px,py,px+dot,py+dot,paint);
                    }
                }
                paint.setAntiAlias(true);
                canvas.drawText(suffix,x+count*size,baseline,menuPaint);
                return;
            }
        }
        CharSequence fitted=TextUtils.ellipsize(label,menuPaint,area.width()*.88f,TextUtils.TruncateAt.END);
        canvas.drawText(fitted.toString(),area.centerX(),area.centerY()-(menuPaint.ascent()+menuPaint.descent())/2,menuPaint);
    }
    private void drawVideo(Canvas canvas,RectF screen) {
        boolean crt=crtMode!=0&&!mapMode&&itemLines==null;
        videoPaint.setFilterBitmap(crt);
        canvas.drawBitmap(frame,null,screen,videoPaint);
        if(!crt)return;

        canvas.save();
        canvas.clipRect(screen);
        // Spread a small portion of the signal horizontally before adding scanlines.
        float spread=screen.width()/frame.getWidth()*.5f;
        videoPaint.setAlpha(crtMode==1?24:44);
        canvas.save();
        canvas.translate(-spread,0);
        canvas.drawBitmap(frame,null,screen,videoPaint);
        canvas.translate(spread*2,0);
        canvas.drawBitmap(frame,null,screen,videoPaint);
        canvas.restore();
        videoPaint.setAlpha(255);

        float lineHeight=screen.height()/frame.getHeight();
        if(lineHeight>=2f) {
            crtPaint.setColor(crtMode==1?0x22000000:0x46000000);
            for(int row=0;row<frame.getHeight();row++) {
                float bottom=screen.top+(row+1)*lineHeight;
                canvas.drawRect(screen.left,bottom-lineHeight*.35f,screen.right,bottom,crtPaint);
            }
        }
        canvas.translate(screen.centerX(),screen.centerY());
        canvas.scale(screen.width()*.72f,screen.height()*.72f);
        crtPaint.setShader(crtEdges);
        crtPaint.setAlpha(crtMode==1?22:44);
        canvas.drawRect(-1,-1,1,1,crtPaint);
        crtPaint.setShader(null);
        canvas.restore();
    }
    @Override protected void onDraw(Canvas canvas) {
        canvas.drawColor(0xff101722);if(layout==null)return;
        RectF screen=rect(layout.screen);paint.setColor(Color.BLACK);canvas.drawRect(screen,paint);
        drawVideo(canvas,screen);
        if(mapMode&&mapFloor>0) {
            RectF heading=new RectF(screen.left,screen.top,screen.right,screen.top+screen.height()/12);
            String region=mapRegion>0?"-"+mapRegion:"";
            text(canvas,"マップ B"+mapFloor+region,heading,0xffe0e6ee,.65f);
        }
        if(menu||browserItems!=null||spellCharacters!=null)drawMenu(canvas,screen);
        if(itemLines!=null){
            paint.setColor(Color.BLACK);canvas.drawRect(screen,paint);
            float sx=screen.width()/frame.getWidth(),sy=screen.height()/itemCanvasHeight();
            paint.setColor(0xffe6e6d7);paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(Math.max(1,sx/2));
            if(frame.getWidth()==160) {
                canvas.drawRect(screen.left,screen.top,screen.centerX(),screen.top+98*sy,paint);
                canvas.drawRect(screen.centerX(),screen.top,screen.right,screen.top+98*sy,paint);
            } else {
                canvas.drawRect(screen.left,screen.top,screen.centerX(),screen.top+194*sy,paint);
                canvas.drawRect(screen.centerX(),screen.top,screen.right,screen.top+194*sy,paint);
            }
            paint.setStyle(Paint.Style.FILL);
            menuPaint.setTypeface(Typeface.MONOSPACE);menuPaint.setTextAlign(Paint.Align.LEFT);
            menuPaint.setTextSize((frame.getWidth()==160?8:13)*sy);
            for(String line:itemLines){String[] parts=line.split("\\|",6);if(parts.length!=6)continue;
                float x=Float.parseFloat(parts[0]),y=Float.parseFloat(parts[1]),w=Float.parseFloat(parts[2]),h=Float.parseFloat(parts[3]);boolean selected=parts[4].equals("1");
                RectF box=new RectF(screen.left+x*sx,screen.top+y*sy,screen.left+(x+w)*sx,screen.top+(y+h)*sy);
                paint.setColor(selected?0xffe6e6d7:Color.BLACK);canvas.drawRect(box,paint);menuPaint.setColor(selected?Color.BLACK:0xffe6e6d7);
                canvas.save();canvas.clipRect(box);
                String label=parts[5];
                if(label.startsWith("@ROM:")&&(label.length()-5)%16==0){
                    paint.setColor(selected?Color.BLACK:0xffe6e6d7);paint.setAntiAlias(false);
                    int columns=Math.min((label.length()-5)/16,(int)(w/8));float glyphTop=box.centerY()-4*sy;
                    for(int col=0;col<columns;col++)for(int yy=0;yy<8;yy++){
                        int pos=5+col*16+yy*2,bits=(Character.digit(label.charAt(pos),16)<<4)|Character.digit(label.charAt(pos+1),16);
                        for(int xx=0;xx<8;xx++)if((bits&(128>>xx))!=0){float px=box.left+(col*8+xx)*sx,py=glyphTop+yy*sy;canvas.drawRect(px,py,px+sx,py+sy,paint);}
                    }
                    paint.setAntiAlias(true);
                }else canvas.drawText(TextUtils.ellipsize(label,menuPaint,box.width(),TextUtils.TruncateAt.END).toString(),box.left,box.centerY()-(menuPaint.ascent()+menuPaint.descent())/2,menuPaint);
                canvas.restore();
            }
        }
        for(Map.Entry<Integer,int[]> entry:layout.buttons.entrySet()) {
            int key=entry.getKey();RectF box=rect(entry.getValue());
            boolean circle=key<=3||key>=17;
            int color=(active&(1<<key))!=0?0xffb47442:(circle?0xffa3333f:0xff344154);
            if(RectF.intersects(box,screen))color=(color&0x00ffffff)|0xb0000000;
            paint.setColor(color);
            if(circle)canvas.drawOval(box,paint);else canvas.drawRoundRect(box,4,4,paint);
            int previous=sfc?15:12,next=sfc?16:10;
            text(canvas,itemLines!=null&&key==previous?"L / 前の人":itemLines!=null&&key==next?"R / 次の人":
                mapMode&&key==previous?"L / 階層↑":mapMode&&key==next?"R / 階層↓":labels[key],
                box,Color.WHITE,key<=3?.3f:key<=7?.6f:.4f);
        }
    }
    private void drawMenu(Canvas c,RectF screen) {
        menuBox=new RectF(screen.left+screen.width()*.025f,screen.top+screen.height()*.035f,screen.right-screen.width()*.025f,screen.bottom-screen.height()*.035f);
        paint.setColor(0xff080c12);c.drawRect(menuBox,paint);
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(2);paint.setColor(0xffaab8c8);c.drawRect(menuBox,paint);paint.setStyle(Paint.Style.FILL);
        String[] items=browserItems!=null?browserItems:rosterNames!=null?rosterRows():spellCharacters;
        int visibleRows=items!=null?7:characterMenu?characterRowCount():supportMenu?supportRowCount():MENU_ROWS;
        float header=menuBox.height()*.13f;rowHeight=menuBox.height()*.735f/visibleRows;rowTop=menuBox.top+header;
        menuText(c,browserItems!=null?"ROM選択":rosterNames!=null?"名簿の並べ替え":spellCharacters!=null?"対象キャラクター選択":characterMenu?"キャラクター編集":supportMenu?"サポート":"機能メニュー",new RectF(menuBox.left,menuBox.top,menuBox.right,rowTop),0xfff0f2f5,header*.46f);
        String speed=speedHalf%2==0?Integer.toString(speedHalf/2):"1.5";
        String[] rows=items!=null?items:characterMenu?characterRows():supportMenu?supportRows():new String[]{"スロット   ‹  "+slot+" / 5  ›","ステート保存","ステート読込","再開","A / B 入替   ‹  "+(swap?"ON":"OFF")+"  ›","ROM選択","リセット","ロゴスキップ   ‹  "+(skipLogo?"ON":"OFF")+"  ›","速度   ‹  "+speed+"倍  ›  音声1倍","サポート","X / Y 入替   ‹  "+(swapXY?"ON":"OFF")+"  ›","ブラウン管   ‹  "+CRT_MODES[crtMode]+"  ›"};
        int selected=browserItems!=null?browserRow:rosterNames!=null?rosterRow:spellCharacters!=null?spellRow:menuRow,first=items!=null?(selected/7)*7:0;
        // Use the main menu's font size in every submenu. Row spacing may
        // grow for shorter lists, but font size never depends on row count,
        // label length, ROM capabilities or the selection marker.
        float itemTextSize=menuBox.height()*.735f/10*.53f;
        for(int i=0;i<visibleRows&&first+i<rows.length;i++) {
            RectF r=new RectF(menuBox.left+8,rowTop+i*rowHeight,menuBox.right-8,rowTop+(i+1)*rowHeight);
            if(first+i==selected){paint.setColor(0xff2c3c51);c.drawRect(r,paint);}
            menuText(c,(first+i==selected?"›  ":"")+rows[first+i],r,first+i==selected?0xffffdc83:0xffe0e6ee,itemTextSize);
        }
        String hint=rosterNames!=null?(rosterFrom>=0?"A:保存してタイトルへ  B:取消":"上下:選択  A:つかむ  B:戻る"):
            spellCharacters!=null?"上下:選択  A:決定  B:戻る":characterMenu?"上下:選択  左右:変更  A:実行  B:戻る":navigationHint;
        menuText(c,hint,new RectF(menuBox.left,rowTop+visibleRows*rowHeight,menuBox.right,menuBox.bottom),0xffa7b4c5,menuBox.height()*.135f*.3f);
    }
    private int itemCanvasHeight(){return frame.getWidth()==256?240:frame.getHeight();}
    @Override public boolean onTouchEvent(MotionEvent event) {
        if(layout==null)return true;
        int type=event.getActionMasked();
        RectF screen=rect(layout.screen);
        if(itemLines!=null&&type==MotionEvent.ACTION_DOWN&&screen.contains(event.getX(),event.getY())){
            actions.itemTap((int)((event.getX()-screen.left)*frame.getWidth()/screen.width()),
                (int)((event.getY()-screen.top)*itemCanvasHeight()/screen.height()));
            blocked=true;return true;
        }
        if(type==MotionEvent.ACTION_UP||type==MotionEvent.ACTION_CANCEL) {blocked=false;active=buttons=0;invalidate();return true;}
        if(blocked)return true;
        if((menu||browserItems!=null||spellCharacters!=null)&&type==MotionEvent.ACTION_DOWN&&menuBox!=null&&menuBox.contains(event.getX(),event.getY())) {
            int row=(int)((event.getY()-rowTop)/rowHeight);
            String[] items=browserItems!=null?browserItems:rosterNames!=null?rosterNames:spellCharacters;
            if(event.getY()>=rowTop&&row>=0&&row<(items!=null?7:characterMenu?characterRowCount():supportMenu?supportRowCount():MENU_ROWS)) {
                int adjust=items==null&&(characterMenu?row>=2:supportMenu?(gameOptionsSupported!=0?row>=1:(row>=1&&row<=3)||row==5||row==6||(row>=8&&row<12)):(row==0||row==4||row==7||row==8||row==10))?(event.getX()<menuBox.centerX()?-1:1):0;
                int selected=browserItems!=null?browserRow:rosterNames!=null?rosterRow:spellRow,index=items==null?row:(selected/7)*7+row;
                if(items==null||index<items.length)actions.menuItem(index,adjust);blocked=true;
            }
            return true;
        }
        int next=0;
        for(int p=0;p<event.getPointerCount();p++) {
            if(type==MotionEvent.ACTION_POINTER_UP&&p==event.getActionIndex())continue;
            for(Map.Entry<Integer,int[]> entry:layout.buttons.entrySet())if(rect(entry.getValue()).contains(event.getX(p),event.getY(p)))next|=1<<entry.getKey();
        }
        int pressed=next&~active;active=next;buttons=(next&255)|((next>>>7)&0xf00);
        for(int i=0;i<labels.length;i++)if((pressed&(1<<i))!=0)actions.pressed(i);
        invalidate();return true;
    }
}
