pianobar
========

pianobar is a free/open-source, console-based client for the personalized
online radio Pandora_.

.. _Pandora: http://www.pandora.com

.. note::
   This is a fork of the original pianobar with WebSocket support for custom UIs.
   See the `WebSocket Support`_ section below for details.
   Original repository: https://github.com/PromyLOPh/pianobar

.. image:: https://6xq.net/pianobar/pianobar-screenshot.png
    :target: https://6xq.net/pianobar/pianobar-screenshot.png
    :alt: pianobar screenshot

Features
--------

- play and manage (create, add more music, delete, rename, ...) stations
- rate songs and explain why they have been selected
- upcoming songs/song history
- customize keybindings and text output (see `configuration example`_)
- remote control and eventcmd interface (send tracks to last.fm_, for example)
- WebSocket API for building custom UIs (web, mobile, desktop)
- included modern web UI built with Lit components
- proxy support for listeners outside the USA

.. _last.fm: https://www.last.fm
.. _configuration example: https://github.com/PromyLOPh/pianobar/blob/master/contrib/config-example

Download
--------

There are community provided packages available for most Linux distributions
(see your distribution’s package manager), Mac OS X (via homebrew_)
and \*BSD as well as a `native Windows port`_.

.. _homebrew: http://brew.sh/
.. _native Windows Port: https://github.com/thedmd/pianobar-windows

The current pianobar release is 2024.12.21_ (sha256__, sign__). More recent and
experimental code is available at GitHub_ and the local gitweb_. Older releases
are available here:

- 2022.04.01_ (sha256__, sign__)
- 2020.11.28_ (sha256__, sign__)
- 2020.04.05_ (sha256__, sign__)
- 2019.02.14_ (sha256__, sign__)
- 2019.01.25_ (sha256__, sign__)
- 2018.06.22_ (sha256__, sign__)
- 2017.08.30_ (sha256__, sign__)
- 2016.06.02_ (sha256__, sign__)
- 2015.11.22_ (sha256__, sign__)
- 2014.09.28_ (sha256__, sign__)
- 2014.06.08_ (sha256__, sign__)
- 2013.09.15_ (sha256__, sign__)
- 2013.05.19_ (sha256__, sign__)
- 2012.12.01_ (sha256__, sign__)
- 2012.09.07_ (sha256__, sign__)
- 2012.06.24_ (sha256__, sign__)
- 2012.05.06_ (sha256__, sign__)
- 2012.04.24_ (sha256__, sign__)
- 2012.01.10_ (sha256__, sign__)
- 2011.12.11_ (sha256__, sign__)
- 2011.11.11_ (sha256__, sign__)
- 2011.11.09_ (sha256__, sign__)
- 2011.09.22_ (sha256__, sign__)
- 2011.07.09_ (sha256__, sign__)
- 2011.04.27_ (sha256__, sign__)
- 2011.04.10_ (sha256__, sign__)
- 2011.01.24_ (sha256__)
- 2010.11.06_ (sha1__)
- 2010.10.07_ (sha1__)
- 2010.08.21_ (sha1__)

