/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011, 2012, 2013, 2020, 2021, 2022  Neil Roberts
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

var SHOUT_TIME = 10 * 1000;
var TILE_SIZE = 20;
var SHORT_GAME_N_TILES = 50;
var NORMAL_GAME_N_TILES = 122;
var DEFAULT_N_TILES = SHORT_GAME_N_TILES;

var PLAYER_CONNECTED = (1 << 0);
var PLAYER_TYPING = (1 << 1);
var PLAYER_NEXT_TURN = (1 << 2);

function MessageReader (dv, pos)
{
  this.dv = dv;
  this.pos = 0;
}

MessageReader.prototype.getString = function ()
{
  var s = "";

  while (!this.isFinished ()) {
    var c = this.getUint8 ();

    if (c == 0)
      break;

    s = s + '%';
    if (c < 16)
      s = s + '0';
    s = s + c.toString (16);
  }

  return decodeURIComponent (s);
};

MessageReader.prototype.getUint64 = function ()
{
  var a = new Uint8Array (8);
  var i;

  for (i = 0; i < 8; i++)
    a[i] = this.getUint8 ();

  return a;
};

MessageReader.prototype.getUint8 = function ()
{
  return this.dv.getUint8 (this.pos++);
};

MessageReader.prototype.getInt16 = function ()
{
  var val = this.dv.getInt16 (this.pos, true /* littleEndian */);
  this.pos += 2;
  return val;
};

MessageReader.prototype.isFinished = function ()
{
  return this.pos >= this.dv.byteLength;
};

/**@constructor*/
function ChatSession (playerName)
{
  this.personId = null;
  this.personNumber = null;
  this.messageNumber = 0;
  this.sentTypingState = false;
  this.unreadMessages = 0;
  this.players = [];
  this.tiles = [];
  this.dragTile = null;
  this.dragTouchId = null;
  this.lastMovedTile = null;
  this.soundOn = true;
  this.totalNumTiles = DEFAULT_N_TILES;
  this.syncReceived = false;

  /* Set of the tile numbers that need their position to be sent to
   * the server. */
  this.dirtyTiles = new Set ();
  /* Timeout to check if the bufferedAmount is empty in order to
   * update the tiles. */
  this.dirtyTilesTimeout = null;

  this.sock = null;
  this.sockHandlers = [];
  this.connected = false;
  this.reconnectTimeout = null;
  this.reconnectCount = 0;
  this.keepAliveTimeout = null;

  this.playerName = playerName || "ludanto";

  var search = window.location.search;

  this.roomName = "@LANG_CODE@:default";
  this.conversationId = null;

  if (search && search.match (/^\?.+$/))
  {
    var joinPart = search.substring (1);
    var id = this.decodeId(joinPart);

    if (id == null)
    {
      this.roomName = ("@LANG_CODE@:" + decodeURIComponent (joinPart));
    }
    else
    {
      this.conversationId = id;
      $("#blurb").text ("@INVITE_BLURB@");
    }
  }

  this.handleResize ();
  $(window).resize (this.handleResize.bind (this));
}

ChatSession.prototype.handleResize = function ()
{
  var scaledRegion = $("#scaled-region");
  var mb = $("#messages-box");
  var winHeight = $(window).height ();
  var content = $(".content");

  /* Reset the heights back to the default */
  scaledRegion.css ("font-size", "1.0em");
  mb.css ("height", "");

  var contentOffset = content.offset ();
  var contentBottom = contentOffset.top + content.outerHeight () + 3;

  if (contentBottom > winHeight)
  {
    /* Scale the scaledRegion so that the contents will exactly fit */
    var boardHeight = scaledRegion.outerHeight ();

    if (contentBottom - winHeight < boardHeight)
      scaledRegion.css ("font-size",
                        (1.0 - (contentBottom - winHeight) / boardHeight) +
                        "em");
  }
  else
  {
    /* Give the extra space to the message box */
    mb.css ("height", (mb.height () + (winHeight - contentBottom)) + "px");
  }

  /* Work out the scale from pixels to ems so that we can translate
   * mouse positions to offsets in ems */
  var dummyElem = document.createElement ("div");
  dummyElem.style.width = "100em";
  dummyElem.style.height = "100em";
  dummyElem.style.position = "absolute";
  scaledRegion.append (dummyElem);
  this.pixelsPerEm = $(dummyElem).innerWidth () / 100.0;
  $(dummyElem).remove ();
};

ChatSession.prototype.canTurn = function ()
{
  if (this.shoutingPlayer != null)
    return false;

  if (this.tiles.length >= this.totalNumTiles)
    return false;

  if (this.state != "in-progress")
    return false;

  if (this.tiles.length == 0)
    return true;

  var player = this.getPlayer (this.personNumber);

  return (player.flags & PLAYER_NEXT_TURN) != 0;
};

