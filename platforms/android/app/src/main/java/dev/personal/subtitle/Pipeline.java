package dev.personal.subtitle;
import android.content.*;
import android.graphics.*;
import android.net.Uri;
import android.os.*;
import com.google.mlkit.vision.common.InputImage;
import com.google.mlkit.vision.text.TextRecognition;
import com.google.mlkit.vision.text.TextRecognizer;
import com.google.mlkit.vision.text.japanese.JapaneseTextRecognizerOptions;
import java.net.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;
import org.json.*;
final class Pipeline {
    final Context context;
    final Handler main=new Handler(Looper.getMainLooper());
    final ExecutorService io=Executors.newSingleThreadExecutor();
    final TextRecognizer recognizer=TextRecognition.getClient(new JapaneseTextRecognizerOptions.Builder().build());
    long core=NativeCore.create(), request=0;
    volatile long generation=0;
    volatile HttpURLConnection connection;
    Future<?> pending;
    Runnable listener;
    Bitmap image;
    final RectF crop=new RectF(0,0,1,1);
    float zoom=1,panX=0,panY=0;
    boolean busy=false,freeApi=true,consent=false,remember=false,closed=false;
    int source=0;
    String japanese="",korean="",status="이미지를 선택하거나 다른 앱에서 공유하세요.",key="";
    long ocrMs=0,translateMs=0;
    Pipeline(Context c) {
        context=c.getApplicationContext();
        var prefs=context.getSharedPreferences("settings",0);
        freeApi=prefs.getBoolean("free",true); consent=prefs.getBoolean("consent",false); remember=prefs.getBoolean("remember",false);
        if(remember) try { key=SecureKeys.load(context); } catch(Exception e) { status="저장한 키를 읽지 못했습니다. 설정에서 다시 입력하세요."; }
    }
    void notifyUi() { if(listener!=null) listener.run(); }
    void load(Uri uri,int inputSource) {
        cancel(false);
        if(uri==null || !"content".equals(uri.getScheme())) { status="앱이 제공한 이미지 URI가 올바르지 않습니다."; notifyUi(); return; }
        busy=true; image=null; japanese=""; korean=""; status="이미지 읽는 중…"; notifyUi();
        final long ticket=++generation;
        pending=io.submit(()->{
            try {
                Bitmap decoded=ImageDecoder.decodeBitmap(ImageDecoder.createSource(context.getContentResolver(),uri),(decoder,info,src)->{
                    int w=info.getSize().getWidth(),h=info.getSize().getHeight();
                    if(w<=0 || h<=0 || (long)w*h>100000000) throw new IllegalArgumentException("oversize");
                    int sample=Math.max(1,(int)Math.ceil(Math.sqrt((double)w*h/4000000)));
                    decoder.setTargetSampleSize(sample);
                    decoder.setAllocator(ImageDecoder.ALLOCATOR_SOFTWARE);
                });
                main.post(()->{
                    if(closed || generation!=ticket) { decoded.recycle(); return; }
                    image=decoded; source=inputSource; crop.set(0,0,1,1); zoom=1; panX=panY=0;
                    busy=false; status="핀치로 확대하고 자막 영역을 드래그한 뒤 번역하세요."; notifyUi();
                });
            } catch(SecurityException e) { main.post(()->fail(ticket,"이미지 읽기 권한이 없습니다. 이미지 선택으로 다시 가져오세요.")); }
            catch(Exception | OutOfMemoryError e) { main.post(()->fail(ticket,"이미지를 읽지 못했습니다. PNG/JPEG 스크린샷을 다시 선택하세요.")); }
        });
    }
    void run() {
        if(busy || image==null) return;
        final Bitmap input=image;
        final int x=Math.max(0,(int)(crop.left*input.getWidth())),y=Math.max(0,(int)(crop.top*input.getHeight()));
        final int w=Math.min(input.getWidth()-x,(int)(crop.width()*input.getWidth()));
        final int h=Math.min(input.getHeight()-y,(int)(crop.height()*input.getHeight()));
        if(w<8 || h<8) { status="선택 영역이 너무 작습니다. 영역을 다시 그리세요."; notifyUi(); return; }
        busy=true; japanese=""; korean=""; status="일본어 OCR 처리 중…"; notifyUi();
        final long ticket=++generation;
        request=NativeCore.begin(core,source,w,h);
        if(request==0) { fail(ticket,"요청을 시작할 수 없습니다."); return; }
        final long started=SystemClock.elapsedRealtime();
        main.postDelayed(()->{ if(!closed && generation==ticket && busy) { cancel(false); status="처리 시간이 40초를 초과했습니다. 영역을 줄이거나 다시 시도하세요."; notifyUi(); } },40000);
        pending=io.submit(()->{
            try {
                final Bitmap selected=Bitmap.createBitmap(input,x,y,w,h);
                main.post(()->{
                    if(closed || generation!=ticket) return;
                    recognizer.process(InputImage.fromBitmap(selected,0))
                        .addOnSuccessListener(result->{
                            if(closed || generation!=ticket) return;
                            ocrMs=SystemClock.elapsedRealtime()-started;
                            int decision=NativeCore.accept(core,request,result.getText().getBytes(StandardCharsets.UTF_8));
                            japanese=new String(NativeCore.source(core),StandardCharsets.UTF_8);
                            if(decision==0) return;
                            if(decision==2) {
                                korean=new String(NativeCore.result(core),StandardCharsets.UTF_8); busy=false;
                                status="같은 원문의 최근 번역을 재사용했습니다."; notifyUi(); return;
                            }
                            if(decision!=1) { fail(ticket,"일본어를 인식하지 못했거나 텍스트가 너무 깁니다. 영역을 바꿔보세요."); return; }
                            if(key.isEmpty() || !consent) {
                                NativeCore.ocrOnly(core,request); busy=false; status="OCR 완료. 번역은 설정의 개인 API 키·전송 동의가 필요합니다."; notifyUi(); return;
                            }
                            status="한국어 번역 중…"; notifyUi(); translate(ticket,japanese,key,freeApi);
                        }).addOnFailureListener(e->fail(ticket,"OCR 실패. 모델 준비 상태와 이미지 품질을 확인하세요."));
                });
            } catch(Exception | OutOfMemoryError e) { main.post(()->fail(ticket,"선택 영역을 처리할 메모리가 부족합니다. 더 작은 이미지를 선택하세요.")); }
        });
    }
    private void translate(long ticket,String text,String apiKey,boolean free) {
        pending=io.submit(()->{
            final long started=SystemClock.elapsedRealtime();
            HttpURLConnection conn=null;
            try {
                if(Thread.currentThread().isInterrupted() || generation!=ticket) return;
                conn=(HttpURLConnection)new URL(free?"https://api-free.deepl.com/v2/translate":"https://api.deepl.com/v2/translate").openConnection();
                connection=conn; conn.setInstanceFollowRedirects(false); conn.setRequestMethod("POST");
                conn.setConnectTimeout(15000); conn.setReadTimeout(20000); conn.setDoOutput(true);
                conn.setRequestProperty("Authorization","DeepL-Auth-Key "+apiKey);
                conn.setRequestProperty("Content-Type","application/json");
                byte[] body=new JSONObject().put("text",new JSONArray().put(text)).put("source_lang","JA").put("target_lang","KO").toString().getBytes(StandardCharsets.UTF_8);
                conn.setFixedLengthStreamingMode(body.length);
                if(Thread.currentThread().isInterrupted() || generation!=ticket) return;
                try(OutputStream out=conn.getOutputStream()) {
                    if(Thread.currentThread().isInterrupted() || generation!=ticket) return;
                    out.write(body);
                }
                int code=conn.getResponseCode();
                if(code!=200) throw new IOException(httpError(code));
                ByteArrayOutputStream out=new ByteArrayOutputStream();
                try(InputStream in=conn.getInputStream()) {
                    byte[] chunk=new byte[8192]; int n;
                    while((n=in.read(chunk))!=-1) { if(out.size()+n>1048576) throw new IOException("번역 응답이 너무 큽니다."); out.write(chunk,0,n); }
                }
                JSONArray items=new JSONObject(out.toString(StandardCharsets.UTF_8.name())).getJSONArray("translations");
                if(items.length()!=1) throw new JSONException("shape");
                String translated=items.getJSONObject(0).getString("text");
                if(translated.trim().isEmpty()) throw new JSONException("empty");
                main.post(()->{
                    if(closed || generation!=ticket) return;
                    if(!NativeCore.complete(core,request,translated.getBytes(StandardCharsets.UTF_8))) return;
                    korean=translated; translateMs=SystemClock.elapsedRealtime()-started; busy=false;
                    status="번역 완료 · OCR "+ocrMs+" ms · 번역 "+translateMs+" ms"; notifyUi();
                });
            } catch(SocketTimeoutException e) { main.post(()->fail(ticket,"번역 연결 시간이 초과되었습니다. 직접 재시도하세요.")); }
            catch(JSONException e) { main.post(()->fail(ticket,"번역 서버 응답 형식이 올바르지 않습니다.")); }
            catch(IOException e) {
                // Only our predefined HTTP messages may be surfaced; never raw server/network messages.
                String message=e.getMessage()!=null && e.getMessage().startsWith("HTTP:") ? e.getMessage().substring(5) : "번역 서버에 연결하지 못했습니다. 인터넷 연결을 확인하세요.";
                main.post(()->fail(ticket,message));
            } finally { if(conn!=null) conn.disconnect(); if(connection==conn) connection=null; }
        });
    }
    private static String httpError(int status) {
        if(status==401 || status==403) return "HTTP:API 키 또는 Free/Pro 설정을 확인하세요.";
        if(status==429) return "HTTP:요청이 많습니다. 잠시 후 직접 재시도하세요.";
        if(status==456) return "HTTP:DeepL 사용 한도를 확인하세요.";
        return "HTTP:번역 서버 오류 ("+status+"). 직접 재시도하세요.";
    }
    void fail(long ticket,String message) {
        if(closed || generation!=ticket) return;
        NativeCore.fail(core,request); busy=false; status=message; notifyUi();
    }
    void cancel(boolean visible) {
        generation++; if(core!=0) NativeCore.cancel(core);
        if(pending!=null) pending.cancel(true);
        final HttpURLConnection old=connection; connection=null;
        if(old!=null) new Thread(old::disconnect,"cancel-http").start();
        boolean active=busy; busy=false;
        if(visible && active) { status="처리를 취소했습니다. 전송된 요청은 비용이 발생할 수 있습니다."; notifyUi(); }
    }
    void saveSettings(String entered,boolean free,boolean allowed,boolean persist) throws Exception {
        if(entered.contains("\n") || entered.contains("\r") || entered.length()>512) throw new IllegalArgumentException("key");
        if(persist && !entered.isEmpty()) SecureKeys.save(context,entered); else SecureKeys.clear(context);
        key=entered; freeApi=free; consent=allowed; remember=persist && !entered.isEmpty();
        context.getSharedPreferences("settings",0).edit().putBoolean("free",free).putBoolean("consent",allowed).putBoolean("remember",remember).apply();
        NativeCore.resetCache(core);
    }
    void close() { cancel(false); closed=true; listener=null; recognizer.close(); io.shutdownNow(); NativeCore.destroy(core); core=0; }
}
