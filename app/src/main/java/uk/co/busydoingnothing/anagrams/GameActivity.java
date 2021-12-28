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
import android.content.res.AssetManager;
import android.os.Bundle;
import android.opengl.GLSurfaceView;
import android.util.Log;
import android.view.View;

public class GameActivity extends AppCompatActivity
{
  private static String TAG = "GameActivity";
  private static String PERSON_ID_KEY = "VsxPersonId";

  private GameView surface;

  @Override
  protected void onCreate(Bundle savedInstanceState)
  {
    super.onCreate(savedInstanceState);

    setContentView(R.layout.game);

    surface = (GameView) findViewById(R.id.gl_view);

    if (savedInstanceState != null &&
        savedInstanceState.containsKey(PERSON_ID_KEY))
      surface.setPersonId(savedInstanceState.getLong(PERSON_ID_KEY));
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

    Long personId = surface.getPersonId();

    if (personId != null)
      outState.putLong(PERSON_ID_KEY, personId);
  }
}