.. _2024.12.21: https://6xq.net/pianobar/pianobar-2024.12.21.tar.bz2
__ https://6xq.net/pianobar/pianobar-2024.12.21.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2024.12.21.tar.bz2.asc
.. _2022.04.01: https://6xq.net/pianobar/pianobar-2022.04.01.tar.bz2
__ https://6xq.net/pianobar/pianobar-2022.04.01.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2022.04.01.tar.bz2.asc
.. _2020.11.28: https://6xq.net/pianobar/pianobar-2020.11.28.tar.bz2
__ https://6xq.net/pianobar/pianobar-2020.11.28.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2020.11.28.tar.bz2.asc
.. _snapshot: http://github.com/PromyLOPh/pianobar/tarball/master
.. _GitHub: http://github.com/PromyLOPh/pianobar/
.. _gitweb: https://6xq.net/git/lars/pianobar.git/
.. _2020.04.05: https://6xq.net/pianobar/pianobar-2020.04.05.tar.bz2
__ https://6xq.net/pianobar/pianobar-2020.04.05.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2020.04.05.tar.bz2.asc
.. _2019.02.14: https://6xq.net/pianobar/pianobar-2019.02.14.tar.bz2
__ https://6xq.net/pianobar/pianobar-2019.02.14.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2019.02.14.tar.bz2.asc
.. _2019.01.25: https://6xq.net/pianobar/pianobar-2019.01.25.tar.bz2
__ https://6xq.net/pianobar/pianobar-2019.01.25.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2019.01.25.tar.bz2.asc
.. _2018.06.22: https://6xq.net/pianobar/pianobar-2018.06.22.tar.bz2
__ https://6xq.net/pianobar/pianobar-2018.06.22.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2018.06.22.tar.bz2.asc
.. _2017.08.30: https://6xq.net/pianobar/pianobar-2017.08.30.tar.bz2
__ https://6xq.net/pianobar/pianobar-2017.08.30.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2017.08.30.tar.bz2.asc
.. _2016.06.02: https://6xq.net/pianobar/pianobar-2016.06.02.tar.bz2
__ https://6xq.net/pianobar/pianobar-2016.06.02.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2016.06.02.tar.bz2.asc
.. _2015.11.22: https://6xq.net/pianobar/pianobar-2015.11.22.tar.bz2
__ https://6xq.net/pianobar/pianobar-2015.11.22.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2015.11.22.tar.bz2.asc
.. _2014.09.28: https://6xq.net/pianobar/pianobar-2014.09.28.tar.bz2
__ https://6xq.net/pianobar/pianobar-2014.09.28.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2014.09.28.tar.bz2.asc
.. _2014.06.08: https://6xq.net/pianobar/pianobar-2014.06.08.tar.bz2
__ https://6xq.net/pianobar/pianobar-2014.06.08.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2014.06.08.tar.bz2.asc
.. _2013.09.15: https://6xq.net/pianobar/pianobar-2013.09.15.tar.bz2
__ https://6xq.net/pianobar/pianobar-2013.09.15.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2013.09.15.tar.bz2.asc
.. _2013.05.19: https://6xq.net/pianobar/pianobar-2013.05.19.tar.bz2
__ https://6xq.net/pianobar/pianobar-2013.05.19.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2013.05.19.tar.bz2.asc
.. _2012.12.01: https://6xq.net/pianobar/pianobar-2012.12.01.tar.bz2
__ https://6xq.net/pianobar/pianobar-2012.12.01.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2012.12.01.tar.bz2.asc
.. _2012.09.07: https://6xq.net/pianobar/pianobar-2012.09.07.tar.bz2
__ https://6xq.net/pianobar/pianobar-2012.09.07.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2012.09.07.tar.bz2.asc
.. _2012.06.24: https://6xq.net/pianobar/pianobar-2012.06.24.tar.bz2
__ https://6xq.net/pianobar/pianobar-2012.06.24.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2012.06.24.tar.bz2.asc
.. _2012.05.06: https://6xq.net/pianobar/pianobar-2012.05.06.tar.bz2
__ https://6xq.net/pianobar/pianobar-2012.05.06.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2012.05.06.tar.bz2.asc
.. _2012.04.24: https://6xq.net/pianobar/pianobar-2012.04.24.tar.bz2
__ https://6xq.net/pianobar/pianobar-2012.04.24.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2012.04.24.tar.bz2.asc
.. _2012.01.10: https://6xq.net/pianobar/pianobar-2012.01.10.tar.bz2
__ https://6xq.net/pianobar/pianobar-2012.01.10.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2012.01.10.tar.bz2.asc
.. _2011.12.11: https://6xq.net/pianobar/pianobar-2011.12.11.tar.bz2
__ https://6xq.net/pianobar/pianobar-2011.12.11.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2011.12.11.tar.bz2.asc
.. _2011.11.11: https://6xq.net/pianobar/pianobar-2011.11.11.tar.bz2
__ https://6xq.net/pianobar/pianobar-2011.11.11.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2011.11.11.tar.bz2.asc
.. _2011.11.09: https://6xq.net/pianobar/pianobar-2011.11.09.tar.bz2
__ https://6xq.net/pianobar/pianobar-2011.11.09.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2011.11.09.tar.bz2.asc
.. _2011.09.22: https://6xq.net/pianobar/pianobar-2011.09.22.tar.bz2
__ https://6xq.net/pianobar/pianobar-2011.09.22.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2011.09.22.tar.bz2.asc
.. _2011.07.09: https://6xq.net/pianobar/pianobar-2011.07.09.tar.bz2
__ https://6xq.net/pianobar/pianobar-2011.07.09.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2011.07.09.tar.bz2.asc
.. _2011.04.27: https://6xq.net/pianobar/pianobar-2011.04.27.tar.bz2
__ https://6xq.net/pianobar/pianobar-2011.04.27.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2011.04.27.tar.bz2.asc
.. _2011.04.10: https://6xq.net/pianobar/pianobar-2011.04.10.tar.bz2
__ https://6xq.net/pianobar/pianobar-2011.04.10.tar.bz2.sha256
__ https://6xq.net/pianobar/pianobar-2011.04.10.tar.bz2.asc
.. _2011.01.24: https://6xq.net/pianobar/pianobar-2011.01.24.tar.bz2
__ https://6xq.net/pianobar/pianobar-2011.01.24.tar.bz2.sha256
.. _2010.11.06: https://6xq.net/pianobar/pianobar-2010.11.06.tar.bz2
__ https://6xq.net/pianobar/pianobar-2010.11.06.tar.bz2.sha1
.. _2010.10.07: https://6xq.net/pianobar/pianobar-2010.10.07.tar.bz2
__ https://6xq.net/pianobar/pianobar-2010.10.07.tar.bz2.sha1
.. _2010.08.21: https://6xq.net/pianobar/pianobar-2010.08.21.tar.bz2
__ https://6xq.net/pianobar/pianobar-2010.08.21.tar.bz2.sha1

Install
-------

You need the following software to build pianobar:

