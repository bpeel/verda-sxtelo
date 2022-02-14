/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021, 2022  Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

package uk.co.busydoingnothing.anagrams;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.content.res.Resources;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.os.Handler;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewParent;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class GameView extends GLSurfaceView
{
  private int pointersDown = 0;
  private GameRenderer renderer;
  private long nativeData;
  private int dpi;
  private Context context;

  static {
    System.loadLibrary("anagrams");
  }

  public GameView(Context context)
  {
    super(context);
    initialize(context);
  }

  public GameView(Context context, AttributeSet attrs)
  {
    super(context, attrs);
    initialize(context);
  }

  private void initialize(Context context)
  {
    this.context = context;

    dpi = (int) (context.getResources().getDisplayMetrics().densityDpi + 0.5f);

    setEGLContextClientVersion(2);
    renderer = new GameRenderer();
    setRenderer(renderer);
    setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
  }

  private void handlePointerDown(int pointerId, int x, int y)
  {
    if (pointerId < 0 || pointerId > 1)
      return;

    if ((pointersDown & (1 << pointerId)) != 0)
      return;

    pointersDown |= 1 << pointerId;

    queueEvent(new Runnable() {
        public void run() {
          renderer.handlePointerDown(nativeData, pointerId, x, y);
        }
      });
  }

  private void handlePointerMotion(int pointerId, int x, int y)
  {
    if (pointerId < 0 || pointerId > 1)
      return;

    if ((pointersDown & (1 << pointerId)) == 0)
      return;

    queueEvent(new Runnable() {
        public void run() {
          renderer.handlePointerMotion(nativeData, pointerId, x, y);
        }
      });
  }

  private void handlePointerUp(int pointerId)
  {
    if (pointerId < 0 || pointerId > 1)
      return;

    if ((pointersDown & (1 << pointerId)) == 0)
      return;

    pointersDown &= ~(1 << pointerId);

    queueEvent(new Runnable() {
        public void run() {
          renderer.handlePointerUp(nativeData, pointerId);
        }
      });
  }

  private void handleAllPointersUp()
  {
    for (int i = 0; i < 2; i++) {
      if ((pointersDown & (1 << i)) == 0)
        continue;

      handlePointerUp(i);
    }
  }

  private void handleGestureCancel()
  {
    pointersDown = 0;

    queueEvent(new Runnable() {
        public void run() {
          renderer.handleGestureCancel(nativeData);
        }
      });
  }

  public void queueFlushIdleEvents()
  {
    queueEvent(new Runnable() {
        public void run() {
          renderer.flushIdleEvents(nativeData);
        }
      });
  }

  public void shareLink(String link)
  {
    Handler mainHandler = new Handler(context.getMainLooper());

    mainHandler.post(new Runnable() {
        @Override
        public void run()
        {
          Intent sendIntent = new Intent();
          sendIntent.setAction(Intent.ACTION_SEND);
          sendIntent.putExtra(Intent.EXTRA_TEXT, link);
          sendIntent.setType("text/plain");

          Intent shareIntent = Intent.createChooser(sendIntent, null);
          context.startActivity(shareIntent);
        }
      });
  }

  public void setInstanceState(String instanceState)
  {
    queueEvent(new Runnable() {
        public void run() {
          renderer.setInstanceState(nativeData, instanceState);
        }
      });
  }

  public String getInstanceState()
  {
    return renderer.getInstanceState(nativeData);
  }

  public void setInviteUrl(String url)
  {
    queueEvent(new Runnable() {
        public void run() {
          renderer.setInviteUrl(nativeData, url);
        }
      });
  }

  public void setFirstRun()
  {
    queueEvent(new Runnable() {
        public void run() {
          renderer.setFirstRun(nativeData);
        }
      });
  }

  public void setNameProperties(boolean visible,
                                int yPos,
                                int width)
  {
    Handler mainHandler = new Handler(context.getMainLooper());

    mainHandler.post(new Runnable() {
        @Override
        public void run()
        {
          ViewParent parent = getParent();

          if (!(parent instanceof GameLayout))
            return;

          GameLayout layout = (GameLayout) parent;

          layout.setNamePosition(yPos, width);

          View nameTextView = layout.findViewById(R.id.player_name);

          if (nameTextView != null)
            nameTextView.setVisibility(visible ? View.VISIBLE : View.GONE);
        }
      });
  }

  public void requestName()
  {
    Handler mainHandler = new Handler(context.getMainLooper());

    mainHandler.post(new Runnable() {
        @Override
        public void run()
        {
          ViewParent parent = getParent();

          if (!(parent instanceof GameLayout))
            return;

          GameLayout layout = (GameLayout) parent;

          EditText nameTextView = layout.findViewById(R.id.player_name);

          if (nameTextView != null) {
            setPlayerName(nameTextView.getText());

            InputMethodManager imm =
              (InputMethodManager)
              context.getSystemService(Context.INPUT_METHOD_SERVICE);

            imm.hideSoftInputFromWindow(nameTextView.getWindowToken(), 0);
          }
        }
      });
  }

  public void setNameHeight(int height)
  {
    queueEvent(new Runnable() {
        @Override
        public void run() {
          renderer.setNameHeight(nativeData, height);
        }
      });
  }

  private boolean stringContainsNonWhitespace(CharSequence str)
  {
    for (int i = 0; i < str.length(); i++) {
      if (!Character.isWhitespace(str.charAt(i)))
        return true;
    }

    return false;
  }

  public void setPlayerName(CharSequence name)
  {
    if (!stringContainsNonWhitespace(name))
      return;

    String nameString = name.toString ();

    SharedPreferences prefs =
      context.getSharedPreferences (Prefs.FILE_NAME, context.MODE_PRIVATE);
    SharedPreferences.Editor editor = prefs.edit ();
    editor.putString (Prefs.PLAYER_NAME, nameString);
    editor.commit ();

    queueEvent(new Runnable() {
        @Override
        public void run() {
          renderer.setPlayerName(nativeData, nameString);
        }
      });
  }

  @Override
  public boolean onTouchEvent(MotionEvent event)
  {
    int pointerId, pointerIndex;

    switch (event.getActionMasked()) {
    case MotionEvent.ACTION_DOWN:
      pointerId = event.getPointerId(0);
      handlePointerDown(pointerId,
                        (int) event.getX(),
                        (int) event.getY());
      return true;

    case MotionEvent.ACTION_POINTER_DOWN:
      pointerIndex = event.getActionIndex();
      pointerId = event.getPointerId(pointerIndex);
      handlePointerDown(pointerId,
                        (int) event.getX(pointerIndex),
                        (int) event.getY(pointerIndex));
      return true;

    case MotionEvent.ACTION_MOVE:
      for (int i = 0; i < event.getPointerCount(); i++) {
        pointerId = event.getPointerId(i);
        handlePointerMotion(pointerId,
                            (int) event.getX(i),
                            (int) event.getY(i));
      }
      return true;

    case MotionEvent.ACTION_POINTER_UP:
      pointerIndex = event.getActionIndex();
      pointerId = event.getPointerId(pointerIndex);
      handlePointerUp(pointerId);
      return true;

    case MotionEvent.ACTION_UP:
      handleAllPointersUp();
      return true;

    case MotionEvent.ACTION_CANCEL:
      handleGestureCancel();
      return true;
    }

    return false;
  }

  public class GameRenderer implements GLSurfaceView.Renderer
  {
    public GameRenderer()
    {
      nativeData = createNativeData(GameView.this,
                                    getContext().getAssets(),
                                    dpi);

      Resources resources = getResources();

      setGameLanguageCode(nativeData,
                          resources.getString(R.string.game_language_code));
    }

    @Override
    public void onDrawFrame(GL10 unused)
    {
      redraw(nativeData);
    }

    @Override
    public void onSurfaceChanged(GL10 unused, int width, int height)
    {
      resize(nativeData, width, height);
    }

    @Override
    public void onSurfaceCreated(GL10 unused, EGLConfig config)
    {
      initContext(nativeData);
    }

    @Override
    protected void finalize()
    {
      freeNativeData(nativeData);
    }

    private native long createNativeData(GLSurfaceView surface,
                                         AssetManager assetManager,
                                         int dpi);
    private native void setInstanceState(long nativeData, String instanceState);
    private native String getInstanceState(long nativeData);
    private native void setInviteUrl(long nativeData, String inviteUrl);
    private native void setGameLanguageCode(long nativeData,
                                            String languageCode);
    private native void setFirstRun(long nativeData);
    private native void setNameHeight(long nativeData, int height);
    private native void setPlayerName(long nativeData, String name);
    private native boolean initContext(long nativeData);
    private native void resize(long nativeData,
                               int width, int height);
    private native void redraw(long nativeData);
    private native void flushIdleEvents(long nativeData);
    private native void freeNativeData(long nativeData);

    private native void handlePointerDown(long nativeData,
                                          int pointer,
                                          int x, int y);
    private native void handlePointerMotion(long nativeData,
                                            int pointer,
                                            int x, int y);
    private native void handlePointerUp(long nativeData, int pointer);
    private native void handleGestureCancel(long nativeData);
  }
}