ChatSession.prototype.updateTurnButton = function ()
{
  if (this.canTurn ())
    $("#turn-button").removeAttr ("disabled");
  else
    $("#turn-button").attr ("disabled", "disabled");
};

ChatSession.prototype.updateShoutButton = function ()
{
  if (this.state == "in-progress" && this.shoutingPlayer == null)
    $("#shout-button").removeAttr ("disabled");
  else
    $("#shout-button").attr ("disabled", "disabled");

  this.updateTurnButton ();
};

ChatSession.prototype.updateRemainingTiles = function ()
{
  $("#num-tiles").text (this.totalNumTiles - this.tiles.length);
};

ChatSession.prototype.setState = function (state)
{
  var controls = [ "#message-input-box", "#submit-message" ];
  var i;

  this.state = state;

  $("#connecting-message").css('display',
                               state == "connecting" ? "block" : "none");

  if (state == "in-progress")
  {
    for (i = 0; i < controls.length; i++)
      $(controls[i]).removeAttr ("disabled");
  }
  else
  {
    for (i = 0; i < controls.length; i++)
      $(controls[i]).attr ("disabled", "disabled");
  }

  this.updateShoutButton ();
};

ChatSession.prototype.setError = function (msg)
{
  var i;

  if (!msg)
    msg = "@ERROR_OCCURRED@";

  this.disconnect ();

  this.setState ("error");

  /* Firefox seems to inexplicably add the disabled attribute to the
   * button so this is a workaround to remove it */
  $("#error-button").removeAttr ("disabled");

  $("#error-note-text").text (msg);

  $("#error-note").show ();

  /* Throw an exception so that we won't continue processing messages
   * or restart the check data interval */
  throw msg;
};

ChatSession.prototype.getGameLengthForNTiles = function (n_tiles)
{
  if (Math.abs (n_tiles - SHORT_GAME_N_TILES) <
      Math.abs (n_tiles - NORMAL_GAME_N_TILES))
    return "short";
  else
    return "normal";
};

ChatSession.prototype.updatePlayerClass = function (player)
{
  var className = "player";
  if ((player.flags & PLAYER_TYPING))
    className += " typing"
  if ((player.flags & PLAYER_CONNECTED) == 0)
    className += " disconnected";
  if (this.shoutingPlayer == player)
    className += " shouting";
  if ((player.flags & PLAYER_NEXT_TURN))
    className += " next-turn";

  player.element.className = className;
};

ChatSession.prototype.addPlayerNote = function (note, playerName)
{
  var div = $(document.createElement ("div"));
  div.addClass ("message-note");

  note = note.replace ("%%", playerName);

  div.append ($(document.createTextNode (note)));

  this.addMessageDiv (div);
};

ChatSession.prototype.playSound = function (name)
{
  if (!this.soundOn)
    return;

  var sound = document.getElementById (name);
  if (sound && sound.play && sound.readyState > 0)
  {
    sound.currentTime = 0;
    sound.play ();
  }
};

ChatSession.prototype.soundToggleClickCb = function ()
{
  this.soundOn = !this.soundOn;

  if (this.soundOn)
  {
    $("#sound-on").show ();
    $("#sound-off").hide ();
  }
  else
  {
    $("#sound-on").hide ();
    $("#sound-off").show ();

    /* Stop any running sounds */
    $("audio").each (function (a) {
      if (this.pause)
        this.pause ();
    });
  }
};

ChatSession.prototype.inviteButtonCb = function ()
{
  if (this.syncReceived)
    $("#invite-overlay").show ();
};

ChatSession.prototype.closeInviteOverlayCb = function (event)
{
  if (event.target.id == "invite-link" ||
      event.target.id == "invite-qr-link" ||
      event.target.id == "invite-qr-image")
    return;

  $("#invite-overlay").hide ();
};

ChatSession.prototype.addMessageDiv = function (div)
{
  $("#messages").append (div);

  var messagesBox = $("#messages-box");
  messagesBox.scrollTop (messagesBox.prop("scrollHeight"));

  if (document.hasFocus && !document.hasFocus ())
  {
    this.unreadMessages++;
    document.title = "(" + this.unreadMessages + ") @TITLE@";
    this.playSound ("message-alert-sound");
  }
};

ChatSession.prototype.raiseTile = function (tile)
{
  /* Reappend the tile to its parent so that it will appear above all
   * other tiles */
  var parent = tile.element.parentNode;
  $(tile.element).remove ();
  $(parent).append (tile.element);
};

ChatSession.prototype.moveTile = function (tile, x, y, animate)
{
  var te = $(tile.element);
  var dx = tile.x - x;
  var dy = tile.y - y;
  var distance = Math.sqrt (dx * dx + dy * dy);
  var new_x = (x / 10.0) + "em";
  var new_y = (y / 10.0) + "em";

  te.stop ();
  this.raiseTile (tile);

  if (animate)
  {
    te.animate ({ "left": new_x, "top": new_y },
                distance * 2.0);
  }
  else
  {
    te.css ("left", new_x);
    te.css ("top", new_y);
  }
};