- GNU make
- pthreads
- libao
- libcurl ≥ 7.32.0
- gcrypt [1]_
- json-c
- ffmpeg ≤ 5.1 [2]_
- UTF-8 console/locale

.. [1] with blowfish cipher enabled
.. [2] required: demuxer mov, decoder aac, protocol http and filters volume,
        aformat, aresample

Then type::

	gmake clean && gmake

You can run the client directly from the source directory now::

	./pianobar

Or install it to ``/usr/local`` by issuing::

	gmake install

WebSocket Support
-----------------

This fork adds WebSocket support, enabling real-time communication for custom user
interfaces. Build web, mobile, or desktop UIs that control pianobar and receive
live updates.

Features
++++++++

- Real-time playback state updates
- Song metadata (title, artist, album, art URL)
- Station management (list, create, delete, switch)
- Playback control (play, pause, skip, volume)
- Song actions (love, ban, tired, create station)
- Two-way communication via Socket.IO protocol

Building with WebSocket Support
++++++++++++++++++++++++++++++++

To build pianobar with WebSocket support enabled::

	make WEBSOCKET=1 clean && make WEBSOCKET=1

Or use the included build script for development::

	./build.sh        # Standard build with WebSocket
	./build.sh debug  # Debug build with crash capture

Configuration
+++++++++++++

Add these settings to your ``~/.config/pianobar/config``::

	ui_mode = both
	websocket_port = 8080
	webui_path = ./dist/webui

UI mode options:

- ``cli``: Command-line interface only, no WebSocket
- ``web``: Web-only (daemonizes, runs in background)
- ``both``: Both CLI and WebSocket (default, runs in foreground)

When using ``web`` mode, pianobar runs as a daemon and you should specify::

	ui_mode = web
	websocket_port = 8080
	webui_path = ./dist/webui
	pid_file = /tmp/pianobar.pid
	log_file = /tmp/pianobar.log

Then start pianobar and it will run in the background. Open ``http://localhost:8080`` in your browser.

Included Web UI
+++++++++++++++

A modern, lightweight web interface is included:

- Built with Lit web components (5KB framework)
- Material Design styling with dark mode
- Real-time updates via WebSocket
- Mobile-responsive design
- Album art display
- Volume control and playback controls
- Station management

See ``webui/README.md`` for development and build instructions.

Building Custom UIs
+++++++++++++++++++

The WebSocket API uses Socket.IO and provides events for:

**Client → Server (Commands)**::

	- play, pause, skip
	- love_song, ban_song, tired_song
	- set_volume
	- list_stations, play_station
	- create_station, delete_station

**Server → Client (Events)**::

	- state_update: Playback state changes
	- song_update: New song metadata
	- station_update: Station list changes
	- volume_update: Volume changes

Connect to ``ws://localhost:8080`` using any Socket.IO client library.
Full protocol documentation available in ``WEBSOCKET_PROTOCOL.md``.

For more information, see:

- Web UI development: ``webui/README.md``
- WebSocket protocol: ``WEBSOCKET_PROTOCOL.md``
- Original repository: https://github.com/PromyLOPh/pianobar

FAQ
---

The audio output does not work as expected. What can I do?
    pianobar uses libao and most problems are related to a broken libao
    configuration. Have a look at issue `#167`_ for example.
Can I donate money? Do you have a Flattr/Bitcoin/… account?
    No, money is not necessary to continue working on pianobar. There are many
    other ways to support pianobar: Reporting bugs, creating `cool stuff`_
    based on pianobar, blogging about it and the most important one: Keeping
    Pandora alive.

.. _#167: https://github.com/PromyLOPh/pianobar/issues/167
.. _cool stuff: `addons`_

External projects
-----------------

Addons
++++++

control-pianobar_
    Scripts that interact with pianobar entirely through notification bubbles
    and hotkeys
pianobar.el_
    Emacs interface for pianobar
`pianobar-mediaplayer2`_
    Control pianobar like any other media player through DBUS/MPRIS.
PianobarNowPlayable_
    Integrate pianobar with the Now Playing feature of macOS

.. _control-pianobar: http://malabarba.github.io/control-pianobar/
.. _pianobar.el: https://github.com/agrif/pianobar.el
.. _pianobar-mediaplayer2: https://github.com/ryanswilson59/pianobar-mediaplayer2
.. _PianobarNowPlayable: https://github.com/iDom818/PianobarNowPlayable

Clients
+++++++

pithos_
	Python/GTK desktop client
pianod_
    Pandora UNIX daemon, based on pianobar
Hermes_
    Pandora Client for OS X

.. _pithos: http://pithos.github.io/
.. _pianod: http://deviousfish.com/pianod/
.. _Hermes: http://hermesapp.org/

Standalone devices
++++++++++++++++++

PandoraBar_
    Beagleboard-based radio device running pianobar
`Pandora’s Box`_
    Raspberry Pi-based standalone devices running pianobar

.. _PandoraBar: https://hackaday.com/2012/09/20/how-to-build-your-own-dedicated-pandora-radio/
.. _Pandora’s Box: http://www.instructables.com/id/Pandoras-Box-An-Internet-Radio-player-made-with/

