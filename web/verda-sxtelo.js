/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2012  Neil Roberts
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

var YEAR_IN_SECONDS = 31556926;

function chatCb ()
{
  var langCode = document.getElementById ("language-select").value;
  var expiresDate = new Date ((new Date ()).getTime () +
                              YEAR_IN_SECONDS * 1000);
  document.cookie = ("langCode=" + langCode + ";max-age=" + YEAR_IN_SECONDS +
                     ";expires=" + expiresDate.toGMTString ());
  window.location = "chat.@LANG_CODE@.html?" + langCode;
}

function loadCb ()
{
  var match;

  if (document.cookie &&
      (match = document.cookie.match (/(?:; *|^)langCode=([a-z]+)(?:$|;)/)))
    $("#language-select").val (match[1]);

  $("#chat-button").bind ("click", chatCb);
}

$(window).load (loadCb);
