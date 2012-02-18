function addCommand(name, fun) {
  FugueCommands[name] = {
    name: name,
    callback: fun
  }
}

addCommand('key_ctrl_P', function() { recallPrev(); });
addCommand('key_ctrl_N', function() { recallNext(); });
addCommand('key_ctrl_up', function() { recallPrev(); });
addCommand('key_ctrl_down', function() { recallNext(); });

addCommand('limit', function(args) {
  try {
    var m = args.split(/\s+/,2);
    if (m[0].match(/^-m/)) {
      if (m[0].match(/^-mregexp$/i)) {
        setLimit(curWorld, new RegExp(m[1]));
      } else if (m[0].match(/^-mglob/i)) {
        setLimit(curWorld, glob_to_regexp(m[1]));
      } else {
        API.alert("Valid values for -m are 'glob' and 'regexp'.");
      }
    } else {
      setLimit(curWorld, glob_to_regexp(args));
    }
  } catch (err) {
    API.alert("Unable to limit: '" + err + "'");
  }
});

addCommand('unlimit', function(args) {
  try {
    setLimit(curWorld, undefined);
  } catch (err) {
    API.alert("Unable to unlimit: '" + err.description + "'");
  }
});

addCommand('fg', function(args) {
  if (allWorlds[args]) {
    showWorld(args);
  }
});
addCommand('open', function(args) {
  API.world.open(args);
});
addCommand('connect', function(args) {
  var m = args.split(/\s+/,2);
  API.world.connect(curWorld, m[0], m[1]);
});
addCommand('disconnect', function(args) {
  API.world.disconnect(curWorld);
});
addCommand('close', function(args) {
  API.world.close(curWorld);
});
addCommand('setpassword', function(args) {
  if (args.match(/\S{5}/)) {
    appendToWorld(curWorld, 'Submitting password change request. (It won\'t verify)');
    API.user.setPassword(args);
  } else {
    appendToWorld(curWorld, 'Password too short.');
  }
});
addCommand('wob', function(args) {
  $.stylesheetSwitch('white-on-black');
});
addCommand('bow', function(args) {
  $.stylesheetSwitch('black-on-white');
});
addCommand('help', function(args) {
    var hlp = {};
    hlp['help_'] = [
    "-- WebFugue help --",
    "Welcome to the WebFugue peel for Banana!",
    "",
    "/help        - This screen",
    "/help worlds - Help on commands for creating and navigating worlds.",
    "/help other  - Help for the other possibly useful commands.",
    "",
    "The source code for Banana is available at:",
    "https://github.com/captdeaf/banana-muclient",
    "",
    "",
    "Using WebFugue (Quick start):",
    "",
    "/open &lt;worldname&gt;       - Open a tab with the name &lt;worldname&gt;.",
    "/connect &lt;host&gt; &lt;port&gt;  - In a tab, connect to telnet server.",
    "Then connect to your character as normal!",
    "",
    "If you lose your connection to Webfugue, Banana's connection to worlds",
    "will not be lost. Use /disconnect to disconnect from a server.",
    "",
    "--Example--",
    "To connect to Walker's test mush for \"support\":",
    "",
    "/open walker",
    "/connect localhost 4240",
    "connect guest",
    "----"
  ];
  hlp['help_worlds'] = [
    "-- World commands --",
    "/open &lt;worldname&gt;       - Open a tab with the name &lt;worldname&gt;.",
    "/connect &lt;host&gt; &lt;port&gt;  - In a tab, connect to telnet server.",
    "/disconnect             - Disconnect from the current world.",
    "/close                  - Close the current tab (disconnects from the world).",
    "",
    "/fg &lt;worldname&gt; - Display the named world, when you have more than one.",
    "",
    "If you lose your connection to Webfugue, Banana's connection to worlds",
    "will not be lost. Use /disconnect to disconnect from a server.",
  ];
  hlp['help_other'] = [
    "-- Other commands --",
    "/wob - Change colors to white-on-black style",
    "/bow - Change colors to black-on-white style",
    "",
    "/limit &lt;pattern&gt; shows only lines matching the glob &lt;pattern&gt;.",
    "/unlimit removes the pattern filter.",
    "",
    "/setpassword &lt;password&gt; - Set a new password for your Banana web login.",
    "",
    "ctrl+p or ctrl+up recalls previous command",
    "ctrl+n or ctrl+down recalls next command",
  ];
  hlp['help__notfound'] = [
    "Help file for \"".concat(args).concat("\" not found.")
  ];
  file = "help_".concat(args);
  if (! hlp.hasOwnProperty(file)) {
    file = "help__notfound";
  }
  for (var i = 0; i < hlp[file].length; i++) {
    appendToWorld(curWorld, hlp[file][i]);
  }
});
