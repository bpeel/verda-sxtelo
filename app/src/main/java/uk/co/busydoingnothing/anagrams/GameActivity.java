/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021  Neil Roberts
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

import androidx.appcompat.app.AppCompatActivity;
import android.app.Activity;
import android.content.Intent;
import android.content.res.AssetManager;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.opengl.GLSurfaceView;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.TextView;

public class GameActivity extends AppCompatActivity
{
  private static String TAG = "GameActivity";
  private static String INSTANCE_STATE_KEY = "VsxState";
  /* The invite URL is saved in the instance state only to check
   * whether the intent we receive in onCreate is the same as the one
   * we used for the instance state. The lower layers handle storing
   * the actual conversation ID themselves so we don’t want to pass
   * this invite URL down.
   */
  private static String INVITE_URL_KEY = "VsxInviteUrl";

  private GameView surface;

  @Override
  protected void onCreate(Bundle savedInstanceState)
  {
    super.onCreate(savedInstanceState);

    setContentView(R.layout.game);

    surface = (GameView) findViewById(R.id.gl_view);

    EditText nameEdit = (EditText) findViewById(R.id.player_name);

    SharedPreferences prefs =
      getSharedPreferences (Prefs.FILE_NAME, MODE_PRIVATE);
    String playerName = prefs.getString (Prefs.PLAYER_NAME, null /* default */);

    if (playerName == null)
      surface.setFirstRun();
    else
      nameEdit.setText (playerName);

    String inviteUrl = getInviteUrlFromIntent();

    if (savedInstanceState != null) {
      String previousInviteUrl = savedInstanceState.getString(INVITE_URL_KEY);

      /* We only want to use one of either the instance state or the
       * invite URL. If the invite URL is the same as the one we last
       * used in the instance state then we’ll assume the instance
       * state is more up-to-date and use that instead.
       */
      if (inviteUrl == null ||
          (previousInviteUrl != null && inviteUrl.equals(previousInviteUrl))) {
        String instanceState = savedInstanceState.getString(INSTANCE_STATE_KEY);

        if (instanceState != null)
          surface.setInstanceState(instanceState);

        inviteUrl = null;
      }
    }

    if (inviteUrl != null)
      surface.setInviteUrl(inviteUrl);

    nameEdit.setOnEditorActionListener(new TextView.OnEditorActionListener() {
        @Override
        public boolean onEditorAction(TextView v,
                                      int actionId,
                                      KeyEvent event) {
          if (actionId == EditorInfo.IME_ACTION_DONE)
            surface.setPlayerName(nameEdit.getText());

          return false;
        }
      });
  }

  private String getInviteUrlFromIntent()
  {
    Intent intent = getIntent();

    if (intent == null)
      return null;

    String action = intent.getAction();

    if (action == null || !action.equals(Intent.ACTION_VIEW))
      return null;

    return intent.getDataString();
  }

  @Override
  public void onNewIntent(Intent intent)
  {
    setIntent(intent);

    String inviteUrl = getInviteUrlFromIntent();

    if (inviteUrl != null)
      surface.setInviteUrl(inviteUrl);
  }

  @Override
  protected void onStart()
  {
    super.onStart();

    surface.onResume();
  }

  @Override
  protected void onStop()
  {
    super.onStop();

    surface.onPause();
  }

  @Override
  protected void onSaveInstanceState(Bundle outState)
  {
    super.onSaveInstanceState(outState);

    String instanceState = surface.getInstanceState();

    if (instanceState != null)
      outState.putString(INSTANCE_STATE_KEY, instanceState);

    String inviteUrl = getInviteUrlFromIntent();

    if (inviteUrl != null)
      outState.putString(INVITE_URL_KEY, inviteUrl);
  }
}
