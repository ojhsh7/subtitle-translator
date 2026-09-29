package dev.personal.subtitle;
import android.content.Context;
import android.graphics.*;
import android.view.*;
final class CropView extends View {
    private final Pipeline model;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Matrix matrix=new Matrix();
    private final ScaleGestureDetector scaler;
    private float downX,downY,startX,startY;
    private boolean drawing=false, multi=false;
    boolean selectMode=true;
    CropView(Context c,Pipeline p) {
        super(c); model=p; setContentDescription("이미지 자막 영역: 한 손가락으로 영역 선택, 두 손가락으로 확대");
        scaler=new ScaleGestureDetector(c,new ScaleGestureDetector.SimpleOnScaleGestureListener() {
            @Override public boolean onScale(ScaleGestureDetector d) {
                model.zoom=Math.max(1,Math.min(6,model.zoom*d.getScaleFactor())); invalidate(); return true;
            }
        });
    }
    private void updateMatrix() {
        if(model.image==null) return;
        float fit=Math.min((float)getWidth()/model.image.getWidth(),(float)getHeight()/model.image.getHeight());
        float scale=fit*model.zoom;
        float maxX=Math.max(0,(model.image.getWidth()*scale-getWidth())/2);
        float maxY=Math.max(0,(model.image.getHeight()*scale-getHeight())/2);
        model.panX=Math.max(-maxX,Math.min(maxX,model.panX));
        model.panY=Math.max(-maxY,Math.min(maxY,model.panY));
        matrix.reset(); matrix.postScale(scale,scale);
        matrix.postTranslate((getWidth()-model.image.getWidth()*scale)/2+model.panX,(getHeight()-model.image.getHeight()*scale)/2+model.panY);
    }
    @Override protected void onDraw(Canvas c) {
        c.drawColor(Color.rgb(26,32,42));
        if(model.image==null) {
            paint.setColor(Color.WHITE); paint.setTextSize(34);
            c.drawText("이미지를 선택하세요",24,getHeight()/2f,paint); return;
        }
        updateMatrix(); c.drawBitmap(model.image,matrix,paint);
        RectF r=new RectF(model.crop.left*model.image.getWidth(),model.crop.top*model.image.getHeight(),
            model.crop.right*model.image.getWidth(),model.crop.bottom*model.image.getHeight());
        matrix.mapRect(r); paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(4); paint.setColor(Color.rgb(66,230,175));
        c.drawRect(r,paint); paint.setStyle(Paint.Style.FILL);
    }
    private float[] point(float x,float y) {
        Matrix inverse=new Matrix(); matrix.invert(inverse); float[] p={x,y}; inverse.mapPoints(p);
        p[0]=Math.max(0,Math.min(1,p[0]/model.image.getWidth())); p[1]=Math.max(0,Math.min(1,p[1]/model.image.getHeight())); return p;
    }
    @Override public boolean onTouchEvent(MotionEvent e) {
        if(model.image==null || model.busy) return false;
        getParent().requestDisallowInterceptTouchEvent(true); scaler.onTouchEvent(e); updateMatrix();
        if(e.getPointerCount()>1) { multi=true; drawing=false; }
        switch(e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                downX=e.getX(); downY=e.getY(); multi=false;
                if(selectMode) { float[] p=point(downX,downY); startX=p[0]; startY=p[1]; drawing=true; }
                return true;
            case MotionEvent.ACTION_MOVE:
                if(!scaler.isInProgress() && !multi) {
                    if(drawing) {
                        float[] p=point(e.getX(),e.getY());
                        model.crop.set(Math.min(startX,p[0]),Math.min(startY,p[1]),Math.max(startX,p[0]),Math.max(startY,p[1]));
                    } else if(!selectMode) { model.panX+=e.getX()-downX; model.panY+=e.getY()-downY; }
                    downX=e.getX(); downY=e.getY(); invalidate();
                }
                return true;
            case MotionEvent.ACTION_UP: drawing=false; performClick(); return true;
            case MotionEvent.ACTION_CANCEL: drawing=false; return true;
            default: return true;
        }
    }
    @Override public boolean performClick() { super.performClick(); return true; }
}