ChatSession.prototype.animateTile = function (tile, x, y)
{
  this.moveTile (tile, x, y, true /* animate */);
};

ChatSession.prototype.stopShout = function ()
{
  if (this.shoutingPlayer)
  {
    var player = this.shoutingPlayer;
    clearTimeout (this.shoutTimeout);
    this.shoutTimeout = null;
    this.shoutingPlayer = null;
    this.updateShoutButton ();
    this.updatePlayerClass (player);
    $("#shout-message").hide ();
  }
};

ChatSession.prototype.sendCurrentMessage = function ()
{
  var message;

  if (!this.connected)
    return;

  message = $("#message-input-box").val ();

  if (message.length > 0)
  {
    $("#message-input-box").val ("");
    this.sendMessage (0x85, 's', message);
    /* The server assumes we've stopped typing whenever a message is
     * sent */
    this.sentTypingState = false;
  }
};

ChatSession.prototype.focusMessageBox = function ()
{
  $("#message-input-box")[0].focus ();
};

ChatSession.prototype.shoutButtonClickCb = function ()
{
  this.shout ();
  this.focusMessageBox ();
};

ChatSession.prototype.shout = function ()
{
  if (this.shoutingPlayer)
    return;

  if (this.state != "in-progress")
    return;

  this.sendMessage (0x8a, '');
};

ChatSession.prototype.turnButtonClickCb = function ()
{
  this.turn ();
  $("#message-input-box")[0].focus ();
};

ChatSession.prototype.turn = function ()
{
  if (this.canTurn ())
    this.sendMessage (0x89, '');
};

ChatSession.prototype.submitMessageClickCb = function ()
{
  this.sendCurrentMessage ();
  this.focusMessageBox ();
};

ChatSession.prototype.messageKeyDownCb = function (event)
{
  if ((event.which == 10 || event.which == 13) &&
      $("#message-input-box").val ().length > 0)
  {
    event.preventDefault ();
    event.stopPropagation ();
    this.sendCurrentMessage ();
  }
};

ChatSession.prototype.documentKeyDownCb = function (event)
{
  if ($("#message-input-box").val ().length == 0)
  {
    if (event.which == 10 || event.which == 13)
    {
      event.preventDefault ();
      event.stopPropagation ();
      this.shout ();
      this.focusMessageBox ();
    }
    else if (event.which == 32)
    {
      event.preventDefault ();
      event.stopPropagation ();
      this.turn ();
      this.focusMessageBox ();
    }
  }
};

ChatSession.prototype.inputCb = function (event)
{
  var newTypingState = $("#message-input-box").val ().length > 0;

  if (newTypingState == this.sentTypingState)
    return;

  if (this.state != "in-progress")
    return;

  this.sentTypingState = newTypingState;

  this.sendMessage(newTypingState ? 0x86 : 0x87, '');
};

ChatSession.prototype.errorButtonClickCb = function ()
{
  if (this.conversationId != null)
  {
    /* Remove the search part */
    window.location = (window.location.protocol + "//" +
                       window.location.host +
                       window.location.pathname);
  }
  else
  {
    window.location.reload ();
  }
};

ChatSession.prototype.focusCb = function ()
{
  this.unreadMessages = 0;
  document.title = "@TITLE@";
};

ChatSession.prototype.getTileNumForTarget = function (target)
{
  if ((target.className) != "tile")
    return null;

  var tileNum;

  for (tileNum = 0; tileNum < this.tiles.length; tileNum++)
  {
    var tile = this.tiles[tileNum];
    if (tile && tile.element == target)
      return tileNum;
  }

  return null;
};

ChatSession.prototype.clearDirtyTilesTimeout = function ()
{
  if (this.dirtyTilesTimeout)
    {
      clearTimeout (this.dirtyTilesTimeout);
      this.dirtyTilesTimeout = null;
    }
};

ChatSession.prototype.trySendTiles = function ()
{
  this.clearDirtyTilesTimeout ();

  if (this.state != "in-progress")
    return;

  if (this.dirtyTiles.size <= 0)
    return;

  /* Only send tile updates when no other data is queued on the socket
   * to avoid flooding it.
   */
  if (this.sock.bufferedAmount > 0)
    {
      /* If there is already some data queued then try again an eighth
       * of a second. */
      if (this.dirtyTilesTimeout == null)
        {
          function callback ()
          {
            this.dirtyTilesTimeout = null;
            this.trySendTiles ();
          }

          this.dirtyTilesTimeout = setTimeout (callback.bind (this), 125);
        }
    }
  else
    {
      function sendTile (tileNum)
      {
        var tile = this.tiles[tileNum];
        this.sendMessage (0x88, 'Cww', tileNum, tile.x, tile.y);
      }

      this.dirtyTiles.forEach (sendTile.bind (this));
      this.dirtyTiles.clear ();
    }
};

