package jp.gbwiz;
import android.app.*;
import android.os.*;
import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.provider.*;
import android.graphics.Color;
import android.media.*;
import android.view.*;
import android.widget.*;
import android.hardware.input.InputManager;
import java.io.*;
import java.util.Arrays;
import java.util.Properties;
import java.util.List;
import java.util.Locale;
import java.util.ArrayList;

public final class MainActivity extends Activity implements Choreographer.FrameCallback,InputManager.InputDeviceListener {
    private static final int ROM=1,FOLDER=2,ROM_FOLDER=3;
    private static final String TURBO_FILE="turbofile.tbf";
    private static final byte[] EMPTY_TURBO_FILE=new byte[8192];
    private static final int[] SPEEDS={2,3,4,6,8,10,20};
    private long core,lastFrame,lastSave;
    private final int[] pixels=new int[256*240];
    private final short[] pcm=new short[4096];
    private GameView view;
    private TextView notification;
    private final Handler notificationHandler=new Handler(Looper.getMainLooper());
    private final Runnable clearNotification=()->{if(notification!=null){notification.setText("");notification.setVisibility(View.GONE);}};
    private DocumentStore store;
    private byte[] savedTurboFile;
    private AudioTrack audio;
    private boolean resumed,picking,swap,swapXY,trigger,menuOpen,skipLogo=true;
    private int slot=1,keyButtons,hatButtons,speedHalf=2;
    private int experienceMultiplier=1,goldMultiplier=1;
    private boolean maxBonus,noMpCost,mixedParty,mapOpen,mapTrigger,itemsOpen,itemTrigger;
    private int[] spellSlots;
    private String[] spellLabels;
    private int selectedCharacter=-1;
    private long lastAxisNavigation;
    private String romName="",stem="";
    private Uri activeRomUri;
    private boolean sfc;
    private long framePeriod=16742706L;
    private final Properties appSettings=new Properties();
    private List<DocumentStore.Rom> romEntries=new ArrayList<>();
    private InputManager inputManager;
    private SaveQueue saveQueue;
    private final PcmQueue pcmQueue=new PcmQueue();

