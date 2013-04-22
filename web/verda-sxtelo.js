/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011, 2012, 2013  Neil Roberts
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

var KEEP_ALIVE_TIME = 2.5 * 60 * 1000;
var SHOUT_TIME = 10 * 1000;
var CONNECT_RETRY_TIME = 5 * 1000;
var TILE_SIZE = 20;
var N_TILES = 122;

var PLAYER_CONNECTED = (1 << 0);
var PLAYER_TYPING = (1 << 1);

function getAjaxObject ()
{
  /* On IE we'll use XDomainRequest but make it look like an
   * XMLHttpRequest object */
  if (window["XDomainRequest"])
    {
      /** constructor */
      function Wrapper () { }
      var obj = new Wrapper ();
      obj.xdr = new XDomainRequest ();

      obj.xdr.onprogress = function ()
        {
          obj.readyState = 3;
          obj.responseText = obj.xdr.responseText;
          if (obj.onreadystatechange)
            obj.onreadystatechange ();
        };
      obj.xdr.onload = function ()
        {
          obj.status = 200;
          obj.readyState = 4;
          obj.responseText = obj.xdr.responseText;
          if (obj.onreadystatechange)
            obj.onreadystatechange ();
        };
      obj.xdr.onerror = function ()
        {
          obj.status = 300;
          obj.readyState = 4;
          if (obj.onreadystatechange)
            obj.onreadystatechange ();
        }
      obj.xdr.ontimeout = obj.xdr.onerror;

      obj.setRequestHeader = function () { };

      /** @this {Wrapper} */
      obj.abort = function () { this.xdr.abort (); };
      /** @this {Wrapper} */
      obj.open = function () { this.xdr.open.apply (this.xdr, arguments); };
      /** @this {Wrapper} */
      obj.send = function () { this.xdr.send.apply (this.xdr, arguments); };

      return obj;
    }
  else
    return new XMLHttpRequest ();
}

/**@constructor*/
function ChatSession (playerName)
{
  this.terminatorRegexp = /\r\n/;
  this.personId = null;
  this.personNumber = null;
  this.messageNumber = 0;
  this.messageQueue = [];
  this.sentTypingState = false;
  this.unreadMessages = 0;
  this.players = [];
  this.tiles = [];
  this.dragTile = null;
  this.lastMovedTile = null;
  this.retryCount = 0;
  this.pendingTimeouts = [];

  this.playerName = playerName || "ludanto";

  var search = window.location.search;
  if (search && search.match (/^\?[a-z]+$/))
    this.roomName = search.substring (1);
  else
    this.roomName = "default";
}

ChatSession.prototype.updateShoutButton = function ()
{
  if (this.state == "in-progress" && this.shoutingPlayer == null)
    $("#shout-button").removeAttr ("disabled");
  else
    $("#shout-button").attr ("disabled", "disabled");
};

ChatSession.prototype.updateRemainingTiles = function ()
{
  $("#num-tiles").text (N_TILES - this.tiles.length);
};