ChatSession.prototype.dirtyTile = function (tileNum)
{
  this.dirtyTiles.add (tileNum);
  this.trySendTiles ();
};

ChatSession.prototype.dragStart = function (target, pageX, pageY)
{
  var tileNum = this.getTileNumForTarget (target);
  if (tileNum == null)
    return;

  if (this.state != "in-progress")
    return;

  /* Don't allow the player to start dragging if someone else is
   * shouting */
  if (this.shoutingPlayer &&
      this.shoutingPlayer != this.getPlayer (this.personNumber))
    return;

  var tile = this.tiles[tileNum];

  var position = $(tile.element).position ();
  this.dragOffsetX = pageX - position.left;
  this.dragOffsetY = pageY - position.top;

  this.dragTile = tileNum;
  this.tileMoved = false;

  $(tile.element).stop ();
  this.raiseTile (tile);
};

ChatSession.prototype.dragMove = function (pageX, pageY)
{
  if (this.state != "in-progress")
    return;

  var newX = Math.round ((pageX - this.dragOffsetX) /
                         this.pixelsPerEm * 10.0);
  var newY = Math.round ((pageY - this.dragOffsetY) /
                         this.pixelsPerEm * 10.0);
  var tile = this.tiles[this.dragTile];

  /* Gives some resistance to the initial move of the tile */
  if (!this.tileMoved &&
      Math.abs (newX - tile.x) < 3 &&
      Math.abs (newY - tile.y) < 3)
    return;

  if (newX == tile.y && newY == tile.y)
    return;

  this.tileMoved = true;
  this.lastMovedTile = tile;

  tile.x = newX;
  tile.y = newY;

  this.dirtyTile (this.dragTile);

  tile.element.style.left = (newX / 10.0) + "em";
  tile.element.style.top = (newY / 10.0) + "em";
};

ChatSession.prototype.dragCancel = function ()
{
  this.dragTile = null;
};

ChatSession.prototype.dragEnd = function ()
{
  var tile = this.tiles[this.dragTile];

  if (this.state == "in-progress" &&
      !this.tileMoved &&
      this.lastMovedTile &&
      this.lastMovedTile != tile)
  {
    var newPos = this.lastMovedTile.x + TILE_SIZE;
    var maxPos = ($("#board").innerWidth () / this.pixelsPerEm *
                  10.0 - TILE_SIZE);
    if (newPos < maxPos)
    {
      this.animateTile (tile, newPos, this.lastMovedTile.y);
      tile.x = newPos;
      tile.y = this.lastMovedTile.y;
      this.dirtyTile (this.dragTile);
      this.lastMovedTile = tile;
    }
  }

  this.dragTile = null;
};

ChatSession.prototype.mouseDownCb = function (event)
{
  if (event.which != 1 || this.dragTile != null)
    return;

  event.preventDefault ();

  this.dragStart (event.target, event.pageX, event.pageY);
};

ChatSession.prototype.mouseMoveCb = function (event)
{
  if (this.dragTile == null || this.dragTouchId != null)
    return;

  event.preventDefault ();

  this.dragMove (event.pageX, event.pageY);
};

ChatSession.prototype.mouseUpCb = function (event)
{
  if (event.which != 1 || this.dragTile == null || this.dragTouchId != null)
    return;

  event.preventDefault ();

  this.dragEnd ();
};

ChatSession.prototype.touchStartCb = function (event)
{
  event.preventDefault ();

  if (this.dragTile != null)
    return;

  var touch = event.originalEvent.changedTouches[0];
  this.dragStart (touch.target, touch.pageX, touch.pageY);
  if (this.dragTile != null)
    this.dragTouchId = touch.identifier;
};

ChatSession.prototype.getDragTouch = function (event)
{
  var touches = event.originalEvent.changedTouches;

  for (i = 0; i < touches.length; i++)
  {
    if (touches[i].identifier == this.dragTouchId)
      return touches[i];
  }

  return null;
};

ChatSession.prototype.touchMoveCb = function (event)
{
  event.preventDefault ();

  var touch = this.getDragTouch (event);

  if (touch)
    this.dragMove (touch.pageX, touch.pageY);
};

ChatSession.prototype.touchEndCb = function (event)
{
  event.preventDefault ();

  var touch = this.getDragTouch (event);

  if (touch)
  {
    this.dragEnd ();
    this.dragTouchId = null;
  }
};

ChatSession.prototype.touchCancelCb = function (event)
{
  event.preventDefault ();

  var touch = this.getDragTouch (event);

  if (touch)
  {
    this.dragCancel ();
    this.dragTouchId = null;
  }
};

