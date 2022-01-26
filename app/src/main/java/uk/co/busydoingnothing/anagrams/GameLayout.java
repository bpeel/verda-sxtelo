/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2022  Neil Roberts
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
import android.util.AttributeSet;
import android.view.View;
import android.view.ViewGroup;

public class GameLayout extends ViewGroup
{
  private int nameYPos, nameMaxWidth;

  public GameLayout(Context context)
  {
    super(context);
  }

  public GameLayout(Context context, AttributeSet attrs)
  {
    this(context, attrs, 0);
  }

  public GameLayout(Context context, AttributeSet attrs, int defStyle)
  {
    super(context, attrs, defStyle);
  }

  @Override
  public boolean shouldDelayChildPressedState()
  {
    return false;
  }

  @Override
  protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec)
  {
    int count = getChildCount();

    int maxHeight = 0;
    int maxWidth = 0;
    int childState = 0;

    for (int i = 0; i < count; i++) {
      final View child = getChildAt(i);

      if (child.getVisibility() == GONE)
        continue;

      measureChild(child, widthMeasureSpec, heightMeasureSpec);

      if (i == 0) {
        maxWidth = child.getMeasuredWidth();
        maxHeight = child.getMeasuredHeight();
        childState = child.getMeasuredState();
      }
    }

    maxHeight = Math.max(maxHeight, getSuggestedMinimumHeight());
    maxWidth = Math.max(maxWidth, getSuggestedMinimumWidth());

    setMeasuredDimension(resolveSizeAndState(maxWidth,
                                             widthMeasureSpec,
                                             childState),
                         resolveSizeAndState(maxHeight,
                                             heightMeasureSpec,
                                             childState <<
                                             MEASURED_HEIGHT_STATE_SHIFT));
  }

  private void updateNameHeight(int height)
  {
    View child = getChildAt(0);

    if (!(child instanceof GameView))
      return;

    GameView surface = (GameView) child;

    surface.setNameHeight(height);
  }

  @Override
  protected void onLayout(boolean changed,
                          int left, int top,
                          int right, int bottom)
  {
    final int count = getChildCount();

    int leftPos = getPaddingLeft();
    int rightPos = right - left - getPaddingRight();
    int topPos = getPaddingTop();
    int bottomPos = bottom - top - getPaddingBottom();

    for (int i = 0; i < count; i++) {
      final View child = getChildAt(i);

      if (child.getVisibility() == GONE)
        continue;

      if (i == 1) {
        int nameWidth = child.getMeasuredWidth();
        int nameHeight = child.getMeasuredHeight();

        if (nameWidth > nameMaxWidth)
          nameWidth = nameMaxWidth;

        int x = (leftPos + rightPos) / 2 - nameWidth / 2;
        int y = topPos + nameYPos;

        child.layout(x, y,
                     x + nameWidth,
                     y + nameHeight);

        updateNameHeight(nameHeight);
      } else {
        child.layout(leftPos, topPos, rightPos, bottomPos);
      }
    }
  }

  public void setNamePosition(int yPos, int width)
  {
    nameYPos = yPos;
    nameMaxWidth = width;
    requestLayout();
  }
}
