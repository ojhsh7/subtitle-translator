package dev.personal.subtitle;
import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.graphics.*;
import java.io.*;
public final class FixtureProvider extends ContentProvider {
    public boolean onCreate() { return true; }
    public String getType(Uri u) { return "image/png"; }
    public ParcelFileDescriptor openFile(Uri uri,String mode) throws FileNotFoundException {
        if(!"/image".equals(uri.getPath())) throw new SecurityException("Test URI denied");
        File file=new File(getContext().getFilesDir(),"fixture.png");
        Bitmap b=Bitmap.createBitmap(1000,180,Bitmap.Config.ARGB_8888);
        Canvas canvas=new Canvas(b); canvas.drawColor(Color.WHITE);
        Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG); paint.setColor(Color.BLACK); paint.setTextSize(52);
        canvas.drawText("今日はいい天気です。",24,108,paint);
        try(FileOutputStream out=new FileOutputStream(file)) { b.compress(Bitmap.CompressFormat.PNG,100,out); }
        catch(IOException e) { throw new FileNotFoundException("Fixture creation failed"); }
        b.recycle();
        return ParcelFileDescriptor.open(file,ParcelFileDescriptor.MODE_READ_ONLY);
    }
    public Cursor query(Uri u,String[] p,String s,String[] a,String o) { return null; }
    public Uri insert(Uri u,ContentValues v) { throw new UnsupportedOperationException(); }
    public int update(Uri u,ContentValues v,String s,String[] a) { throw new UnsupportedOperationException(); }
    public int delete(Uri u,String s,String[] a) { throw new UnsupportedOperationException(); }
}