ChatSession.prototype.gameLengthChangeCb = function (event)
{
  if (this.state != "in-progress")
    return;

  var length = this.getGameLengthForNTiles (this.totalNumTiles);
  var lengthElem = $("#game-length");

  if (length != lengthElem.val ())
  {
    if (lengthElem.val () == "short")
      length = SHORT_GAME_N_TILES;
    else
      length = NORMAL_GAME_N_TILES;

    this.sendMessage (0x8b, 'C', length);
  }
};

ChatSession.prototype.padNumber = function (num)
{
  num = "" + num;

  return num.length == 1 ? "0" + num : num;
};

ChatSession.prototype.updateNextGameTime = function ()
{
  var link = $("#game-time-url");

  if (link.length <= 0)
    return;

  var GAME_DAY = 1; /* Mondays */
  var GAME_TIME = 19; /* 7pm UTC */
  var now = new Date ();

  var daysAdd = GAME_DAY - now.getUTCDay ();

  if (daysAdd < 0 ||
      (daysAdd == 0 && now.getUTCHours () >= GAME_TIME + 1))
    daysAdd += 7;

  var gameDay = new Date (now.getTime () + daysAdd * 24 * 60 * 60 * 1000);
  var gameTime = new Date (Date.UTC (gameDay.getUTCFullYear (),
                                     gameDay.getUTCMonth (),
                                     gameDay.getUTCDate (),
                                     GAME_TIME,
                                     0, /* hours */
                                     0 /* seconds */));

  var url = ("http://www.timeanddate.com/worldclock/fixedtime.html?iso=" +
             gameTime.getUTCFullYear () +
             this.padNumber (gameTime.getUTCMonth () + 1) +
             this.padNumber (gameTime.getUTCDate ()) +
             "T" + this.padNumber (GAME_TIME) +
             "&ah=1" +
             "&msg=" + encodeURI("@TITLE@"));

  link.attr ("href", url);
};

ChatSession.prototype.start = function ()
{
  if (!this.connected)
    {
      this.reconnectCount = 0;
      this.doConnect ();
    }

  this.setState ("connecting");

  $("#shout-button").bind ("click", this.shoutButtonClickCb.bind (this));
  $("#turn-button").bind ("click", this.turnButtonClickCb.bind (this));
  $("#submit-message").bind ("click", this.submitMessageClickCb.bind (this));
  $("#message-input-box").bind ("keydown", this.messageKeyDownCb.bind (this));
  $("#message-input-box").bind ("input", this.inputCb.bind (this));
  $("#error-button").bind ("click", this.errorButtonClickCb.bind (this));
  /* Prevent default handling of mouse events on the board because
   * otherwise you can accidentally select the text in a tile */
  var preventDefaultCb = function (event) { event.preventDefault (); };
  $("#board").click (preventDefaultCb);

  $("#board").mousedown (this.mouseDownCb.bind (this));
  $("#board").mousemove (this.mouseMoveCb.bind (this));
  $("#board").mouseup (this.mouseUpCb.bind (this));

  $("#board").bind ("touchstart", this.touchStartCb.bind (this));
  $("#board").bind ("touchend", this.touchEndCb.bind (this));
  $("#board").bind ("touchleave", this.touchEndCb.bind (this));
  $("#board").bind ("touchcancel", this.touchCancelCb.bind (this));
  $("#board").bind ("touchmove", this.touchMoveCb.bind (this));

  $("#sound-toggle").bind ("click", this.soundToggleClickCb.bind (this));

  $("#invite-button").bind ("click", this.inviteButtonCb.bind (this));
  $("#invite-overlay").mousedown (this.closeInviteOverlayCb.bind (this));

  $("#game-length").change (this.gameLengthChangeCb.bind (this));

  $(document).keydown (this.documentKeyDownCb.bind (this));

  $(window).focus (this.focusCb.bind (this));

  $(window).unload (this.unloadCb.bind (this));

  this.updateRemainingTiles ();
  this.updateNextGameTime ();

  $("#start-note").show ();
};

ChatSession.prototype.stringToUtf8 = function (s)
{
  s = encodeURIComponent (s);

  var length = 0;
  var i;

  for (i = 0; i < s.length; i++)
    {
      if (s[i] == '%')
        i += 2;
      length++;
    }

  var ba = new Uint8Array (length);
  var p = 0;

  for (i = 0; i < s.length; i++)
    {
      if (s[i] == '%')
        {
          ba[p++] = parseInt ("0x" + s.substring (i + 1, i + 3));
          i += 2;
        }
      else
        {
          ba[p++] = s.charCodeAt (i);
        }
    }

  return ba;
};

ChatSession.prototype.clearKeepAliveTimeout = function ()
{
  if (this.keepAliveTimeout)
    {
      clearTimeout (this.keepAliveTimeout);
      this.keepAliveTimeout = null;
    }
};

