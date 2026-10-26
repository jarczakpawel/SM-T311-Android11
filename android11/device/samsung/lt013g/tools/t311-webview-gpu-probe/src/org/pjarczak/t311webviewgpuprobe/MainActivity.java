package org.pjarczak.t311webviewgpuprobe;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.widget.TextView;
import android.widget.LinearLayout;

public final class MainActivity extends Activity {
    private static final String TAG = "T311WEBGPU";
    private boolean finished;
    private WebView view;
    private final Handler handler = new Handler(Looper.getMainLooper());

    private static final String HTML =
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'></head>" +
        "<body style='background:#efefef;color:#222'><h2>SM-T311 WebView GPU test</h2>" +
        "<canvas id='t' width='80' height='80'></canvas><div id='status'>Running</div></body></html>";

    private static final String JS =
        "(function() {" +
        "const r={};" +
        "r.userAgent=navigator.userAgent;" +
        "r.devicePixelRatio=window.devicePixelRatio;" +
        "try {const c=document.createElement('canvas');c.width=8;c.height=8;" +
        "const x=c.getContext('2d');x.fillStyle='#00ff00';x.fillRect(0,0,8,8);" +
        "const p=x.getImageData(4,4,1,1).data;r.canvas2d=(p[0]===0&&p[1]===255&&p[2]===0&&p[3]===255)?'PASS':'FAIL_'+Array.from(p).join(',');" +
        "}catch(e){r.canvas2d='ERROR_'+e.message;}" +
        "try {const c=document.createElement('canvas');c.width=16;c.height=16;" +
        "const gl=c.getContext('webgl',{preserveDrawingBuffer:true,antialias:false})||c.getContext('experimental-webgl',{preserveDrawingBuffer:true,antialias:false});" +
        "if (!gl){r.webgl1='UNAVAILABLE';}" +
        "else {" +
        "r.webglVersion=gl.getParameter(gl.VERSION);r.webglRenderer=gl.getParameter(gl.RENDERER);" +
        "const dbg=gl.getExtension('WEBGL_debug_renderer_info');r.unmaskedRenderer=dbg?gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL):'EXT_UNAVAILABLE';r.maxTexture=gl.getParameter(gl.MAX_TEXTURE_SIZE);" +
        "function makeShader(type,code){const s=gl.createShader(type);gl.shaderSource(s,code);gl.compileShader(s);" +
        "if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw new Error('SHADER_'+gl.getShaderInfoLog(s));return s;}" +
        "const vs=makeShader(gl.VERTEX_SHADER,'attribute vec2 a;void main(){gl_Position=vec4(a,0.0,1.0);}');" +
        "const fs=makeShader(gl.FRAGMENT_SHADER,'precision mediump float;uniform vec4 c;void main(){gl_FragColor=c;}');" +
        "const pr=gl.createProgram();gl.attachShader(pr,vs);gl.attachShader(pr,fs);gl.linkProgram(pr);" +
        "if(!gl.getProgramParameter(pr,gl.LINK_STATUS))throw new Error('LINK_'+gl.getProgramInfoLog(pr));" +
        "gl.useProgram(pr);const color=gl.getUniformLocation(pr,'c');gl.uniform4f(color,0,1,0,1);const a=gl.getAttribLocation(pr,'a');const buf=gl.createBuffer();" +
        "gl.bindBuffer(gl.ARRAY_BUFFER,buf);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,1,1]),gl.STATIC_DRAW);" +
        "gl.enableVertexAttribArray(a);gl.vertexAttribPointer(a,2,gl.FLOAT,false,0,0);" +
        "gl.viewport(0,0,16,16);gl.clearColor(1,0,0,1);gl.clear(gl.COLOR_BUFFER_BIT);" +
        "gl.drawArrays(gl.TRIANGLE_STRIP,0,4);gl.finish();" +
        "const p=new Uint8Array(4);gl.readPixels(8,8,1,1,gl.RGBA,gl.UNSIGNED_BYTE,p);" +
        "r.webgl1=(p[0]<20&&p[1]>240&&p[2]<20&&p[3]===255)?'PASS':'FAIL_'+Array.from(p).join(',');" +
        "let fail=0;const start=performance.now();for(let i=0;i<60;i++){const blue=i%2===0;gl.uniform4f(color,blue?0:1,blue?0:1,blue?1:0,1);gl.drawArrays(gl.TRIANGLE_STRIP,0,4);gl.readPixels(8,8,1,1,gl.RGBA,gl.UNSIGNED_BYTE,p);if(blue?(p[2]<240||p[0]>20||p[1]>20):(p[0]<240||p[1]<240||p[2]>20))fail++;}r.webglStress=(fail===0)?'PASS':'FAIL_'+fail;r.webglStressFrames=60;r.webglStressMs=Math.round(performance.now()-start);r.webglError=gl.getError();" +
        "gl.deleteBuffer(buf);gl.deleteProgram(pr);gl.deleteShader(vs);gl.deleteShader(fs);" +
        "gl.getExtension('WEBGL_lose_context')?.loseContext();" +
        "}" +
        "}catch(e){r.webgl1='ERROR_'+e.message;}" +
        "try {const c=document.createElement('canvas');r.webgl2=c.getContext('webgl2')?'AVAILABLE':'UNAVAILABLE';" +
        "}catch(e){r.webgl2='ERROR_'+e.message;}" +
        "try {const x=document.getElementById('t').getContext('2d');x.fillStyle='#00ff00';x.fillRect(0,0,80,80);" +
        "document.getElementById('status').textContent=JSON.stringify({canvas2d:r.canvas2d,webgl1:r.webgl1,webgl2:r.webgl2});" +
        "}catch(e){r.display='ERROR_'+e.message;}" +
        "return JSON.stringify(r);" +
        "})()";