    @Override public void onCreate(Bundle state) {
        super.onCreate(state); getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        FrameLayout layout=new FrameLayout(this);layout.setBackgroundColor(0xff101722);
        view=new GameView(this,new GameView.Actions(){
            public void pressed(int key){
                view.navigationHint="上下:選択  左右:変更  A:決定  B:戻る";
                if(key<8){if(itemsOpen||mapOpen||menuOpen||view.browserItems!=null)navigate(1<<key);}
                else if(key==8)pickRom();else if(key==9)menu();
                else if((itemsOpen||mapOpen)&&(sfc?(key==15||key==16):(key==12||key==10)))
                    navigate(key==12||key==15?256:512);
                else if(key==10)state(1,false);else if(key==11)state(1,true);else if(key==12)resetGame();else if(key==13)openMap();else if(key==14)openItems();
            }
            public void itemTap(int x,int y){if(itemsOpen)handleItemInput(0,x,y);}
            public void menuItem(int row,int adjust){if(view.browserItems!=null){view.browserRow=row;activateRom();}else if(view.spellCharacters!=null){view.spellRow=row;selectCharacter();}else{view.menuRow=row;activateMenu(adjust);}}
        });
        int noticeHeight=Math.round(44*getResources().getDisplayMetrics().density);
        FrameLayout.LayoutParams gameParams=new FrameLayout.LayoutParams(-1,-1);
        layout.addView(view,gameParams);
        notification=new TextView(this);
        notification.setVisibility(View.GONE);
        notification.setBackgroundColor(0xcc101722);
        notification.setTextColor(0xffffe696);notification.setTextSize(13);
        notification.setGravity(Gravity.CENTER_VERTICAL);
        notification.setPadding(12,0,12,0);notification.setMaxLines(2);
        notification.setEllipsize(android.text.TextUtils.TruncateAt.END);
        notification.setClickable(false);notification.setFocusable(false);
        notification.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        layout.addView(notification,new FrameLayout.LayoutParams(-1,noticeHeight,Gravity.BOTTOM));
        setContentView(layout);
        // Follow Wiiz: reserve bars, cutouts and mandatory gestures in both orientations.
        layout.setOnApplyWindowInsetsListener((v,insets)->{
            int left,top,right,bottom;
            if(Build.VERSION.SDK_INT>=30){
                android.graphics.Insets bars=insets.getInsetsIgnoringVisibility(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());
                android.graphics.Insets gestures=insets.getInsets(WindowInsets.Type.mandatorySystemGestures());
                left=Math.max(bars.left,gestures.left);top=Math.max(bars.top,gestures.top);
                right=Math.max(bars.right,gestures.right);bottom=Math.max(bars.bottom,gestures.bottom);
            }else{left=insets.getStableInsetLeft();top=insets.getStableInsetTop();right=insets.getStableInsetRight();bottom=insets.getStableInsetBottom();}
            v.setPadding(left,top,right,bottom);return insets;
        });layout.requestApplyInsets();
        inputManager=(InputManager)getSystemService(INPUT_SERVICE);
        inputManager.registerInputDeviceListener(this,new Handler(Looper.getMainLooper()));
        saveQueue=new SaveQueue((path,data)->{
            DocumentStore destination=store;
            if(destination==null)throw new IOException("保存フォルダがありません");
            destination.write(path,data);
        },text->runOnUiThread(()->message(text)));
        String saved=getPreferences(0).getString("save_tree",null);
        if(saved!=null) try { store=new DocumentStore(getContentResolver(),Uri.parse(saved)); loadSettings(); }
        catch(Exception e) { store=null; message("保存フォルダを選び直してください"); }
        try {
            int size=AudioTrack.getMinBufferSize(32768,AudioFormat.CHANNEL_OUT_STEREO,AudioFormat.ENCODING_PCM_16BIT);
            audio=new AudioTrack.Builder().setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).build())
                .setAudioFormat(new AudioFormat.Builder().setSampleRate(32768).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                .setBufferSizeInBytes(Math.max(size,8192)).setTransferMode(AudioTrack.MODE_STREAM).build();
        } catch(Exception e) { audio=null; message("音声デバイスが使えません。無音で続行します"); }
        if(!BuildConfig.DEBUG||!getIntent().getBooleanExtra("test_ui",false))view.post(this::pickRom);
    }
    private void message(String text) {
        // A single non-modal line outside the game and controls; never queue notifications.
        notificationHandler.removeCallbacks(clearNotification);
        notification.setText(text);
        notification.setVisibility(View.VISIBLE);
        notificationHandler.postDelayed(clearNotification,5000);
    }
    private boolean paused() { return !resumed||picking||itemsOpen||mapOpen||menuOpen||view.browserItems!=null; }
    private void clearInput() { keyButtons=hatButtons=0; trigger=false; view.clearInput(); }
    private void muteAudio(){
        if(audio!=null){try{audio.release();}catch(RuntimeException ignored){}audio=null;}
        pcmQueue.clear();message("音声デバイスが使えません。無音で続行します");
    }
    private void silence() {
        clearInput();pcmQueue.clear();
        if(audio!=null)try{audio.pause();audio.flush();}catch(RuntimeException e){muteAudio();}
        lastFrame=0;
    }
    private void playAudio(int count){
        if(audio==null)return;
        try{
            if(audio.getPlayState()!=AudioTrack.PLAYSTATE_PLAYING)audio.play();
            pcmQueue.offer(pcm,count,(data,length)->audio.write(data,0,length,AudioTrack.WRITE_NON_BLOCKING));
        }catch(RuntimeException e){muteAudio();}
    }
    private Uri initial(String folder) { return DocumentsContract.buildDocumentUri("com.android.externalstorage.documents","primary:Documents/"+folder); }
    private void pickRom() {
        if(picking) return;
        closeItems();closeMap();
        view.spellCharacters=null;
        view.characterMenu=false;selectedCharacter=-1;
        menuOpen=false;view.menu=false;view.invalidate();
        silence();
        if(store==null) {
            message("Documentsを選ぶとWizPlayer/save・state・settings・romを作成します。既存のWizPlayerも選べます");
            Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,initial(""));
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_WRITE_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            picking=true;
            startActivityForResult(intent,FOLDER);
        } else {
            try {
                String configured=appSettings.getProperty("rom_tree");
                Uri folder=configured==null?store.romFolder:Uri.parse(configured);
                romEntries=DocumentStore.roms(getContentResolver(),folder);
                String[] items=new String[romEntries.size()+1];items[0]="ROMフォルダを変更…";
                int selected=romEntries.isEmpty()?0:1;
                for(int i=0;i<romEntries.size();i++){
                    DocumentStore.Rom entry=romEntries.get(i);
                    String name=entry.name;
                    items[i+1]=isSfcName(name)&&!NativeCore.sfcAvailable()?"[SFC・起動不可] "+name:name;
                    if(entry.uri.equals(activeRomUri))selected=i+1;
                }
                view.browserItems=items;view.browserRow=selected;view.invalidate();
                if(romEntries.isEmpty())message("ROMがありません。「ROMフォルダを変更」で場所を選べます");
            }catch(Exception e){message("ROMフォルダを選び直してください");chooseRomFolder();}
        }
    }
    private void chooseRomFolder(){
        silence();picking=true;
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,store==null?initial("WizPlayer/rom"):store.romFolder);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent,ROM_FOLDER);
    }
    private void activateRom(){
        int selected=view.browserRow;
        if(selected==0){chooseRomFolder();return;}
        if(selected>romEntries.size())return;
        try{openRom(romEntries.get(selected-1).uri);}
        catch(Exception e){message("切り替え失敗（元のゲームを保持）: "+e.getMessage());}
    }
    private void openRomWithPatchChoice(Uri uri,String name,byte[] rom,boolean enabled) {
        picking=false;
        try{openRom(uri,name,enabled?NativeCore.applyBuiltInBugFix(rom):rom);}
        catch(Exception e){message("切り替え失敗（元のゲームを保持）: "+e.getMessage());}
    }
    private void openRom(Uri uri)throws IOException {
        String name=null;
        try(Cursor cursor=getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)) {
            if(cursor!=null&&cursor.moveToFirst())name=cursor.getString(0);
        }
        if(name==null||name.contains("/")||name.contains("\\"))throw new IOException("ROM名を取得できません");
        byte[] rom=DocumentStore.read(getContentResolver(),uri,16*1024*1024);
        if(!NativeCore.hasBuiltInBugFix(rom)){openRom(uri,name,rom);return;}
        final String romName=name;
        picking=true;
        new AlertDialog.Builder(this)
            .setTitle("内蔵バグ修正パッチを適用しますか？")
            .setMessage(name)
            .setPositiveButton("OFF",(dialog,which)->openRomWithPatchChoice(uri,romName,rom,false))
            .setNegativeButton("ON",(dialog,which)->openRomWithPatchChoice(uri,romName,rom,true))
            .setNeutralButton("キャンセル",(dialog,which)->picking=false)
            .setOnCancelListener(dialog->picking=false)
            .show();
    }
    private void openRom(Uri uri,String name,byte[] rom)throws IOException {
        boolean nextNes=rom.length>=4&&rom[0]=='N'&&rom[1]=='E'&&rom[2]=='S'&&rom[3]==0x1a;
        boolean nextSfc=!nextNes&&isSfcName(name);
        String candidate=!nextNes&&!nextSfc?NativeCore.handheldCandidate(rom):null;
        if(candidate==null){
            openRom(uri,name,rom,nextNes,nextSfc,true);
            return;
        }
        final String romName=name;
        picking=true;
        AlertDialog choice=new AlertDialog.Builder(this)
            .setTitle("ROMの作品候補")
            .setItems(new String[]{candidate,"作品別機能を使わずに起動"},(dialog,index)->{
                try{openRom(uri,romName,rom,false,false,index==0);}
                catch(Exception e){message("切り替え失敗（元のゲームを保持）: "+e.getMessage());}
            })
            .setNegativeButton("キャンセル",null).create();
        choice.setOnDismissListener(dialog->{picking=false;clearInput();});
        choice.show();
    }
    private void openRom(Uri uri,String name,byte[] rom,boolean nextNes,boolean nextSfc,boolean useProfile)throws IOException {
        int dot=name.lastIndexOf('.');String nextStem=dot>0?name.substring(0,dot):name;
        if(nextSfc&&!NativeCore.sfcAvailable())throw new IOException("このAPKにはSFC実行部がありません");
        if(nextNes)nextStem+=".fc";
        if(nextSfc)nextStem+=".sfc";
        flush();
        byte[] savedSettings=store.read(romSettingsFile(name));
        Properties options=new Properties(appSettings);
        if(savedSettings!=null)options.load(new ByteArrayInputStream(savedSettings));
        RomSettings chosen=RomSettings.read(options);
        byte[] battery=store.read(nextStem+".sav");
        byte[] exploration=nextNes?store.read(nextStem+".map"):null;
        byte[] turboFile=nextNes?store.read(TURBO_FILE):null;
        long next=nextSfc?NativeCore.openSfc(rom,battery,chosen.skipLogo()):NativeCore.openConfigured(rom,battery,chosen.skipLogo(),turboFile,useProfile);
        if(exploration!=null)NativeCore.loadExploration(next,exploration);
        NativeCore.setRewards(next,NativeCore.rewardsSupported(next)?chosen.experienceMultiplier():1,
            NativeCore.goldRewardsSupported(next)?chosen.goldMultiplier():1);
        view.rewardSupported=NativeCore.rewardsSupported(next);view.goldRewardSupported=NativeCore.goldRewardsSupported(next);
        NativeCore.setMaxBonus(next,chosen.maxBonus());view.bonusSupported=NativeCore.maxBonusSupported(next);
        NativeCore.setNoMpCost(next,chosen.noMpCost());view.mpSupported=NativeCore.mpSupported(next);
        NativeCore.setMixedParty(next,chosen.mixedParty());view.partySupported=NativeCore.partySupported(next);
        view.effectsSupported=NativeCore.effectsSupported(next);
        NativeCore.setEffects(next,chosen.effects()&view.effectsSupported);
        view.gameOptionsSupported=NativeCore.gameOptionsSupported(next);
        NativeCore.setGameOptions(next,chosen.gameOptions()&view.gameOptionsSupported);
        boolean speedAccepted=NativeCore.setSpeed(next,chosen.speedHalf());
        if(core!=0)NativeCore.close(core);
        core=next;activeRomUri=uri;romName=name;stem=nextStem;sfc=nextSfc;
        savedTurboFile=turboFile;
        applySettings(chosen);
        if(!speedAccepted){
            speedHalf=2;view.speedHalf=2;
            message("このROMの倍速・等速音声は未対応です。1倍速で再生します");
        }
        if(savedSettings==null)saveSettings();
        framePeriod=NativeCore.framePeriod(next);
        view.frameSize(nextNes||sfc?256:160,sfc?224:nextNes?240:144);
        view.sfcControls(sfc);
        lastFrame=0;lastSave=SystemClock.elapsedRealtime();
        view.browserItems=null;view.invalidate();silence();
    }
    private static boolean isSfcName(String name){
        String lower=name.toLowerCase(Locale.ROOT);
        return lower.endsWith(".sfc")||lower.endsWith(".smc");
    }
    @Override protected void onActivityResult(int request,int result,Intent intent) {
        super.onActivityResult(request,result,intent); picking=false; clearInput();
        if(result!=RESULT_OK||intent==null||intent.getData()==null) return;
        try {
            Uri uri=intent.getData();
            if(request==FOLDER) {
                boolean readGranted=(intent.getFlags()&Intent.FLAG_GRANT_READ_URI_PERMISSION)!=0;
                boolean writeGranted=(intent.getFlags()&Intent.FLAG_GRANT_WRITE_URI_PERMISSION)!=0;
                if(readGranted&&writeGranted)getContentResolver().takePersistableUriPermission(uri,Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                else if(readGranted)getContentResolver().takePersistableUriPermission(uri,Intent.FLAG_GRANT_READ_URI_PERMISSION);
                else if(writeGranted)getContentResolver().takePersistableUriPermission(uri,Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                DocumentStore candidate=new DocumentStore(getContentResolver(),uri);
                candidate.read("settings.ini"); store=candidate;
                getPreferences(0).edit().putString("save_tree",uri.toString()).apply(); loadSettings(); pickRom();
            } else if(request==ROM_FOLDER){
                if((intent.getFlags()&Intent.FLAG_GRANT_READ_URI_PERMISSION)!=0)getContentResolver().takePersistableUriPermission(uri,Intent.FLAG_GRANT_READ_URI_PERMISSION);
                DocumentStore.roms(getContentResolver(),uri);
                appSettings.setProperty("rom_tree",uri.toString());
                enqueueSettings("settings.ini",appSettings);
                pickRom();
            } else if(request==ROM) {
                openRom(uri);
            }
        } catch(Exception e) { message("切り替え失敗（元のゲームを保持）: "+e.getMessage()); }
    }
    private void loadSettings() throws IOException {
        byte[] data=store.read("settings.ini");appSettings.clear();
        if(data!=null)appSettings.load(new ByteArrayInputStream(data));
        if(core==0)applySettings(RomSettings.read(appSettings));
    }
    private static String romSettingsFile(String name){return name+".ini";}
    private record RomSettings(boolean swap,boolean swapXY,boolean skipLogo,int slot,int speedHalf,
            int experienceMultiplier,int goldMultiplier,boolean maxBonus,boolean noMpCost,boolean mixedParty,int effects,int gameOptions,int crtMode) {
        static RomSettings read(Properties p) {
            int speed=intSetting(p,"speed_half",2);
            boolean validSpeed=false;
            for(int value:SPEEDS)if(value==speed){validSpeed=true;break;}
            return new RomSettings(Boolean.parseBoolean(p.getProperty("swap_ab","false")),
                Boolean.parseBoolean(p.getProperty("swap_xy","false")),
                Boolean.parseBoolean(p.getProperty("skip_logo","true")),
                Math.max(1,Math.min(5,intSetting(p,"slot",1))),validSpeed?speed:2,
                rewardSetting(p,"experience_multiplier"),rewardSetting(p,"gold_multiplier"),
                Boolean.parseBoolean(p.getProperty("max_bonus","false")),
                Boolean.parseBoolean(p.getProperty("no_mp_cost","false")),
                Boolean.parseBoolean(p.getProperty("mixed_party","false")),intSetting(p,"effects",0)&15,
                intSetting(p,"game_options",0)&31,Math.max(0,Math.min(2,intSetting(p,"crt_mode",0))));
        }
    }
    private void applySettings(RomSettings options) {
        view.swap=swap=options.swap();view.swapXY=swapXY=options.swapXY();
        view.skipLogo=skipLogo=options.skipLogo();view.slot=slot=options.slot();
        view.speedHalf=speedHalf=options.speedHalf();
        view.experienceMultiplier=experienceMultiplier=options.experienceMultiplier();
        view.goldMultiplier=goldMultiplier=options.goldMultiplier();
        view.maxBonus=maxBonus=options.maxBonus();view.noMpCost=noMpCost=options.noMpCost();
        view.mixedParty=mixedParty=options.mixedParty();
        view.effects=options.effects();
        view.gameOptions=options.gameOptions();
        view.crtMode=options.crtMode();
    }
    private void enqueueSettings(String file,Properties options)throws IOException {
        ByteArrayOutputStream output=new ByteArrayOutputStream();
        options.store(output,"WizPlayer");
        saveQueue.write(file,output.toByteArray());
    }
    private void saveSettings() {
        try {
            if(store==null||core==0)return;
            Properties options=new Properties();
            options.setProperty("swap_xy",Boolean.toString(swapXY));
            options.setProperty("swap_ab",Boolean.toString(swap));options.setProperty("slot",Integer.toString(slot));
            options.setProperty("skip_logo",Boolean.toString(skipLogo));
            options.setProperty("speed_half",Integer.toString(speedHalf));
            options.setProperty("crt_mode",Integer.toString(view.crtMode));
            options.setProperty("experience_multiplier",Integer.toString(experienceMultiplier));
            options.setProperty("gold_multiplier",Integer.toString(goldMultiplier));
            options.setProperty("effects",Integer.toString(view.effects));
            options.setProperty("game_options",Integer.toString(view.gameOptions));
            options.setProperty("max_bonus",Boolean.toString(maxBonus));
            options.setProperty("no_mp_cost",Boolean.toString(noMpCost));
            options.setProperty("mixed_party",Boolean.toString(mixedParty));
            enqueueSettings(romSettingsFile(romName),options);
        } catch(Exception e) { message("設定を保存できません: "+e.getMessage()); }
    }
    private void flush() throws IOException {
        saveBattery();saveQueue.flush();
    }
    private void saveBattery() throws IOException {
        if(core!=0&&store!=null){
            byte[] data=NativeCore.battery(core);
            if(data.length>0)saveQueue.write(stem+".sav",data);
            byte[] exploration=NativeCore.exploration(core);
            if(exploration!=null)saveQueue.write(stem+".map",exploration);
            byte[] turboFile=NativeCore.turboFile(core);
            if(turboFile!=null&&!Arrays.equals(turboFile,savedTurboFile==null?EMPTY_TURBO_FILE:savedTurboFile)){
                if(!Arrays.equals(savedTurboFile,store.read(TURBO_FILE)))
                    throw new IOException("別の操作でターボファイルが変更されました");
                store.write(TURBO_FILE,turboFile);
                savedTurboFile=turboFile;
            }
        }
    }
    private void menu() {
        if(picking)return;
        closeItems();closeMap();
        view.browserItems=null;
        view.spellCharacters=null;
        view.characterMenu=false;selectedCharacter=-1;
        menuOpen=!menuOpen;view.menu=menuOpen;view.slot=slot;view.swap=swap;view.swapXY=swapXY;view.skipLogo=skipLogo;view.speedHalf=speedHalf;
        view.supportMenu=false;
        if(menuOpen)view.menuRow=0;silence();view.invalidate();
    }
    private void resetGame(){
        if(core==0)return;
        closeItems();closeMap();
        try{flush();NativeCore.reset(core);silence();menuOpen=false;view.menu=false;view.browserItems=null;view.spellCharacters=null;view.characterMenu=false;selectedCharacter=-1;view.invalidate();}
        catch(Exception e){message("保存失敗のためリセットを中止しました");}
    }
    private void activateMenu(int adjust){
        int row=view.menuRow;
        if(view.characterMenu){
            if(row==0&&adjust!=0&&spellSlots.length>1){
                view.spellRow=1+Math.floorMod(view.spellRow-1+adjust,spellSlots.length-1);
                showSpellCharacter(view.spellRow);
            }
            if(row==2&&adjust!=0&&view.characterAlignment>=0)applyAlignment((view.characterAlignment+(adjust>0?1:2))%3);
            if(row>=3&&adjust!=0)adjustCharacterTrait(row==3,adjust);
            if(adjust==0){if(row==0)pickSpellCharacter();else if(row==1&&view.characterSpellsSupported)applySpells();}
            view.invalidate();return;
        }
        if(view.supportMenu){
            if(row==0&&adjust==0){view.supportMenu=false;view.menuRow=9;}
            else if(view.gameOptionsSupported!=0&&row>=3){
                int bit=view.gameOptionBit(row);
                int options=view.gameOptions^bit;
                if(bit!=0&&core!=0&&NativeCore.setGameOptions(core,options&view.gameOptionsSupported)){
                    view.gameOptions=options;
                    saveSettings();
                }
            }
            else if((row==1||row==2)&&adjust!=0){
                if(core==0||!(row==1?view.rewardSupported:view.goldRewardSupported)){
                    message(row==1?"このROMは経験値倍率に対応していません":"このROMはお金の倍率に対応していません");
                    return;
                }
                int exp=row==1?nextReward(experienceMultiplier,adjust):experienceMultiplier;
                int gold=row==2?nextReward(goldMultiplier,adjust):goldMultiplier;
                if(NativeCore.setRewards(core,view.rewardSupported?exp:1,view.goldRewardSupported?gold:1)){experienceMultiplier=exp;goldMultiplier=gold;view.experienceMultiplier=exp;view.goldMultiplier=gold;saveSettings();}
            }
            else if(row==3&&adjust!=0){
                if(core!=0&&NativeCore.setMaxBonus(core,!maxBonus)){maxBonus=!maxBonus;view.maxBonus=maxBonus;saveSettings();}
                else message("このROMは作成ボーナス最大に対応していません");
            }
            else if(row==4&&adjust==0)pickSpellCharacter();
            else if(row==7&&adjust==0)openItems();
            else if(row>=8&&row<12&&adjust!=0){
                int bit=1<<(row-8);
                int effects=view.effects^bit;
                if(core!=0&&(view.effectsSupported&bit)!=0&&NativeCore.setEffects(core,effects&view.effectsSupported)){
                    view.effects=effects;
                    saveSettings();
                }
            }
            else if(row==6&&adjust!=0){
                if(core!=0&&NativeCore.setMixedParty(core,!mixedParty)){mixedParty=!mixedParty;view.mixedParty=mixedParty;saveSettings();}
                else message("このROMは善悪混合に対応していません");
            }
            else if(row==5&&adjust!=0){
                if(core!=0&&NativeCore.setNoMpCost(core,!noMpCost)){noMpCost=!noMpCost;view.noMpCost=noMpCost;saveSettings();}
                else message("MP消費なしは対応版のGB外伝1〜3で使用できます");
            }
            view.invalidate();return;
        }
        if(row==0){if(adjust!=0){slot=(slot-1+(adjust>0?1:4))%5+1;saveSettings();}}
        else if(row==4){swap=!swap;saveSettings();}
        else if(row==10){swapXY=!swapXY;saveSettings();}
        else if(row==11){view.crtMode=(view.crtMode+(adjust<0?2:1))%3;saveSettings();}
        else if(row==7){skipLogo=!skipLogo;if(core!=0)NativeCore.setSkipLogo(core,skipLogo);saveSettings();}
        else if(row==8&&adjust!=0){
            int index=0;
            while(index+1<SPEEDS.length&&SPEEDS[index]!=speedHalf)index++;
            int next=SPEEDS[(index+(adjust>0?1:SPEEDS.length-1))%SPEEDS.length];
            try {
                if(core!=0&&NativeCore.setSpeed(core,next)){speedHalf=next;silence();saveSettings();}
                else message("このROMは等速音声付きの倍速に未対応です");
            }catch(IllegalStateException e){message("速度を変更できません: "+e.getMessage());}
        }
        else if(adjust==0){
            if(row==3)menu();
            else if(row==1||row==2){if(state(slot,row==2))menu();}
            else if(row==5)pickRom();else if(row==6)resetGame();
            else if(row==9){view.supportMenu=true;view.menuRow=0;}
        }
        view.slot=slot;view.swap=swap;view.swapXY=swapXY;view.skipLogo=skipLogo;view.speedHalf=speedHalf;view.invalidate();
    }
    private void refreshItems(){view.itemLines=itemsOpen?NativeCore.itemLines(core):null;silence();view.invalidate();}
    private void handleItemInput(int bit,int x,int y) {
        try {
            itemsOpen=NativeCore.itemKey(core,bit,x,y);
            refreshItems();
            if(!itemsOpen)saveBattery();
        } catch(IllegalStateException e) {
            message("アイテム操作に失敗しました: "+e.getMessage());
        } catch(IOException e) {
            message("通常セーブの保存に失敗しました");
        }
    }
    private void closeItems(){if(core!=0&&itemsOpen){NativeCore.closeItems(core);itemsOpen=false;refreshItems();try{saveBattery();}catch(IOException e){message("通常セーブの保存に失敗しました");}}}
    private void openItems(){
        if(core==0||picking)return;
        if(itemsOpen){closeItems();return;}
        if(!NativeCore.openItems(core)){message("アイテム編集には１人以上のパーティーが必要です（戦闘・データ更新中は不可）");return;}
        closeMap();view.browserItems=null;view.spellCharacters=null;view.characterMenu=false;view.supportMenu=false;
        menuOpen=false;view.menu=false;itemsOpen=true;refreshItems();
    }
    private void closeMap(){
        if(core!=0&&mapOpen){NativeCore.closeMap(core);mapOpen=false;refreshMap();silence();}
    }
    private void refreshMap(){
        view.mapMode=mapOpen;
        view.mapFloor=NativeCore.mapFloor(core);
        view.mapRegion=NativeCore.mapRegion(core);
        NativeCore.mapFrame(core,pixels);
        view.frame(pixels);
    }
    private void openMap(){
        if(itemsOpen)return;
        if(core==0||picking)return;
        if(mapOpen){closeMap();return;}
        if(!NativeCore.openMap(core)){message("現在の状態ではマップを表示できません");return;}
        menuOpen=false;view.menu=false;view.spellCharacters=null;view.characterMenu=false;view.browserItems=null;
        mapOpen=true;silence();refreshMap();

    }
    private void navigate(int bit){
        if(itemsOpen){handleItemInput(bit,-1,-1);return;}
        if(mapOpen){
            try{
                if(NativeCore.mapKey(core,bit)){
                    if(!NativeCore.mapMoveCanStart(core)){
                        message("現在の状態では移動できません");
                        refreshMap();
                        return;
                    }
                    if(store==null)throw new IOException("保存先がありません");
                    byte[] backup=NativeCore.state(core);String path=stem+".slot1.state";
                    saveQueue.write(path,backup);saveQueue.flush();
                    if(!java.util.Arrays.equals(backup,store.read(path)))throw new IOException("スロット1保存の確認に失敗しました");
                    if(!NativeCore.moveMapAfterBackup(core))message("現在の状態では移動できません");
                }
            }catch(Exception e){message("移動を中止しました: "+e.getMessage());}
            mapOpen=NativeCore.mapIsOpen(core);refreshMap();silence();return;
        }
        if(view.spellCharacters!=null){
            int count=view.spellCharacters.length;
            if((bit&64)!=0)view.spellRow=(view.spellRow+count-1)%count;
            else if((bit&128)!=0)view.spellRow=(view.spellRow+1)%count;
            else if((bit&1)!=0)selectCharacter();
            else if((bit&2)!=0){view.spellCharacters=null;view.menuRow=4;}
            view.invalidate();return;
        }
        if(view.browserItems!=null){
            int count=view.browserItems.length;
            if((bit&64)!=0)view.browserRow=(view.browserRow+count-1)%count;
            else if((bit&128)!=0)view.browserRow=(view.browserRow+1)%count;
            else if((bit&1)!=0)activateRom();
            else if((bit&2)!=0){view.browserItems=null;silence();}
            view.invalidate();return;
        }
        if(!menuOpen)return;
        if(view.characterMenu&&(bit&2)!=0){pickSpellCharacter();view.invalidate();return;}
        int rows=view.characterMenu?view.characterRowCount():view.supportMenu?view.supportRowCount():GameView.MENU_ROWS;
        if((bit&64)!=0)view.menuRow=(view.menuRow+rows-1)%rows;
        else if((bit&128)!=0)view.menuRow=(view.menuRow+1)%rows;
        else if((bit&16)!=0)activateMenu(1);else if((bit&32)!=0)activateMenu(-1);
        else if((bit&1)!=0)activateMenu(0);else if((bit&2)!=0){if(view.supportMenu){view.supportMenu=false;view.menuRow=9;}else menu();}
        view.invalidate();
    }
    private void pickSpellCharacter(){
        if(core==0||!NativeCore.characterEditorSupported(core)){message("このROMのキャラクター編集は未対応です");return;}
        if(!NativeCore.canEditCharacters(core)){message("戦闘中やデータ更新中は使用できません。少し再開してから選び直してください");return;}
        String[] names=NativeCore.spellNames(core);List<String> labels=new ArrayList<>();List<Integer> slots=new ArrayList<>();
        labels.add("戻る");slots.add(-1);
        for(int i=0;i<names.length;i++)if(names[i]!=null){labels.add((i+1)+": "+java.text.Normalizer.normalize(names[i],java.text.Normalizer.Form.NFC));slots.add(i);}
        if(labels.size()==1){message("登録キャラクターがいません");return;}
        spellSlots=new int[slots.size()];for(int i=0;i<slots.size();i++)spellSlots[i]=slots.get(i);
        spellLabels=labels.toArray(new String[0]);
        view.characterMenu=false;selectedCharacter=-1;view.spellCharacters=spellLabels;view.spellRow=0;
    }
    private void selectCharacter(){
        if(view.spellRow==0){view.spellCharacters=null;view.menuRow=4;return;}
        int selected=view.spellRow;if(spellSlots==null||selected<0||selected>=spellSlots.length)return;
        showSpellCharacter(selected);
        view.spellCharacters=null;view.characterMenu=true;view.menuRow=0;
    }
    private void showSpellCharacter(int selected){
        selectedCharacter=spellSlots[selected];
        view.characterAlignment=NativeCore.alignment(core,selectedCharacter);
        view.characterName=spellLabels[selected];
        view.characterSpellsSupported=NativeCore.spellsSupported(core);
        view.characterRaceCount=NativeCore.characterTraitCount(core,true);
        view.characterJobCount=NativeCore.characterTraitCount(core,false);
        if(view.characterRaceCount>0){
            view.characterRace=NativeCore.characterTrait(core,selectedCharacter,true);
            view.characterJob=NativeCore.characterTrait(core,selectedCharacter,false);
        }
    }
    private void applySpells(){
        if(core==0||selectedCharacter<0)return;
        try{flush();}catch(Exception e){message("通常セーブの保存に失敗したため適用を中止しました");return;}
        if(!NativeCore.learnSpells(core,selectedCharacter)){message("戦闘中やデータ更新中は適用できません。キャラクターの操作を終えてから選び直してください");return;}
        try{flush();}
        catch(Exception e){message("適用済みですが通常セーブを保存できません。再度保存してください");}
    }
    private void applyAlignment(int alignment){
        if(core==0||selectedCharacter<0)return;
        try{flush();}catch(Exception e){message("通常セーブの保存に失敗したため適用を中止しました");return;}
        if(!NativeCore.setAlignment(core,selectedCharacter,alignment)){message("未対応版・戦闘中・データ更新中は属性を変更できません。操作を終えてから選び直してください");return;}
        view.characterAlignment=NativeCore.alignment(core,selectedCharacter);
        try{flush();}
        catch(Exception e){message("変更済みですが通常セーブを保存できません。再度保存してください");}
    }
    private void adjustCharacterTrait(boolean race,int direction){
        if(core==0||selectedCharacter<0)return;
        int current=race?view.characterRace:view.characterJob;
        int count=race?view.characterRaceCount:view.characterJobCount;
        if(current<0||count==0)return;
        int next=(current+(direction>0?1:count-1))%count;
        try{flush();}catch(Exception e){message("通常セーブの保存に失敗したため適用を中止しました");return;}
        if(!NativeCore.setCharacterTrait(core,selectedCharacter,race,next)){
            message("戦闘中やデータ更新中は変更できません。操作を終えてから選び直してください");
            return;
        }
        if(race)view.characterRace=next;else view.characterJob=next;
        try{flush();}
        catch(Exception e){message("変更済みですが通常セーブを保存できません。再度保存してください");}
    }
    private static int intSetting(Properties options,String key,int fallback){
        try{return Integer.parseInt(options.getProperty(key,Integer.toString(fallback)));}
        catch(NumberFormatException ignored){return fallback;}
    }
    private static int rewardSetting(Properties options,String key){
        int value=intSetting(options,key,1);
        for(int allowed:new int[]{1,8,16,32,64,128,256})if(value==allowed)return value;
        return 1;
    }
    private static int nextReward(int current,int direction){
        int[] values={1,8,16,32,64,128,256};int index=0;
        while(index<values.length-1&&values[index]!=current)index++;
        return values[(index+(direction>0?1:values.length-1))%values.length];
    }
    private boolean state(int number,boolean load) {
        if(core==0||store==null||itemsOpen||mapOpen)return false;
        if(load&&(view.spellCharacters!=null||view.characterMenu)){view.spellCharacters=null;view.characterMenu=false;selectedCharacter=-1;view.menuRow=4;}
        try {
            String path=stem+".slot"+number+".state";
            if(load) { saveQueue.flush();byte[] data=store.read(path); if(data==null) {message("保存がありません");return false;} if(!NativeCore.loadState(core,data)) throw new IOException("不正なステートです");
                if(NativeCore.speed(core)!=speedHalf&&!NativeCore.setSpeed(core,speedHalf))
                    throw new IOException("ROMごとの速度設定を復元できません");
                silence(); }
            else { saveQueue.write(path,NativeCore.state(core)); flush(); }
            return true;
        } catch(Exception e) { message("保存・読込失敗: "+e.getMessage());return false; }
    }
    @Override protected void onResume() { super.onResume(); resumed=true; lastFrame=0; Choreographer.getInstance().postFrameCallback(this); }
    @Override protected void onPause() {
        resumed=false; Choreographer.getInstance().removeFrameCallback(this); silence();
        try { flush(); } catch(Exception e) {message("通常セーブの保存に失敗しました");} super.onPause();
    }
    @Override protected void onDestroy() {notificationHandler.removeCallbacks(clearNotification);if(saveQueue!=null)saveQueue.close();if(inputManager!=null)inputManager.unregisterInputDeviceListener(this);if(core!=0) {NativeCore.close(core);core=0;} if(audio!=null) audio.release(); super.onDestroy(); }
    @Override public void onInputDeviceAdded(int id){}
    @Override public void onInputDeviceChanged(int id){clearInput();}
    @Override public void onInputDeviceRemoved(int id){clearInput();}
    @Override public void doFrame(long now) {
        if(resumed) Choreographer.getInstance().postFrameCallback(this);
        if(paused()||core==0) {lastFrame=0;return;}
        if(lastFrame==0) lastFrame=now-framePeriod;
        if(now-lastFrame>4*framePeriod) lastFrame=now-framePeriod;
        try {
            while(now-lastFrame>=framePeriod) {
                int buttons=view.buttons|keyButtons|hatButtons;
                if(swap) buttons=(buttons&~3)|((buttons&1)<<1)|((buttons&2)>>1);
                int count=NativeCore.step(core,buttons,pixels,pcm);
                playAudio(count);
                lastFrame+=framePeriod;
            }
            view.frame(pixels);
            long time=SystemClock.elapsedRealtime(); if(time-lastSave>10000) {lastSave=time;saveBattery();}
        } catch(Exception e) {message("動作を停止しました: "+e.getMessage()); menu();}
    }
    @Override public void onBackPressed() {if(itemsOpen||mapOpen||view.browserItems!=null||view.supportMenu)navigate(2);else menu();}
    @Override public void onWindowFocusChanged(boolean focused){super.onWindowFocusChanged(focused);if(!focused&&view!=null){itemTrigger=false;clearInput();}}
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if(picking)return super.dispatchKeyEvent(event);
        int key=event.getKeyCode(); boolean down=event.getAction()==KeyEvent.ACTION_DOWN;
        if(down&&event.getRepeatCount()==0) {
            if((mapOpen||itemsOpen)&&(key==KeyEvent.KEYCODE_PAGE_UP||key==KeyEvent.KEYCODE_BUTTON_L1)){navigate(256);return true;}
            if((mapOpen||itemsOpen)&&(key==KeyEvent.KEYCODE_PAGE_DOWN||key==KeyEvent.KEYCODE_BUTTON_R1)){navigate(512);return true;}
            if(itemsOpen&&(key==KeyEvent.KEYCODE_B||key==KeyEvent.KEYCODE_N)){navigate(key==KeyEvent.KEYCODE_B?256:512);return true;}
            if(key==KeyEvent.KEYCODE_BUTTON_R2||key==KeyEvent.KEYCODE_M||key==KeyEvent.KEYCODE_F3){openMap();return true;}
            if(key==KeyEvent.KEYCODE_F4){openItems();return true;}
            if(key==KeyEvent.KEYCODE_BUTTON_THUMBL||key==KeyEvent.KEYCODE_F1) {menu();return true;}
            if(key==KeyEvent.KEYCODE_BUTTON_THUMBR||key==KeyEvent.KEYCODE_F8) {if(!itemsOpen&&!mapOpen&&!menuOpen)state(1,true);return true;}
            if(key==KeyEvent.KEYCODE_BUTTON_L2||key==KeyEvent.KEYCODE_F5) {if(!itemsOpen&&!mapOpen&&!menuOpen)state(1,false);return true;}
        }
        InputDevice device=event.getDevice();int vendor=device==null?0:device.getVendorId(),product=device==null?0:device.getProductId();
        int value=ControllerMapping.bit(key,vendor,product,swapXY);
        if(sfc){
            if(key==KeyEvent.KEYCODE_BUTTON_L1||key==KeyEvent.KEYCODE_Q)value=256;
            else if(key==KeyEvent.KEYCODE_BUTTON_R1||key==KeyEvent.KEYCODE_W)value=512;
            else if(key==KeyEvent.KEYCODE_BUTTON_X||key==KeyEvent.KEYCODE_S)value=swapXY?2048:1024;
            else if(key==KeyEvent.KEYCODE_BUTTON_Y||key==KeyEvent.KEYCODE_A)value=swapXY?1024:2048;
        }
        if(value!=0){
            if(device!=null&&(event.getSource()&InputDevice.SOURCE_GAMEPAD)!=0)
                view.navigationHint=vendor==0x057e&&product==0x2009?"上下:選択  左右:変更  A:決定  B:戻る":"上下:選択  左右:変更  B:決定  A:戻る";
            if(itemsOpen||mapOpen||menuOpen||view.browserItems!=null){if(down&&(event.getRepeatCount()==0||(value&240)!=0))navigate(value);keyButtons=0;}
            else if(down)keyButtons|=value;else keyButtons&=~value;
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        if(!picking&&resumed&&(event.getSource()&InputDevice.SOURCE_JOYSTICK)!=0) {
            float x=event.getAxisValue(MotionEvent.AXIS_HAT_X),y=event.getAxisValue(MotionEvent.AXIS_HAT_Y);
            // Wiiz threshold hysteresis: activate at 50%, release at 35%.
            if(Math.abs(x)<.1f)x=event.getAxisValue(MotionEvent.AXIS_X);
            if(Math.abs(y)<.1f)y=event.getAxisValue(MotionEvent.AXIS_Y);
            int previous=hatButtons;
            hatButtons=(x>((previous&16)!=0?.35f:.5f)?16:x< -((previous&32)!=0?.35f:.5f)?32:0)|
                (y< -((previous&64)!=0?.35f:.5f)?64:y>((previous&128)!=0?.35f:.5f)?128:0);
            if((itemsOpen||mapOpen||menuOpen||view.browserItems!=null)&&hatButtons!=0&&(hatButtons!=previous||SystemClock.uptimeMillis()-lastAxisNavigation>180)){
                navigate(hatButtons);lastAxisNavigation=SystemClock.uptimeMillis();
            }
            boolean pressed=event.getAxisValue(MotionEvent.AXIS_LTRIGGER)>0.5||event.getAxisValue(MotionEvent.AXIS_BRAKE)>0.5;
            boolean mapPressed=event.getAxisValue(MotionEvent.AXIS_RTRIGGER)>.5||event.getAxisValue(MotionEvent.AXIS_GAS)>.5;
            if(mapPressed&&!mapTrigger)openMap();mapTrigger=mapPressed;
            InputDevice device=event.getDevice();
            int rightVertical=device!=null&&device.getMotionRange(MotionEvent.AXIS_RZ,event.getSource())!=null?
                MotionEvent.AXIS_RZ:MotionEvent.AXIS_RY;
            boolean itemsPressed=event.getAxisValue(rightVertical)>(itemTrigger?.35f:.6f);
            if(itemsPressed&&!itemTrigger)openItems();
            itemTrigger=itemsPressed;
            if(pressed&&!trigger&&!itemsOpen&&!mapOpen&&!menuOpen)state(1,false);trigger=pressed;return true;
        }
        return super.onGenericMotionEvent(event);
    }
}