ChatSession.prototype.resetKeepAliveTimeout = function ()
{
  this.clearKeepAliveTimeout ();

  function callback ()
  {
    this.keepAliveTimeout = null;
    if (this.sock)
      this.sendMessage (0x83, "");
  }

  this.keepAliveTimeout = setTimeout (callback.bind (this), 60 * 1000);
};

ChatSession.prototype.addSocketHandler = function (event, func)
{
  this.sockHandlers.push (event, func);
  this.sock.addEventListener (event, func);
}

ChatSession.prototype.removeSocketHandlers = function (event, func)
{
  var i;

  for (i = 0; i < this.sockHandlers.length; i += 2)
    {
      this.sock.removeEventListener (this.sockHandlers[i],
                                     this.sockHandlers[i + 1]);
    }

  this.sockHandlers = [];
}

ChatSession.prototype.doConnect = function ()
{
  var location = window.location;

  console.log ("Connecting…");

  var protocol, port;

  if (location.protocol.toLowerCase ().startsWith ("https"))
    {
      protocol = "wss";
      port = 5145;
    }
  else
    {
      protocol = "ws";
      port = 5144;
    }

  this.sock = new WebSocket (protocol + "://" +
                             location.hostname + ":" +
                             port + "/");
  this.sock.binaryType = 'arraybuffer';
  this.addSocketHandler ("error", this.sockErrorCb.bind (this));
  this.addSocketHandler ("close", this.sockErrorCb.bind (this));
  this.addSocketHandler ("open", this.sockOpenCb.bind (this));
  this.addSocketHandler ("message", this.messageCb.bind (this));
};

ChatSession.prototype.reconnectTimeoutCb = function ()
{
  this.reconnectTimeout = null;
  this.doConnect ();
};

ChatSession.prototype.disconnect = function ()
{
  if (this.reconnectTimeout != null)
    {
      clearTimeout (this.reconnectTimeout);
      this.reconnectTimeout = null;
    }

  if (this.sock == null)
    return;

  this.removeSocketHandlers ();
  this.clearKeepAliveTimeout ();
  this.clearDirtyTilesTimeout ();
  this.sock.close ();
  this.sock = null;
  this.connected = false;
};

ChatSession.prototype.sockErrorCb = function (e)
{
  console.log ("Error on socket: " + e);
  this.disconnect ();

  if (++this.reconnectCount >= 20)
    {
      this.setError ();
    }
  else
    {
      this.setState ("connecting");

      if (this.reconnectTimeout == null)
        {
          this.reconnectTimeout =
            setTimeout (this.reconnectTimeoutCb.bind (this),
                        15000);
        }
    }
};

ChatSession.ARG_SIZES =
  {
    "B": 8,
    "C": 1,
    "W": 2,
    "w": 2,
  };

ChatSession.prototype.sendMessage = function (msgType, argTypes)
{
  var msgSize = 1;
  var i;
  var stringArgs = null;

  for (i = 0; i < argTypes.length; i++)
    {
      var ch = argTypes.charAt (i);
      if (ch == "s")
        {
          if (stringArgs == null)
            stringArgs = [];
          stringArgs.push (this.stringToUtf8 (arguments[i + 2]));
          msgSize += stringArgs[stringArgs.length - 1].length + 1;
        }
      else
        {
          msgSize += ChatSession.ARG_SIZES[ch];
        }
    }

  var ab = new ArrayBuffer (msgSize);
  var dv = new DataView (ab);

  dv.setUint8 (0, msgType);

  var pos = 1;
  var stringArg = 0;

  for (i = 0; i < argTypes.length; i++)
    {
      var arg = arguments[i + 2];
      var t = argTypes.charAt (i);

      if (t == 'B')
        {
          var j;
          for (j = 0; j < 8; j++)
            dv.setUint8 (pos++, arg[j]);
        }
      else if (t == 'C')
        {
          dv.setUint8 (pos++, arg, true);
        }
      else if (t == 'W')
        {
          dv.setUint16 (pos, arg, true);
          pos += 2;
        }
      else if (t == 'w')
        {
          dv.setInt16 (pos, arg, true);
          pos += 2;
        }
      else if (t == 's')
        {
          arg = stringArgs[stringArg++];
          var j;
          for (j = 0; j < arg.length; j++)
            dv.setUint8 (pos++, arg[j]);
          dv.setUint8 (pos++, 0);
        }
    }

  this.sock.send (ab);

  this.resetKeepAliveTimeout ();
};

ChatSession.prototype.sockOpenCb = function (e)
{
  console.log ("connected!");

  this.connected = true;

  if (this.personId != null)
    this.sendMessage (0x81, "BW", this.personId, this.numMessagesReceived);
  else if (this.conversationId != null)
    this.sendMessage (0x8d, "Bs", this.conversationId, this.playerName);
  else
    this.sendMessage (0x80, "ss", this.roomName, this.playerName);
};

