package dev.personal.subtitle;
import android.app.*;
import android.content.*;
import android.graphics.Color;
import android.net.Uri;
import android.os.*;
import android.text.InputType;
import android.view.*;
import android.widget.*;
public final class MainActivity extends Activity {
    private Pipeline model;
    private CropView cropView;
    private TextView status,original,translated;
    private Button run,pick,settings,retry,cancel;
    private static final int PICK=42;
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        Object retained=getLastNonConfigurationInstance();
        model=retained instanceof Pipeline?(Pipeline)retained:new Pipeline(this);
        ScrollView scroll=new ScrollView(this);
        LinearLayout root=new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL);
        int pad=dp(16); root.setPadding(pad,pad,pad,pad);
        scroll.addView(root); setContentView(scroll);
        scroll.setOnApplyWindowInsetsListener((view,insets)->{
            if(Build.VERSION.SDK_INT>=30) {
                var bars=insets.getInsets(WindowInsets.Type.systemBars());
                scroll.setPadding(bars.left,bars.top,bars.right,bars.bottom);
            } else scroll.setPadding(0,insets.getSystemWindowInsetTop(),0,insets.getSystemWindowInsetBottom());
            return insets;
        });
        TextView title=new TextView(this); title.setText("스크린샷 자막 번역"); title.setTextSize(24); root.addView(title);
        pick=button(root,"이미지 선택",v->choose());
        settings=button(root,"설정 · 개인 API 키",v->settings());
        TextView hint=new TextView(this); hint.setText("핀치로 확대 · 영역 모드에서 드래그로 자막 선택"); root.addView(hint);
        cropView=new CropView(this,model); root.addView(cropView,new LinearLayout.LayoutParams(-1,dp(300)));
        LinearLayout tools=new LinearLayout(this); root.addView(tools);
        Button mode=button(tools,"영역 그리기",v->{
            cropView.selectMode=!cropView.selectMode;
            ((Button)v).setText(cropView.selectMode?"영역 그리기":"이미지 이동");
        });
        button(tools,"영역 초기화",v->{ if(!model.busy) { model.crop.set(0,0,1,1); model.zoom=1; model.panX=model.panY=0; cropView.invalidate(); } });
        run=button(root,"선택 영역 번역",v->model.run());
        LinearLayout actions=new LinearLayout(this); root.addView(actions);
        cancel=button(actions,"취소",v->model.cancel(true));
        retry=button(actions,"재시도",v->model.run());
        status=text(root,15); status.setPadding(0,dp(8),0,dp(8));
        text(root,14).setText("한국어 결과"); translated=text(root,21); translated.setTextIsSelectable(true);
        text(root,14).setText("일본어 원문"); original=text(root,17); original.setTextIsSelectable(true);
        model.listener=this::render; render();
        if(retained==null && saved==null) receive(getIntent());
        if(saved!=null && retained==null) { model.status="앱이 다시 시작되었습니다. 이미지를 다시 선택하세요."; render(); }
        if(Build.VERSION.SDK_INT>=33)
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,()->{
                if(model.busy) model.cancel(true); else finish();
            });
    }
    private int dp(int n) { return (int)(n*getResources().getDisplayMetrics().density); }
    private TextView text(LinearLayout parent,int size) {
        TextView view=new TextView(this); view.setTextSize(size); parent.addView(view); return view;
    }
    private Button button(LinearLayout parent,String label,View.OnClickListener listener) {
        Button b=new Button(this); b.setText(label); b.setMinHeight(dp(48)); b.setOnClickListener(listener);
        parent.addView(b); return b;
    }
    private void render() {
        status.setText(model.status); original.setText(model.japanese); translated.setText(model.korean);
        run.setEnabled(!model.busy && model.image!=null); retry.setEnabled(!model.busy && model.image!=null);
        pick.setEnabled(!model.busy); settings.setEnabled(!model.busy); cancel.setEnabled(model.busy);
        cropView.invalidate();
    }
    private void choose() {
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("image/*").addCategory(Intent.CATEGORY_OPENABLE);
        try { startActivityForResult(intent,PICK); }
        catch(ActivityNotFoundException e) { model.status="이미지 선택기를 열 수 없습니다. 갤러리에서 이미지를 공유해 주세요."; render(); }
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(request==PICK) {
            if(result==RESULT_OK && data!=null && data.getData()!=null) model.load(data.getData(),0);
            else { model.status="이미지 선택을 취소했습니다. 다시 선택할 수 있습니다."; render(); }
        }
    }
    @Override protected void onNewIntent(Intent intent) { super.onNewIntent(intent); setIntent(intent); receive(intent); }
    private void receive(Intent intent) {
        if(Intent.ACTION_SEND.equals(intent.getAction()) && intent.getType()!=null && intent.getType().startsWith("image/")) {
            Uri uri=Build.VERSION.SDK_INT>=33?intent.getParcelableExtra(Intent.EXTRA_STREAM,Uri.class):intent.getParcelableExtra(Intent.EXTRA_STREAM);
            model.load(uri,1);
        }
    }
    private void settings() {
        LinearLayout box=new LinearLayout(this); box.setOrientation(LinearLayout.VERTICAL); box.setPadding(dp(20),dp(8),dp(20),dp(8));
        EditText key=new EditText(this); key.setSingleLine(); key.setHint("본인의 DeepL API 키");
        key.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_PASSWORD); key.setText(model.key); box.addView(key);
        CheckBox free=new CheckBox(this); free.setText("DeepL API Free (해제: API Pro)"); free.setChecked(model.freeApi); box.addView(free);
        CheckBox remember=new CheckBox(this); remember.setText("이 기기에 키 저장 · Android Keystore로 암호화"); remember.setChecked(model.remember); box.addView(remember);
        TextView notice=text(box,14); notice.setText("이미지 OCR은 기기에서 처리합니다. 번역 시 인식한 일본어 텍스트와 인증 키가 HTTPS로 DeepL에 전송됩니다. 요금제에 따라 비용 또는 무료 한도 소모가 발생합니다. 취소해도 전송된 요청은 과금될 수 있습니다. ML Kit SDK의 진단 데이터 처리는 배포 전 개인정보 안내를 확인하세요.\n키가 없거나 동의하지 않으면 OCR만 실행합니다.");
        CheckBox consent=new CheckBox(this); consent.setText("텍스트 전송과 비용 가능성 확인"); consent.setChecked(model.consent); box.addView(consent);
        ScrollView scroll=new ScrollView(this); scroll.addView(box);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("개인용 번역 설정").setView(scroll).setNegativeButton("취소",null)
            .setPositiveButton("저장",null).create();
        dialog.setOnShowListener(d->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{
            try {
                model.saveSettings(key.getText().toString().trim(),free.isChecked(),consent.isChecked(),remember.isChecked());
                dialog.dismiss(); model.status="설정 저장 완료. 이미지를 선택하세요."; render();
            } catch(Exception e) { notice.setText("키를 안전하게 저장하지 못했습니다. 저장 선택을 해제하거나 키를 다시 입력하세요."); }
        }));
        dialog.show();
    }
    @Override public Object onRetainNonConfigurationInstance() { model.listener=null; return model; }
    @Override protected void onStop() {
        if(!isChangingConfigurations() && model.busy) model.cancel(true);
        super.onStop();
    }
    @Override protected void onDestroy() {
        model.listener=null;
        if(!isChangingConfigurations()) model.close();
        super.onDestroy();
    }
    @Override public void onBackPressed() { if(model.busy) model.cancel(true); else super.onBackPressed(); }
}