    private void finishProbe(String result) {
        if (finished) return;
        finished = true;
        Log.e(TAG, "T311_WEBVIEW_GPU_RESULT=" + result);
        if (view != null) {
            Bitmap bitmap = Bitmap.createBitmap(80,80,Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(bitmap);
            view.draw(canvas);
            Log.e(TAG, "T311_WEBVIEW_DRAW_RESULT=size80x80 pixel=" + Integer.toHexString(bitmap.getPixel(40,40)));
            bitmap.recycle();
        }
        handler.postDelayed(this::finish, 1200);
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        TextView title = new TextView(this);
        title.setText("SM-T311 WebView rendering diagnostic");
        root.addView(title);
        view = new WebView(this);
        root.addView(view, new LinearLayout.LayoutParams(-1,0,1));
        setContentView(root);
        Log.e(TAG, "T311_WEBVIEW_PROVIDER=" + WebView.getCurrentWebViewPackage());
        view.getSettings().setJavaScriptEnabled(true);
        view.getSettings().setDomStorageEnabled(true);
        view.setWebChromeClient(new WebChromeClient());
        view.setWebViewClient(new WebViewClient() {
            @Override public void onPageFinished(WebView v, String u) {
                Log.e(TAG, "T311_WEBVIEW_PAGE_FINISHED");
                handler.postDelayed(() -> v.evaluateJavascript(JS, value -> finishProbe(value)), 900);
            }
            @Override public void onReceivedError(WebView v, WebResourceRequest req, WebResourceError error) {
                Log.e(TAG, "T311_WEBVIEW_PAGE_ERROR=" + error.getDescription());
            }
        });
        handler.postDelayed(() -> finishProbe("TIMEOUT"), 20000);
        view.loadDataWithBaseURL("about:blank",HTML,"text/html","UTF-8",null);
    }

    @Override protected void onDestroy() {
        if (view != null) { view.destroy(); view = null; }
        super.onDestroy();
    }
}