ChatSession.prototype.handlePlayerId = function (mr)
{
  this.personId = mr.getUint64 ();
  this.personNumber = mr.getUint8 ();

  /* If we get a player ID then we can assume the connection was worked */
  this.reconnectCount = 0;

  this.setState ("in-progress");
};

ChatSession.prototype.handleMessage = function (mr)
{
  var person = mr.getUint8 ();
  var text = mr.getString ();

  var div = $(document.createElement ("div"));
  div.addClass ("message");

  var span = $(document.createElement ("span"));
  div.append (span);
  span.text (this.getPlayer (person).name);
  span.addClass (person == this.personNumber
                 ? "message-you" : "message-stranger");

  div.append ($(document.createTextNode (" " + text)));

  this.addMessageDiv (div);

  this.messageNumber++;
};

ChatSession.prototype.handleNTiles = function (mr)
{
  var nTiles = mr.getUint8 ();

  $("#game-length").val (this.getGameLengthForNTiles (nTiles));
  this.totalNumTiles = nTiles;
  this.updateRemainingTiles ();
};

ChatSession.prototype.handleTile = function (mr)
{
  var tileNum = mr.getUint8 ();
  var tileX = mr.getInt16 ();
  var tileY = mr.getInt16 ();
  var letter = mr.getString ();
  var playerNum = mr.getUint8 ();

  var tile = this.tiles[tileNum];

  if (tile == null)
  {
    tile = this.tiles[tileNum] = {};
    tile.element = document.createElement ("div");
    tile.element.className = "tile";
    tile.element.style.left = (-TILE_SIZE / 10.0) + "em";
    tile.element.style.top = (-TILE_SIZE / 10.0) + "em";
    $(tile.element).text (letter);
    tile.x = tile.y = -TILE_SIZE;
    $("#board").append (tile.element);

    this.updateRemainingTiles ();
    this.updateTurnButton ();

    $("#start-note").remove ();

    if (this.syncReceived)
      this.playSound ("turn-sound");
  }

  if (playerNum != this.personNumber &&
      (tileX != tile.x || tileY != tile.y))
    this.moveTile (tile, tileX, tileY, this.syncReceived);

  tile.x = tileX;
  tile.y = tileY;
};

ChatSession.prototype.handlePlayerName = function (mr)
{
  var playerNum = mr.getUint8 ();
  var name = mr.getString ();

  var player = this.getPlayer (playerNum);
  player.name = name;
  $(player.element).text (player.name);
};

ChatSession.prototype.handlePlayer = function (mr)
{
  var playerNum = mr.getUint8 ();
  var flags = mr.getUint8 ();

  var player = this.getPlayer (playerNum);

  if (this.syncReceived &&
      ((flags ^ player.flags) & PLAYER_CONNECTED) != 0)
  {
    this.addPlayerNote ((flags & PLAYER_CONNECTED) == 0 ?
                        "@PLAYER_LEFT@" :
                        "@PLAYER_JOINED@",
                        player.name);
  }

  player.flags = flags;

  this.updatePlayerClass (player);
  this.updateTurnButton ();
};

ChatSession.prototype.handlePlayerShouted = function (mr)
{
  var playerNum = mr.getUint8 ();

  this.stopShout ();

  this.shoutingPlayer = this.getPlayer (playerNum);

  this.updatePlayerClass (this.shoutingPlayer);
  this.updateShoutButton ();

  var sm = $("#shout-message");
  sm.show ();
  sm.text (this.shoutingPlayer.name);
  sm.fadeOut (3000);

  this.shoutTimeout = setTimeout (this.stopShout.bind (this),
                                  SHOUT_TIME);

  this.playSound ("shout-sound");
};

ChatSession.prototype.handleSync = function (mr)
{
  this.syncReceived = true;
};

ChatSession.prototype.handleEnd = function (mr)
{
  /* This should only happen if we've left the conversation, but that
   * shouldn't be possible without leaving the page so something has
   * gone wrong */
  this.setError ();
};

ChatSession.prototype.handleBadPlayerId = function (mr)
{
  this.setError ();
};

ChatSession.prototype.handleBadConversationId = function (mr)
{
  this.setError ("@INVITE_LINK_NOT_VALID@");
};

ChatSession.prototype.handleConversationFull = function (mr)
{
  this.setError ("@CONVERSATION_FULL@");
};

ChatSession.prototype.handleConversationId = function (mr)
{
  var id = this.encodeId (mr.getUint64 ());
  var webLink = (window.location.protocol + "//" +
                 window.location.host +
                 window.location.pathname +
                 "?" + id);
  var appLink = (window.location.protocol + "//" +
                 window.location.host +
                 "/j/" +
                 id);
  var qrImg = "/cgi-bin/invite-cgi?" + id;

  $("#invite-link").attr ("href", webLink);
  $("#invite-link").text (webLink);
  $("#invite-qr-link").attr ("href", appLink);
  $("#invite-qr-image").attr ("src", qrImg);
};

