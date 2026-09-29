package dev.personal.subtitle;
import android.content.*;
import android.content.pm.ActivityInfo;
import android.graphics.*;
import android.net.Uri;
import android.os.SystemClock;
import android.view.*;
import androidx.test.core.app.ActivityScenario;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.uiautomator.UiDevice;
import org.junit.*;
import org.junit.runner.RunWith;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.lang.reflect.Field;
import java.util.concurrent.atomic.*;
import static org.junit.Assert.*;
@RunWith(AndroidJUnit4.class)
public final class FlowTest {
    private static final Uri IMAGE=Uri.parse("content://dev.personal.subtitle.test.fixture/image");
    private static <T> T field(Object o,String name) {
        try { Field f=o.getClass().getDeclaredField(name); f.setAccessible(true); return (T)f.get(o); }
        catch(Exception e) { throw new AssertionError(e); }
    }
    private void waitFor(java.util.function.BooleanSupplier condition,long ms) {
        long end=SystemClock.elapsedRealtime()+ms;
        while(SystemClock.elapsedRealtime()<end) { if(condition.getAsBoolean()) return; SystemClock.sleep(60); }
        fail("Timed out");
    }
    @Test public void unicodeAndLateResultsInRealJni() {
        long s=NativeCore.create(); long id=NativeCore.begin(s,1,100,100); NativeCore.cancel(s);
        assertEquals(0,NativeCore.accept(s,id,"old".getBytes(StandardCharsets.UTF_8)));
        id=NativeCore.begin(s,0,100,100);
        assertEquals(1,NativeCore.accept(s,id,"今日は".getBytes(StandardCharsets.UTF_8)));
        assertTrue(NativeCore.complete(s,id,"오늘은".getBytes(StandardCharsets.UTF_8)));
        assertEquals("오늘은",new String(NativeCore.result(s),StandardCharsets.UTF_8)); NativeCore.destroy(s);
    }
    @Test public void sharedImageCropOcrRotationCancelAndRetry() throws Exception {
        Context target=InstrumentationRegistry.getInstrumentation().getTargetContext();
        Intent share=new Intent(Intent.ACTION_SEND).setClass(target,MainActivity.class).setType("image/png")
            .putExtra(Intent.EXTRA_STREAM,IMAGE).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_ACTIVITY_NEW_TASK);
        try(ActivityScenario<MainActivity> scenario=ActivityScenario.launch(share)) {
            AtomicReference<Pipeline> ref=new AtomicReference<>();
            scenario.onActivity(a->ref.set(field(a,"model")));
            Pipeline p=ref.get();
            waitFor(()->p.image!=null && !p.busy,10000);
            scenario.onActivity(a->{
                // Real touch drag crops the image before ML Kit sees it.
                CropView crop=field(a,"cropView"); long now=SystemClock.uptimeMillis();
                float y=crop.getHeight()/2f;
                crop.dispatchTouchEvent(MotionEvent.obtain(now,now,MotionEvent.ACTION_DOWN,4,y-45,0));
                crop.dispatchTouchEvent(MotionEvent.obtain(now,now+30,MotionEvent.ACTION_MOVE,crop.getWidth()-4,y+45,0));
                crop.dispatchTouchEvent(MotionEvent.obtain(now,now+60,MotionEvent.ACTION_UP,crop.getWidth()-4,y+45,0));
                p.key=""; p.consent=false; p.run();
            });
            waitFor(()->!p.busy,30000);
            assertTrue(p.status,p.japanese.contains("天気")); assertEquals("",p.korean);
            org.json.JSONObject metrics=new org.json.JSONObject().put("platform","Android 15 API35 x86_64 emulator").put("realDevice",false).put("ocrMs",p.ocrMs).put("appPssKiB",android.os.Debug.getPss()).put("inputWidth",1000).put("inputHeight",180).put("translationVerified",false).put("networkTest","Wi-Fi and mobile data disabled before OCR; bundled model");
            try(FileOutputStream out=target.openFileOutput("test-metrics.json",0)) { out.write(metrics.toString(2).getBytes(StandardCharsets.UTF_8)); }
            final RectF before=new RectF(p.crop);
            scenario.onActivity(a->a.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE));
            SystemClock.sleep(1200);
            scenario.onActivity(a->{ assertSame(p,field(a,"model")); assertEquals(before,p.crop); });
            scenario.onActivity(a->{ p.run(); p.cancel(true); });
            SystemClock.sleep(700); assertFalse(p.busy); assertTrue(p.status.contains("취소"));
            scenario.onActivity(a->p.run()); waitFor(()->!p.busy,30000); assertTrue(p.japanese.contains("天気"));
            scenario.onActivity(a->p.load(Uri.parse("content://dev.personal.subtitle.test.fixture/denied"),0));
            waitFor(()->!p.busy,10000); assertTrue(p.status,p.status.contains("권한"));
            scenario.onActivity(a->p.load(IMAGE,0)); waitFor(()->!p.busy,10000); assertNotNull(p.image);
            scenario.onActivity(a->{ p.run(); a.moveTaskToBack(true); });
            SystemClock.sleep(800); assertFalse(p.busy);
        }
    }
    @Test public void pickerCancellation() throws Exception {
        try(ActivityScenario<MainActivity> scenario=ActivityScenario.launch(MainActivity.class)) {
            AtomicReference<Pipeline> ref=new AtomicReference<>();
            scenario.onActivity(a->{ ref.set(field(a,"model")); ((android.widget.Button)field(a,"pick")).performClick(); });
            SystemClock.sleep(1000); UiDevice.getInstance(InstrumentationRegistry.getInstrumentation()).pressBack();
            waitFor(()->ref.get().status.contains("취소"),6000);
            assertFalse(ref.get().busy);
        }
    }
}