ChatSession.prototype.setState = function (state)
{
  var controls = [ "#message-input-box", "#submit-message", "#turn-button" ];
  var i;

  this.state = state;

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

ChatSession.prototype.getUrl = function (method)
{
  var location = window.location;
  return "http://" + location.hostname + ":5142/" + method;
};

ChatSession.prototype.clearWatchAjax = function ()
{
  var watchAjax = this.watchAjax;
  /* Clear the Ajax object first incase aborting it fires a
   * readystatechange event */
  this.watchAjax = null;
  if (watchAjax)
    watchAjax.abort ();
};

ChatSession.prototype.setError = function (msg)
{
  var i;

  if (!msg)
    msg = "@ERROR_OCCURRED@";

  for (i = 0; i < this.pendingTimeouts.length; i++)
    clearTimeout (this.pendingTimeouts[i]);
  this.pendingTimeouts.splice (0, this.pendingTimeouts.length);

  this.clearWatchAjax ();
  this.clearCheckDataInterval ();

  $("#status-note").text (msg);
  this.setState ("error");

  /* Throw an exception so that we won't continue processing messages
   * or restart the check data interval */
  throw msg;
};

ChatSession.prototype.handleHeader = function (header)
{
  if (typeof (header) != "object")
    this.setError ("@BAD_DATA@");

  /* Ignore the header if we've already got further than this state */
  if (this.state != "connecting")
    return;

  /* If we've successfully got the header then we've succesfully
   * connected so we can reset the count */
  this.retryCount = 0;

  this.personNumber = header.num;
  this.personId = header.id;

  $("#status-note").text ("");
  this.setState ("in-progress");
};

ChatSession.prototype.handlePlayerName = function (data)
{
  if (typeof (data) != "object")
    this.setError ("@BAD_DATA@");

  if (this.state != "in-progress")
    return;

  var player = this.getPlayer (data.num);
  player.name = data.name;
  player.element.textContent = player.name;
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

  player.element.className = className;
};

ChatSession.prototype.handlePlayer = function (data)
{
  if (typeof (data) != "object")
    this.setError ("@BAD_DATA@");

  if (this.state != "in-progress")
    return;

  var player = this.getPlayer (data.num);
  player.flags = data.flags;

  this.updatePlayerClass (player);
};

ChatSession.prototype.handleEnd = function ()
{
  /* This should only happen if we've left the conversation, but that
   * shouldn't be possible without leaving the page so something has
   * gone wrong */
  this.setError ();
};

ChatSession.prototype.handleChatMessage = function (message)
{
  if (typeof (message) != "object")
    this.setError ("@BAD_DATA@");

  /* Ignore the message if we've already got further than this state */
  if (this.state != "in-progress")
    return;

  var div = $(document.createElement ("div"));
  div.addClass ("message");

  var span = $(document.createElement ("span"));
  div.append (span);
  span.text (this.getPlayer (message.person).name);
  span.addClass (message.person == this.personNumber
                 ? "message-you" : "message-stranger");

  div.append ($(document.createTextNode (" " + message.text)));

  $("#messages").append (div);

  var messagesBox = $("#messages-box");
  messagesBox.scrollTop (messagesBox.prop("scrollHeight"));

  if (document.hasFocus && !document.hasFocus ())
  {
    this.unreadMessages++;
    document.title = "(" + this.unreadMessages + ") Verda Ŝtelo";
    var messageAlertSound = document.getElementById ("message-alert-sound");
    if (messageAlertSound && messageAlertSound.play)
      messageAlertSound.play ();
  }

  this.messageNumber++;
};

ChatSession.prototype.raiseTile = function (tile)
{
  /* Reappend the tile to its parent so that it will appear above all
   * other tiles */
  var parent = tile.element.parentNode;
  $(tile.element).remove ();
  $(parent).append (tile.element);
};

ChatSession.prototype.handleTile = function (data)
{
  if (typeof (data) != "object")
    this.setError ("@BAD_DATA@");

  if (this.state != "in-progress")
    return;

  var tile = this.tiles[data.num];

  if (tile == null)
  {
    tile = this.tiles[data.num] = {};
    tile.element = document.createElement ("div");
    tile.element.className = "tile";
    tile.element.style.left = (-TILE_SIZE / 10.0) + "em";
    tile.element.style.top = (-TILE_SIZE / 10.0) + "em";
    tile.element.textContent = data.letter;
    tile.x = tile.y = -TILE_SIZE;
    $("#board").append (tile.element);

    this.updateRemainingTiles ();
  }

  if (data.num != this.dragTile &&
      (data.x != tile.x || data.y != tile.y))
  {
    var te = $(tile.element);
    var dx = tile.x - data.x;
    var dy = tile.y - data.y;
    var distance = Math.sqrt (dx * dx + dy * dy);
    var new_x = (data.x / 10.0) + "em";
    var new_y = (data.y / 10.0) + "em";

    te.stop ();
    this.raiseTile (tile);
    te.animate ({ "left": new_x, "top": new_y },
                distance * 2.0);
  }

  tile.x = data.x;
  tile.y = data.y;
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

ChatSession.prototype.handleShout = function (data)
{
  if (typeof (data) != "number")
    this.setError ("@BAD_DATA@");

  if (this.state != "in-progress")
    return;

  this.stopShout ();

  this.shoutingPlayer = this.getPlayer (data);

  this.updatePlayerClass (this.shoutingPlayer);
  this.updateShoutButton ();

  var sm = $("#shout-message");
  sm.show ();
  sm.text (this.shoutingPlayer.name);
  sm.fadeOut (3000);

  this.shoutTimeout = setTimeout (this.stopShout.bind (this),
                                  SHOUT_TIME);
};

ChatSession.prototype.processMessage = function (message)
{
  if (typeof (message) != "object"
      || typeof (message[0]) != "string")
    this.setError ("@BAD_DATA@");

  switch (message[0])
  {
  case "header":
    this.handleHeader (message[1]);
    break;

  case "end":
    this.handleEnd ();
    break;

  case "player-name":
    this.handlePlayerName (message[1]);
    break;

  case "player":
    this.handlePlayer (message[1]);
    break;

  case "message":
    this.handleChatMessage (message[1]);
    break;

  case "tile":
    this.handleTile (message[1]);
    break;

  case "shout":
    this.handleShout (message[1]);
    break;
  }
};

ChatSession.prototype.checkData = function ()
{
  var responseText = this.watchAjax.responseText;

  if (responseText)
    while (this.watchPosition < responseText.length)
    {
      var rest = responseText.slice (this.watchPosition);
      var terminatorPos = rest.search (this.terminatorRegexp);

      if (terminatorPos == -1)
        break;

      try
      {
        var message = eval ('(' + rest.slice (0, terminatorPos) + ')');
      }
      catch (e)
      {
        this.setError ("@BAD_DATA@");
        return;
      }

      this.processMessage (message);

      this.watchPosition += terminatorPos + 2;
    }
};

ChatSession.prototype.clearCheckDataInterval = function ()
{
  if (this.checkDataInterval)
  {
    clearInterval (this.checkDataInterval);
    this.checkDataInterval = null;
  }
};

ChatSession.prototype.resetCheckDataInterval = function ()
{
  this.clearCheckDataInterval ();
  this.checkDataInterval = setInterval (this.checkData.bind (this), 3000);
};

ChatSession.prototype.addTimeout = function (func, interval)
{
  /* This is a wrapper around window.setTimeout except that it adds
   * the timeoutID to a list so that we can cancel it if an error
   * occurs */
  var timeout;
  var wrapperFunc = (function () {
    var i;

    for (i = 0; i < this.pendingTimeouts.length; i++)
    {
      if (this.pendingTimeouts[i] == timeout)
      {
        this.pendingTimeouts.splice (i, 1);
        break;
      }
    }

    func ();
  }).bind (this);

  timeout = window.setTimeout (wrapperFunc, interval);
  this.pendingTimeouts.push (timeout);
}

ChatSession.prototype.watchReadyStateChangeCb = function ()
{
  if (!this.watchAjax)
    return;

  /* Every time the ready state changes we'll check for new data in
   * the response and reset the timer. That way browsers that call
   * onreadystatechange whenever new data arrives can get data as soon
   * as it comes it, but other browsers will still eventually get the
   * data from the timeout */

  if (this.watchAjax.readyState == 3)
  {
    this.checkData ();
    this.resetCheckDataInterval ();
  }
  else if (this.watchAjax.readyState == 4)
  {
    this.checkData ();
    this.watchAjax = null;
    this.clearCheckDataInterval ();

    if (this.retryCount++ < 10)
      this.addTimeout (this.startWatchAjax.bind (this), CONNECT_RETRY_TIME);
    else
      this.setError ();
  }
};

ChatSession.prototype.startWatchAjax = function ()
{
  var method;

  if (this.personId)
    method = "watch_person?" + this.personId + "&" + this.messageNumber;

  else
    method = ("new_person?" + encodeURIComponent (this.roomName) + "&" +
              encodeURIComponent (this.playerName));

  this.clearWatchAjax ();

  this.watchPosition = 0;

  this.watchAjax = getAjaxObject ();

  this.watchAjax.onreadystatechange = this.watchReadyStateChangeCb.bind (this);

  this.watchAjax.open ("GET", this.getUrl (method));
  this.watchAjax.send (null);

  this.resetCheckDataInterval ();
  this.resetKeepAlive ();
};

ChatSession.prototype.sendMessageReadyStateChangeCb = function ()
{
  if (!this.sendMessageAjax)
    return;

  if (this.sendMessageAjax.readyState == 4)
  {
    if (this.sendMessageAjax.status == 200)
    {
      this.sendMessageAjax = null;
      this.sendNextMessage ();
    }
    else
    {
      this.sendMessageAjax = null;
      this.setError ();
    }
  }
};

ChatSession.prototype.sendNextMessage = function ()
{
  if (this.sendMessageAjax)
    return;

  if (this.messageQueue.length < 1)
  {
    /* Check if we need to update the typing state */
    var newTypingState = $("#message-input-box").val ().length > 0;
    if (newTypingState != this.sentTypingState)
    {
      this.sentTypingState = newTypingState;

      this.sendMessageAjax = getAjaxObject ();
      this.sendMessageAjax.onreadystatechange =
        this.sendMessageReadyStateChangeCb.bind (this);
      this.sendMessageAjax.open ("GET",
                                 this.getUrl ((newTypingState ?
                                               "start_typing?" :
                                               "stop_typing?") +
                                              this.personId));
      this.sendMessageAjax.setRequestHeader ("Content-Type",
                                             "text/plain; charset=UTF-8");
      this.sendMessageAjax.send (message);

      this.resetKeepAlive ();
    }
    else if ((this.state == "in-progress" ||
              this.state == "awaiting-partner") &&
             ((new Date ()).getTime () -
              this.keepAliveTime.getTime () >=
              KEEP_ALIVE_TIME))
    {
      this.sendMessageAjax = getAjaxObject ();
      this.sendMessageAjax.onreadystatechange =
        this.sendMessageReadyStateChangeCb.bind (this);
      this.sendMessageAjax.open ("GET",
                                 this.getUrl ("keep_alive?" + this.personId));
      this.sendMessageAjax.setRequestHeader ("Content-Type",
                                             "text/plain; charset=UTF-8");
      this.sendMessageAjax.send (message);

      this.resetKeepAlive ();
    }

    return;
  }

  var message = this.messageQueue.shift ();

  this.sendMessageAjax = getAjaxObject ();
  this.sendMessageAjax.onreadystatechange =
    this.sendMessageReadyStateChangeCb.bind (this);

  if (message[0] == "message")
  {
    /* The server assumes we've stopped typing whenever a message is
     * sent */
    this.sentTypingState = false;

    this.sendMessageAjax.open ("POST",
                               this.getUrl ("send_message?" + this.personId));
    this.sendMessageAjax.setRequestHeader ("Content-Type",
                                           "text/plain; charset=UTF-8");
    this.sendMessageAjax.send (message[1]);
  }
  else if (message[0] == "move-tile")
  {
    this.sendMessageAjax.open ("GET",
                               this.getUrl ("move_tile?" + this.personId + "&" +
                                            message[1] + "&" +
                                            message[2] + "&" +
                                            message[3]));
    this.sendMessageAjax.send ();
  }
  else if (message[0] == "shout")
  {
    this.sendMessageAjax.open ("GET", this.getUrl ("shout?" + this.personId));
    this.sendMessageAjax.send ();
  }
  else if (message[0] == "turn")
  {
    this.sendMessageAjax.open ("GET", this.getUrl ("turn?" + this.personId));
    this.sendMessageAjax.send ();
  }

  this.resetKeepAlive ();
};

ChatSession.prototype.queueCurrentMessage = function ()
{
  var message;

  if (this.state != "in-progress")
    return;

  message = $("#message-input-box").val ();

  if (message.length > 0)
  {
    $("#message-input-box").val ("");
    this.messageQueue.push (["message", message]);
    this.sendNextMessage ();
  }
};

ChatSession.prototype.shoutButtonClickCb = function ()
{
  this.shout ();
  $("#message-input-box")[0].focus ();
};

ChatSession.prototype.queueSimpleMessage = function (message)
{
  var i;

  if (this.state != "in-progress")
    return;

  /* Make sure that we haven't already queued the same message */
  for (i = 0; i < this.messageQueue.length; i++)
    if (this.messageQueue[i][0] == message)
      return;

  this.messageQueue.push ([message]);
  this.sendNextMessage ();
}

ChatSession.prototype.shout = function ()
{
  if (this.shoutingPlayer)
    return;

  this.queueSimpleMessage ("shout");
};

ChatSession.prototype.turnButtonClickCb = function ()
{
  this.turn ();
  $("#message-input-box")[0].focus ();
};

ChatSession.prototype.turn = function ()
{
  this.queueSimpleMessage ("turn");
};

ChatSession.prototype.submitMessageClickCb = function ()
{
  this.queueCurrentMessage ();
};

ChatSession.prototype.messageKeyDownCb = function (event)
{
  if ((event.which == 10 || event.which == 13) &&
      $("#message-input-box").val ().length > 0)
  {
    event.preventDefault ();
    event.stopPropagation ();
    this.queueCurrentMessage ();
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
    }
    else if (event.which == 32)
    {
      event.preventDefault ();
      event.stopPropagation ();
      this.turn ();
    }
  }
};

ChatSession.prototype.inputCb = function (event)
{
  /* Maybe update the typing status */
  this.sendNextMessage ();
};

ChatSession.prototype.newConversationCb = function ()
{
  window.location.reload ();
};

ChatSession.prototype.focusCb = function ()
{
  this.unreadMessages = 0;
  document.title = "Verda Ŝtelo";
};

ChatSession.prototype.getTileNumForEvent = function (event)
{
  if ((event.target.className) != "tile")
    return null;

  var tileNum;

  for (tileNum = 0; tileNum < this.tiles.length; tileNum++)
  {
    var tile = this.tiles[tileNum];
    if (tile && tile.element == event.target)
      return tileNum;
  }

  return null;
};

ChatSession.prototype.mouseDownCb = function (event)
{
  var tileNum;

  if (event.button != 0)
    return;

  event.preventDefault ();

  tileNum = this.getTileNumForEvent (event);
  if (tileNum == null)
    return;

  var tile = this.tiles[tileNum];

  var position = $(tile.element).position ();
  this.dragOffsetX = event.pageX - position.left;
  this.dragOffsetY = event.pageY - position.top;

  this.dragTile = tileNum;
  this.tileMoved = false;

  $(tile.element).stop ();
  this.raiseTile (tile);
};

ChatSession.prototype.mouseMoveCb = function (event)
{
  if (this.dragTile == null)
    return;

  var newX = Math.round ((event.pageX - this.dragOffsetX) /
                         this.pixelsPerEm * 10.0);
  var newY = Math.round ((event.pageY - this.dragOffsetY) /
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

  this.moveTile (this.dragTile, newX, newY);

  tile.x = newX;
  tile.y = newY;

  tile.element.style.left = (newX / 10.0) + "em";
  tile.element.style.top = (newY / 10.0) + "em";
};

ChatSession.prototype.moveTile = function (tileNum, x, y)
{
  var tile = this.tiles[tileNum];

  /* If we've already queued this move then just update the position */
  for (i = 0; i < this.messageQueue.length; i++)
  {
    if (this.messageQueue[i][0] == "move-tile" &&
        this.messageQueue[i][1] == tileNum)
    {
      this.messageQueue[i][2] = x;
      this.messageQueue[i][3] = y;
      return;
    }
  }

  this.messageQueue.push (["move-tile", tileNum, x, y]);
  this.sendNextMessage ();
};

ChatSession.prototype.mouseUpCb = function (event)
{
  if (event.button != 0)
    return;

  event.preventDefault ();

  if (this.dragTile != null)
  {
    var tile = this.tiles[this.dragTile];

    if (!this.tileMoved &&
        this.lastMovedTile &&
        this.lastMovedTile != tile)
    {
      var newPos = this.lastMovedTile.x + 20;
      var maxPos = ($("#board").innerWidth () / this.pixelsPerEm *
                    10.0 - 20);
      if (newPos < maxPos)
      {
        this.moveTile (this.dragTile, newPos, this.lastMovedTile.y);
        this.lastMovedTile = tile;
      }
    }

    this.dragTile = null;
  }
};

ChatSession.prototype.start = function ()
{
  $("#status-note").text ("@CONNECTING@");
  this.setState ("connecting");
  this.startWatchAjax ();

  $("#shout-button").bind ("click", this.shoutButtonClickCb.bind (this));
  $("#turn-button").bind ("click", this.turnButtonClickCb.bind (this));
  $("#submit-message").bind ("click", this.submitMessageClickCb.bind (this));
  $("#message-input-box").bind ("keydown", this.messageKeyDownCb.bind (this));
  $("#message-input-box").bind ("input", this.inputCb.bind (this));
  $("#new-conversation-button").bind ("click",
                                      this.newConversationCb.bind (this));
  /* Prevent default handling of mouse events on the board because
   * otherwise you can accidentally select the text in a tile */
  var preventDefaultCb = function (event) { event.preventDefault (); };
  $("#board").click (preventDefaultCb);

  $("#board").mousedown (this.mouseDownCb.bind (this));
  $("#board").mousemove (this.mouseMoveCb.bind (this));
  $("#board").mouseup (this.mouseUpCb.bind (this));

  $(document).keydown (this.documentKeyDownCb.bind (this));

  $(window).focus (this.focusCb.bind (this));

  $(window).unload (this.unloadCb.bind (this));

  this.updateRemainingTiles ();

  /* Work out the scale from pixels to ems so that we can translate
   * mouse positions to offsets in ems */
  var dummyElem = document.createElement ("div");
  dummyElem.style.width = "100em";
  dummyElem.style.height = "100em";
  dummyElem.style.position = "absolute";
  $("#board").append (dummyElem);
  this.pixelsPerEm = $(dummyElem).innerWidth () / 100.0;
  $(dummyElem).remove ();
};

ChatSession.prototype.unloadCb = function ()
{
  if (this.personId)
  {
    /* Try to squeeze in a synchronous Ajax to let the server know the
     * person has left */
    var ajax = getAjaxObject ();

    ajax.open ("GET", this.getUrl ("leave?" + this.personId),
               false /* not asynchronous */);
    ajax.send (null);

    /* If this is an XDomainRequest then making it asynchronous
     * doesn't work but we can do a synchronous request back to the
     * same domain. With any luck this will cause it to wait long
     * enough to also flush the leave request */
    if (ajax.xdr)
      {
        var xhr = new XMLHttpRequest ();
        xhr.open ("GET", window.location.url, false);
        xhr.send (null);
      }
  }
};

ChatSession.prototype.resetKeepAlive = function ()
{
  if (this.keepAliveTimeout)
    clearTimeout (this.keepAliveTimeout);
  this.keepAliveTime = new Date ();
  this.keepAliveTimeout = setTimeout (this.sendNextMessage.bind (this),
                                      KEEP_ALIVE_TIME);
};

ChatSession.prototype.getPlayer = function (playerNum)
{
  var player;

  if (!(player = this.players[playerNum]))
  {
    player = this.players[playerNum] = {};

    player.flags = PLAYER_CONNECTED;
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
  function updatePlayButton ()
  {
    $("#playbutton")[0].disabled = ($("#namebox").val ().match (/\S/) == null);
  }

  function start ()
  {
    var cs = new ChatSession ($("#namebox").val ());
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
    $("#namebox").bind ("keydown", keyDownCb);
    $("#namebox").bind ("input", inputCb);
    $("#namebox")[0].focus ();
    $("#playbutton").click (start);

    updatePlayButton ();
  }

  $(window).load (loadCb);
}) ();