ChatSession.prototype.messageCb = function (e)
{
  var mr = new MessageReader (new DataView (e.data));
  var msgType = mr.getUint8 ();

  if (msgType == 0)
    this.handlePlayerId (mr);
  else if (msgType == 0x01)
    this.handleMessage (mr);
  else if (msgType == 0x02)
    this.handleNTiles (mr);
  else if (msgType == 0x03)
    this.handleTile (mr);
  else if (msgType == 0x04)
    this.handlePlayerName (mr);
  else if (msgType == 0x05)
    this.handlePlayer (mr);
  else if (msgType == 0x06)
    this.handlePlayerShouted (mr);
  else if (msgType == 0x07)
    this.handleSync (mr);
  else if (msgType == 0x08)
    this.handleEnd (mr);
  else if (msgType == 0x09)
    this.handleBadPlayerId (mr);
  else if (msgType == 0x0a)
    this.handleConversationId (mr);
  else if (msgType == 0x0b)
    this.handleBadConversationId (mr);
  else if (msgType == 0x0d)
    this.handleConversationFull (mr);
};

ChatSession.prototype.unloadCb = function ()
{
  /* Try to let the server know the player is going. */
  if (this.connected)
  {
    this.sendMessage (0x84, "");
    this.disconnect ();
  }
};

ChatSession.prototype.getPlayer = function (playerNum)
{
  var player;

  if (!(player = this.players[playerNum]))
  {
    player = this.players[playerNum] = {};

    player.flags = 0;
    player.name = "";

    player.element = document.getElementById ("player-" + playerNum);
    if (player.element == null)
    {
      player.element = document.createElement ("span");
      $("#other-players").append (player.element);
      $("#other-players").show ();
    }
  }

  return player;
};

ChatSession.prototype.encodeId = function (id)
{
  var str = "";
  var i;

  /* Change the order of the bytes in the ID to big-endian and convert
   * it to a “binary string”.
   */
  for (i = 7; i >= 1; i--)
    str += String.fromCharCode (id[i]);
  /* Add 2 bits of zeroes before the last 4 bits so that the last 4
   * bits will be in the lower bits of the second-to-last character.
   */
  str += (String.fromCharCode ((id[0] & 0xf0) | ((id[0] & 0x0c) >> 2)) +
          String.fromCharCode ((id[0] & 0x03) << 6));

  /* base64 encode it */
  var b64 = btoa (str);

  /* Replace the + and / with URL-friendly versions */
  var urlFriendly = b64.replace (/[+/]/g,
                                function (ch) {
                                  return ch == "+" ? "-" : "_";
                                });
  /* Remove the last character */
  return urlFriendly.slice (0, -1);
};

ChatSession.prototype.decodeId = function (encoded)
{
  if (encoded.length != 11)
    return null;
  if (encoded.match (/[^a-zA-Z0-9_-]/) != null)
    return null;

  var unfriendly = encoded.replace (/[_-]/g,
                                    function (ch) {
                                      return ch == "_" ? "/" : "+";
                                    });

  var bstr = atob (unfriendly + "A");
  var id = new Uint8Array (8);

  for (var i = 0; i < 7; i++)
    id[7 - i] = bstr.charCodeAt (i);

  var a = bstr.charCodeAt (7);
  var b = bstr.charCodeAt (8);

  id[0] = (a & 0xf0) | ((a & 3) << 2) | (b >> 6);

  return id;
}

/* .bind is only implemented in recent browsers so this provides a
 * fallback if it's not available. Verda Ŝtelo only ever uses it bind the
 * 'this' context so it doesn't bother with any other arguments */
if (!Function.prototype.bind)
  {
    Function.prototype.bind = function (obj)
    {
      var originalFunc = this;
      return function () {
        return originalFunc.apply (obj, [].slice.call (arguments));
      };
    };
  }

(function ()
{
  var cs;

  function updatePlayButton ()
  {
    $("#playbutton")[0].disabled = ($("#namebox").val ().match (/\S/) == null);
  }

  function start ()
  {
    cs.playerName = $("#namebox").val ();
    $("#welcome-overlay").remove ();
    cs.start ();
  }

  function keyDownCb (event)
  {
    if ((event.which == 10 || event.which == 13) &&
        !$("#playbutton")[0].disabled)
      start ();
  }

  function inputCb ()
  {
    updatePlayButton ();
  }

  function loadCb ()
  {
    cs = new ChatSession ();

    $("#namebox").bind ("keydown", keyDownCb);
    $("#namebox").bind ("input", inputCb);
    $("#namebox").bind ("propertychange", inputCb);
    $("#namebox")[0].focus ();
    $("#playbutton").click (start);

    updatePlayButton ();
  }

  $(window).load (loadCb);
}) ();
