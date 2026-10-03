#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the launcher's layouts (RmlUi documents), with nothing but the standard library.

    render-layout.py OUTPUT_UI

  start.rml   the start screen: the screen split down the middle, Cemu on the left over the Wii U
              Homebrew Launcher's bubbles, Azahar on the right over the 3DS one's waves, made yellow
  main.rml    Cemu's launcher: home, library, graphic packs, settings, about
  azahar.rml  Azahar's: the same screens, in its gold (tools/recolour-ui.py's yellow theme), for 3DS
              games, with what a 3DS has in place of the Wii U's graphic packs and players

The two launchers are one layout (ProsperoEden's, with its element ids and classes, so its
stylesheet applies, and port/frontend/ui/ps5cemu.rcss's additions), written for each emulator: its
artwork's folders, stylesheets, names and words. Their repeated rows are written out here, as are
the controller hints.
"""

import os
import sys

WIIU = {
    "id": "wiiu",
    "chrome": "chrome", "icons": "icons", "suffix": "",
    "brand": "PS5 CEMU", "brand_icon": "icons/ps5cemu-72.tga", "cover": "icons/ps5cemu.tga",
    "system": "WII U", "footnote": "Cemu, the Wii U emulator, on PlayStation 5",
    "detail": [("TITLE ID", "game-detail-format"), ("VERSION", "game-detail-size"), ("DLC", "game-detail-dlc")],
    "packs": True,
    "settings": [
        ("Video", "VIDEO", "Upscaling filter, 120 Hz, overlay"),
        ("Audio", "AUDIO", "Game volume"),
        ("Controls", "CONTROLS", "Controllers, buttons, motion, vibration"),
        ("Game files", "GAME FILES", "The folder for your Wii U games"),
        ("Install updates and DLC", "INSTALL", "Updates and DLC into the Wii U storage"),
        ("Diagnostics", "DIAGNOSTICS", "Jailbreak, JIT and log files"),
    ],
    "files_copy": "Choose the folder that holds your Wii U games",
    "controls_copy": "Cemu's controller settings, for each player's DualSense",
    "credits": ("Powered by Cemu", "All credit for the Wii U emulator goes to the Cemu team and its contributors.", "cemu.info",
                "Mihawk-99 and mpereiraesaa (RADV on the PS5), BlackBearReloaded (ProsperoEden's launcher, the app "
                "boilerplate), Swordpdf (PS5SX2), John Tornblom (payload SDK), Dimok (the Homebrew Launcher), the graphic pack authors.",
                "An unofficial port. Not affiliated with the Cemu team, Nintendo or Sony."),
    "setup": [("GAMES", "about-games-path", "/data/ps5cemu/games"), ("KEYS", "about-keys-path", "/data/ps5cemu/keys.txt"),
              ("SAVES", "about-mlc-path", "/data/ps5cemu/mlc01")],
    "setup_note": "Dump your games from your own Wii U (.wua, .wud, .wux, or a folder with code, content and meta).",
}

N3DS = {
    "id": "3ds",
    "chrome": "chrome-3ds", "icons": "icons-3ds", "suffix": "-3ds",
    "brand": "PS5 AZAHAR", "brand_icon": "icons/azahar-72.tga", "cover": "icons/azahar.tga",
    "system": "NINTENDO 3DS", "footnote": "Azahar, the 3DS emulator, on PlayStation 5",
    "detail": [("TITLE ID", "game-detail-format"), ("PUBLISHER", "game-detail-size"), ("FORMAT", "game-detail-dlc")],
    "packs": False,
    "settings": [
        ("Video", "VIDEO", "Internal resolution, screen layout, textures"),
        ("Audio", "AUDIO", "Game volume"),
        ("Controls", "CONTROLS", "Buttons, circle pad, motion"),
        ("Game files", "GAME FILES", "The folder for your 3DS games"),
        ("Install CIA files", "INSTALL", "CIA files into the 3DS storage"),
        ("Diagnostics", "DIAGNOSTICS", "Jailbreak, JIT and log files"),
    ],
    "files_copy": "Choose the folder that holds your 3DS games",
    "controls_copy": "Azahar's controls, on the DualSense",
    "credits": ("Powered by Azahar", "All credit for the 3DS emulator goes to the Azahar team and its contributors, who carry on Citra's work.",
                "azahar-emu.org",
                "Mihawk-99 (PS5_Azahar, PS5_Dynarmic, RADV on the PS5), merryhime (dynarmic), BlackBearReloaded (ProsperoEden's "
                "launcher, the app boilerplate), fincs and smealum (the 3DS Homebrew Launcher), John Tornblom (payload SDK).",
                "An unofficial port. Not affiliated with the Azahar team, Nintendo or Sony."),
    "setup": [("GAMES", "about-games-path", "/data/ps5cemu/azahar/games"), ("DATA", "about-keys-path", "/data/ps5cemu/azahar"),
              ("SAVES", "about-mlc-path", "/data/ps5cemu/azahar/sdmc")],
    "setup_note": "Dump your games from your own 3DS, decrypted: .3ds or .cci, .cxi, .cia or .3dsx.",
}


def chrome(side, kind, w, h):
    return (f'<img class="{kind}-chrome base-chrome" src="{side["chrome"]}/{kind}-normal.tga" width="{w}" height="{h}" alt=""/>'
            f'<img class="{kind}-chrome focused-chrome" src="{side["chrome"]}/{kind}-focused.tga" width="{w}" height="{h}" alt=""/>')


def rows(side, prefix, count, inner, extra_class=''):
    row = chrome(side, 'library-row', 760, 78)
    return '\n'.join(f'            <span id="{prefix}-row-{i}" class="library-row library-row-{i}{extra_class}">{row}{inner(i)}</span>'
                     for i in range(count))


def hint(side, icon, text, text_id=None, second=None):
    tid = f' id="{text_id}"' if text_id else ''
    folder = side["icons"]
    sec = f'<img class="hint-second" src="{folder}/{second}-mono.tga" width="26" height="26" alt=""/>' if second else ''
    cls = 'hint hint-pair' if second else 'hint'
    return (f'<span class="{cls}"><img src="{folder}/{icon}-mono.tga" width="26" height="26" alt=""/>{sec}'
            f'<span{tid} class="hint-text">{text}</span></span>')


def footer(*hints):
    return f'        <footer class="hints">{"".join(hints)}</footer>'


def head(side):
    return f'''<rml>
  <!-- PS5CEMU-HAR's launcher (tools/render-layout.py). Layout, artwork, fonts and stylesheet are
       ProsperoEden's (headless/prosperoeden/ui, GPL-3.0-or-later, by BlackBearReloaded), adapted and
       recoloured (tools/recolour-ui.py); ps5cemu.rcss adds what is new. Element ids and classes
       follow ProsperoEden's so its styles apply. The background is drawn under the page. -->
  <head>
    <title>PS5CEMU-HAR</title>
    <link type="text/rcss" href="styles/app{side["suffix"]}.rcss" />
    <link type="text/rcss" href="styles/ps5cemu{side["suffix"]}.rcss" />
  </head>
  <body>
    <div id="app-shell">'''


TAIL = '''    </div>
  </body>
</rml>
'''


def launcher(side):
    S = side
    c = S["chrome"]
    PANEL = f'<img class="library-panel-chrome" src="{c}/dialog-panel.tga" width="820" height="720" alt=""/>'
    MODAL = f'<img class="dialog-panel-chrome" src="{c}/modal-panel.tga" width="820" height="720" alt=""/>'
    DLG = chrome(S, 'dialog-row', 736, 94)
    LIB = chrome(S, 'library-row', 760, 78)
    h = lambda *args, **kw: hint(S, *args, **kw)
    out = [head(S)]
    add = out.append
    add(f'''      <header id="header">
        <img id="brand-icon" src="{S["brand_icon"]}" width="72" height="72" alt=""/>
        <span id="brand-name">{S["brand"]}</span>
        <span id="brand-version">{S["system"]}  /  PS5CEMU-HAR</span>
        <span id="menu-clock">--:--</span>
      </header>
      <nav id="menu">
        <button id="load-rom" class="nav-item"><img class="nav-focus" src="{c}/nav-focused.tga" width="136" height="64" alt=""/><span>Library</span></button>
        <button id="settings" class="nav-item"><img class="nav-focus" src="{c}/nav-focused.tga" width="136" height="64" alt=""/><span>Settings</span></button>
        <button id="help-about" class="nav-item"><img class="nav-focus" src="{c}/nav-focused.tga" width="136" height="64" alt=""/><span>About</span></button>
      </nav>

      <main id="last-played-card">
        <img class="hero-chrome" src="{c}/hero-panel.tga" width="1680" height="480" alt=""/>
        <img id="last-played-cover" src="{S["cover"]}" width="336" height="336" alt=""/>
        <span class="hero-kicker">CONTINUE PLAYING</span>
        <span id="last-played-title">Your next adventure</span>
        <span id="last-played-caption">Choose a game from your library.</span>
        <button id="continue-game" class="hero-button"><img class="hero-button-chrome base-chrome" src="{c}/hero-button.tga" width="272" height="72" alt=""/><img class="hero-button-chrome focused-chrome" src="{c}/hero-button-focused.tga" width="272" height="72" alt=""/><span id="continue-copy">Launch game</span></button>''')
    if S["packs"]:
        add(f'''        <button id="hero-options" class="hero-button"><img class="hero-button-chrome base-chrome" src="{c}/hero-button.tga" width="272" height="72" alt=""/><img class="hero-button-chrome focused-chrome" src="{c}/hero-button-focused.tga" width="272" height="72" alt=""/><span>Graphic packs</span></button>''')
    add(f'''        <span class="hero-footnote">{S["footnote"]}</span>
      </main>

      <section id="recent-section">
        <span class="section-label">RECENTLY PLAYED</span>
        <button id="view-all"><img class="view-all-focus" src="{c}/view-all-focused.tga" width="304" height="40" alt=""/><span>VIEW ALL GAMES</span></button>
        <span id="recent-empty">Games you launch will appear here.</span>''')
    for i in range(4):
        add(f'        <button id="recent-{i}" class="recent-tile"><img class="recent-chrome base-chrome" src="{c}/recent-tile.tga" width="400" height="144" alt=""/>'
            f'<img class="recent-chrome focused-chrome" src="{c}/recent-tile-focused.tga" width="400" height="144" alt=""/>'
            f'<img id="recent-cover-{i}" class="recent-cover" src="{S["cover"]}" width="96" height="96" alt=""/><span id="recent-title-{i}" class="recent-title"></span></button>')
    home_hints = [h('cross', 'Select')] + ([h('triangle', 'Graphic packs')] if S["packs"] else []) + [h('dpad', 'Navigate'), h('circle', 'Change emulator')]
    add(f'''      </section>

      <div id="startup-status" class="quiet"></div>
      <footer id="footer" class="hints">{"".join(home_hints)}<span id="system-status">Looking for games</span></footer>

      <!-- Library -->
      <div id="rom-dialog" class="library-screen">
        <span class="library-shade"></span>
        <span class="library-title">Your games</span>
        <span class="library-copy">Select a game to begin</span>
        <section class="library-list-panel">
          {PANEL}
          <span class="library-panel-kicker">LIBRARY</span>
          <div id="rom-list-viewport"><div id="rom-list-track">
{rows(S, 'rom', 7, lambda i: f'<img id="rom-icon-{i}" class="rom-row-icon" src="{S["cover"]}" width="56" height="56" alt=""/><span id="rom-name-{i}" class="library-row-name rom-row-name"></span><span id="rom-format-{i}" class="library-row-meta"></span>')}
          </div></div>
          <span id="library-empty">No games found. Put them in the game files folder.</span>
          <span id="rom-scrollbar"><img class="scrollbar-track" src="{c}/scrollbar-track.tga" width="10" height="606" alt=""/><img id="rom-scrollbar-thumb" src="{c}/scrollbar-thumb.tga" width="10" height="110" alt=""/></span>
          <span id="library-position">0 OF 0</span>
        </section>
        <aside class="game-detail-panel">
          {PANEL}
          <span class="detail-kicker">GAME DETAILS</span>
          <span id="game-detail-title">No game selected</span>
          <img id="game-cover" src="{S["cover"]}" width="288" height="288" alt=""/><span id="cover-caption"></span>''')
    for i, (label, value_id) in enumerate(S["detail"]):
        add(f'          <span class="game-detail-label game-detail-line-{i}">{label}</span><span id="{value_id}" class="game-detail-value game-detail-line-{i}">-</span>')
    add('''          <span class="game-path-label">FILES</span><span id="game-detail-path" class="game-path">-</span>''')
    if S["packs"]:
        add(f'''          <span id="game-packs-setting"><img src="{c}/dialog-row-normal.tga" width="736" height="94" alt=""/><span class="game-mode-label">Graphic packs</span><span id="game-packs-value">-</span></span>
          <span id="game-mode-hint"><img src="{S["icons"]}/triangle-mono.tga" width="26" height="26" alt=""/><span id="game-mode-hint-text">Choose this game's graphic packs.</span></span>''')
    library_hints = [h('cross', 'Play'), h('circle', 'Back'), h('updown', 'Browse games')] + ([h('triangle', 'Graphic packs')] if S["packs"] else []) + [h('l1', 'Page', second='r1')]
    add(f'''        </aside>
{footer(*library_hints)}
      </div>
''')
    if S["packs"]:
        add(f'''      <!-- Graphic packs of one game, laid out as Cemu's Graphic Packs window: the packs, and each
           one's presets, a dropdown for each of its categories -->
      <div id="packs-dialog" class="library-screen">
        <span class="library-shade"></span>
        <span class="library-title">Graphic packs</span>
        <span id="packs-game" class="library-copy"></span>
        <section class="library-list-panel">
          {PANEL}
          <span class="library-panel-kicker">COMMUNITY PACKS</span>
          <div id="packs-list-viewport">
{rows(S, 'pack', 7, lambda i: f'<span id="pack-name-{i}" class="library-row-name pack-row-name"></span><span id="pack-state-{i}" class="library-row-meta pack-row-state"></span>')}
          </div>
          <span id="packs-empty">No graphic packs for this game.</span>
          <span id="packs-position">0 OF 0</span>
        </section>
        <aside class="game-detail-panel">
          {PANEL}
          <span id="pack-detail-kicker" class="detail-kicker">GRAPHIC PACK</span>
          <span id="pack-detail-title"></span>
          <span id="pack-detail-description"></span>
          <span class="about-divider pack-divider"></span>
          <span id="presets-kicker" class="detail-kicker presets-kicker">PRESETS</span>
          <span id="presets-empty">This pack has no presets: it is only on or off.</span>
          <div id="presets-viewport">
{rows(S, 'preset', 4, lambda i: f'<span id="preset-label-{i}" class="preset-label"></span><span id="preset-value-{i}" class="preset-value"></span><span class="preset-arrow"></span>', ' preset-row')}
          </div>
          <span id="presets-position"></span>
        </aside>
{footer(h('cross', 'On / off', 'packs-hint-0'), h('circle', 'Back', 'packs-hint-1'), h('updown', 'Browse packs', 'packs-hint-2'), h('leftright', 'Presets', 'packs-hint-3'))}
      </div>
''')
    add(f'''      <!-- Settings -->
      <div id="settings-dialog" class="settings-screen">
        <span class="library-shade"></span>
        <span class="library-title">Settings</span><span class="library-copy">Fine-tune your experience</span>
        <section class="settings-list-panel">
        {PANEL}
        <span class="library-panel-kicker">PREFERENCES</span>''')
    for i, (name, _, _) in enumerate(S["settings"]):
        add(f'        <span id="settings-row-{i}" class="dialog-row settings-row-{i}">{DLG}<span>{name}</span></span>')
    add(f'''        </section>
        <aside class="settings-detail-panel">{PANEL}
          <span class="detail-kicker">ON THIS CONSOLE</span>
          <span class="settings-detail-title">Make it yours.</span>
          <span class="settings-detail-copy">Adjust the essentials without leaving your library behind.</span>''')
    for i, (_, label, value) in enumerate(S["settings"]):
        add(f'          <span class="settings-detail-label settings-detail-{i}">{label}</span><span class="settings-detail-value settings-detail-{i}">{value}</span>')
    add(f'''        </aside>
{footer(h('cross', 'Select'), h('circle', 'Back'), h('updown', 'Browse settings'))}
      </div>

      <!-- Settings > Controls: the players -->
      <div id="controls-dialog" class="library-screen">
        <span class="library-shade"></span>
        <span class="library-title">Controls</span>
        <span class="library-copy">{S["controls_copy"]}</span>
        <section class="library-list-panel">
          {PANEL}
          <span class="library-panel-kicker">PLAYERS</span>
          <div class="list-viewport">
{rows(S, 'control', 4, lambda i: f'<span id="control-name-{i}" class="library-row-name setting-name">Player {i + 1}</span><span id="control-value-{i}" class="setting-value"></span>')}
          </div>
        </section>
        <aside class="game-detail-panel">
          {PANEL}
          <span id="control-kicker" class="detail-kicker">PLAYER 1</span>
          <span id="control-title" class="detail-title"></span>''')
    for i in range(3):
        add(f'          <span id="control-label-{i}" class="files-label files-line-{i}"></span><span id="control-detail-{i}" class="files-value files-line-{i} ready"></span>')
    add(f'''          <span class="about-divider files-divider-0"></span>
          <span id="control-help" class="detail-help">Touchpad: a cursor on the GamePad's screen; click to touch.<br/>Touchpad click + Options: the in-game menu.<br/>Touchpad click + L1: the TV or the GamePad as the main screen.<br/>Touchpad click + R1: the other screen in a corner.</span>
        </aside>
{footer(h('cross', 'Settings'), h('circle', 'Back'), h('updown', 'Browse players'))}
      </div>

      <!-- Settings > Controls > a player -->
      <div id="player-dialog" class="library-screen">
        <span class="library-shade"></span>
        <span id="player-title" class="library-title">Player 1</span>
        <span id="player-copy" class="library-copy"></span>
        <section class="library-list-panel">
          {PANEL}
          <span class="library-panel-kicker">SETTINGS</span>
          <div class="list-viewport">
{rows(S, 'player', 7, lambda i: f'<span id="player-name-{i}" class="library-row-name setting-name"></span><span id="player-value-{i}" class="setting-value"></span>')}
          </div>
        </section>
        <aside class="game-detail-panel">
          {PANEL}
          <span class="detail-kicker">THIS SETTING</span>
          <span id="player-detail-title" class="detail-title"></span>
          <span id="player-help" class="detail-help detail-help-high"></span>
        </aside>
{footer(h('cross', 'Choose', 'player-hint-0'), h('leftright', 'Change'), h('circle', 'Back'), h('updown', 'Browse'))}
      </div>

      <!-- Settings > Controls > buttons -->
      <div id="mapping-dialog" class="library-screen">
        <span class="library-shade"></span>
        <span class="library-title">Buttons</span>
        <span id="mapping-copy" class="library-copy"></span>
        <section class="library-list-panel">
          {PANEL}
          <span id="mapping-kicker" class="library-panel-kicker"></span>
          <div class="list-viewport">
{rows(S, 'map', 7, lambda i: f'<span id="map-name-{i}" class="library-row-name setting-name"></span><span id="map-value-{i}" class="setting-value"></span>')}
          </div>
          <span id="mapping-position" class="list-position">0 OF 0</span>
        </section>
        <aside class="game-detail-panel">
          {PANEL}
          <span class="detail-kicker">THIS BUTTON</span>
          <span id="map-title" class="detail-title"></span>
          <span class="files-label files-line-0">DUALSENSE</span><span id="map-input" class="files-value files-line-0 ready"></span>
          <span class="about-divider files-divider-0"></span>
          <span id="map-message" class="detail-help"></span>
        </aside>
{footer(h('cross', 'Assign'), h('square', 'Clear'), h('circle', 'Back'), h('updown', 'Browse buttons'), h('l1', 'Page', second='r1'))}
      </div>

      <!-- Settings > Game files, and Settings > Install: the file browser -->
      <div id="files-dialog" class="library-screen">
        <span class="library-shade"></span>
        <span id="files-title" class="library-title">Game files</span>
        <span id="files-copy" class="library-copy">{S["files_copy"]}</span>
        <section class="library-list-panel">
          {PANEL}
          <span class="library-panel-kicker">FOLDERS</span>
          <span id="files-path"></span>
          <div id="files-list-viewport">''')
    for i in range(6):
        add(f'            <span id="files-row-{i}" class="library-row library-row-{i} files-row">{LIB}'
            f'<img class="files-icon files-icon-folder" src="{S["icons"]}/folder.tga" width="40" height="32" alt=""/>'
            f'<img class="files-icon files-icon-up" src="{S["icons"]}/folder-up.tga" width="40" height="32" alt=""/>'
            f'<span id="files-name-{i}" class="library-row-name files-row-name"></span><span id="files-meta-{i}" class="library-row-meta files-row-meta"></span></span>')
    add(f'''          </div>
          <span id="files-empty">No folders here.</span>
          <span id="files-position">0 OF 0</span>
        </section>
        <aside class="game-detail-panel">
          {PANEL}
          <span id="files-kicker" class="detail-kicker">THIS FOLDER</span>
          <span id="files-current"></span>''')
    for i in range(3):
        add(f'          <span id="files-label-{i}" class="files-label files-line-{i}"></span><span id="files-value-{i}" class="files-value files-line-{i}"></span>')
    add(f'''          <span class="about-divider files-divider-0"></span>
          <span id="files-message"></span>
        </aside>
{footer(h('cross', 'Open'), h('circle', 'Back'), h('triangle', 'Use this folder', 'files-hint-use'), h('l1', 'Page', second='r1'))}
      </div>

      <!-- Settings > Video -->
      <div id="video-dialog" class="dialog"><div class="dialog-panel">
        {MODAL}
        <span class="dialog-title">Video</span>
        <span class="dialog-copy">Applies to the next game you start.</span>''')
    for i in range(3):
        add(f'        <span id="video-row-{i}" class="dialog-row dialog-row-{i}">{DLG}<span id="video-label-{i}" class="dialog-row-label"></span><span id="video-value-{i}" class="dialog-row-value"></span></span>')
    credit = S["credits"]
    add(f'''        <div class="dialog-hints">{h('updown', 'Select')}{h('leftright', 'Change')}{h('circle', 'Back')}</div>
      </div></div>

      <!-- Settings > Audio -->
      <div id="audio-dialog" class="dialog"><div class="dialog-panel">
        {MODAL}
        <span class="dialog-title">Audio</span>
        <span class="dialog-copy">The sound of your games; the PS5's own volume stays as it is.</span>
        <span id="audio-row-0" class="dialog-row dialog-row-0 focused">{DLG}<span class="dialog-row-label">Game volume</span><span id="audio-volume" class="dialog-row-value">100%</span></span>
        <div class="dialog-hints">{h('leftright', 'Change')}{h('circle', 'Back')}</div>
      </div></div>

      <!-- Settings > Diagnostics -->
      <div id="diagnostics-dialog" class="dialog"><div class="dialog-panel">
        {MODAL}
        <span class="dialog-title">Diagnostics</span>
        <span class="dialog-copy">Attach the log files when you report a problem.</span>
        <div id="setup-details" class="settings-details diagnostics-details"></div>
        <div class="dialog-hints">{h('circle', 'Back')}</div>
      </div></div>

      <!-- About -->
      <div id="about-dialog" class="library-screen about-screen">
        <span class="library-shade"></span>
        <span class="library-title">About {S["brand"]}</span>
        <span class="library-copy">Credits and setup</span>
        <section class="about-credit-panel">
          {PANEL}
          <span class="detail-kicker">PROJECT CREDITS</span>
          <span class="about-lead about-eden-lead">{credit[0]}</span>
          <span class="about-body about-eden">{credit[1]}</span>
          <span class="about-link about-eden-link">{credit[2]}</span>
          <span class="about-divider about-divider-vulkan"></span>
          <span class="about-subhead about-vulkan-head">THANKS</span>
          <span class="about-body about-vulkan">{credit[3]}</span>
          <span class="about-divider about-divider-port"></span>
          <span class="about-subhead about-port-head">PS5 EDITION</span>
          <span class="about-body about-port">{credit[4]}</span>
        </section>
        <aside class="about-setup-panel">
          {PANEL}
          <span class="detail-kicker">GETTING STARTED</span>
          <span class="about-lead">Supply your own files</span>''')
    for (label, value_id, value), cls in zip(S["setup"], ("about-keys", "about-firmware", "about-games")):
        add(f'          <span class="about-setup-label {cls}">{label}</span><span id="{value_id}" class="about-setup-path {cls}">{value}</span>')
    add(f'''          <span class="about-divider about-setup-divider"></span>
          <span class="about-note">{S["setup_note"]}</span>
        </aside>
{footer(h('circle', 'Back'))}
      </div>

      <!-- A dropdown's choices, over the right-hand panel -->
      <div id="picker">
        <div class="picker-panel">
          {MODAL}
          <span id="picker-kicker" class="detail-kicker"></span>
          <span id="picker-title" class="detail-title"></span>
          <div id="picker-viewport">
{rows(S, 'picker', 6, lambda i: f'<span id="picker-name-{i}" class="library-row-name picker-name"></span><span id="picker-mark-{i}" class="library-row-meta picker-mark"></span>')}
          </div>
          <div class="picker-hints">{h('cross', 'Choose')}{h('circle', 'Cancel')}</div>
          <span id="picker-position"></span>
        </div>
      </div>

      <!-- Starting a game -->
      <div id="loading-screen" class="library-screen">
        <span class="library-shade"></span>
        <img id="loading-cover" src="{S["cover"]}" width="288" height="288" alt=""/>
        <span id="loading-title"></span>
        <span id="loading-caption">Starting</span>
      </div>''')
    out.append(TAIL)
    return '\n'.join(out)


def start():
    """The two emulators side by side, each in its own colours."""
    out = [head(WIIU)]
    add = out.append
    add('''      <header id="start-header">
        <img id="start-brand-icon" src="icons/har-72.tga" width="72" height="72" alt=""/>
        <span id="start-brand-name">PS5CEMU-HAR</span>
        <span id="start-brand-copy">CEMU  +  AZAHAR  /  WII U AND 3DS ON PLAYSTATION 5</span>
        <span id="menu-clock">--:--</span>
      </header>
      <span id="start-divider"></span>''')
    for side, name, system, art, left in ((WIIU, "Cemu", "Wii U", "start-wiiu", 100), (N3DS, "Azahar", "Nintendo 3DS", "start-3ds", 1000)):
        c = side["chrome"]
        add(f'''      <section id="start-{side["id"]}" class="start-side start-{side["id"]}">
        <img class="start-chrome base-chrome" src="{c}/start-panel.tga" width="820" height="720" alt=""/>
        <img class="start-chrome focused-chrome" src="{c}/start-panel-focused.tga" width="820" height="720" alt=""/>
        <img class="start-art" src="icons/{art}.tga" width="440" height="440" alt=""/>
        <span class="start-name">{name}</span>
        <span class="start-system">{system.upper()}</span>
        <span id="start-status-{side["id"]}" class="start-status"></span>
        <span class="start-button"><img class="hero-button-chrome base-chrome" src="{c}/hero-button.tga" width="272" height="72" alt=""/><img class="hero-button-chrome focused-chrome" src="{c}/hero-button-focused.tga" width="272" height="72" alt=""/><span>Start {name}</span></span>
      </section>''')
    add(f'''      <footer id="start-footer" class="hints">{hint(WIIU, 'cross', 'Start')}{hint(WIIU, 'leftright', 'Choose')}</footer>''')
    out.append(TAIL)
    return '\n'.join(out)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, text in (("start.rml", start()), ("main.rml", launcher(WIIU)), ("azahar.rml", launcher(N3DS))):
        with open(os.path.join(out, name), "w", newline="\n") as file:
            file.write(text)


if __name__ == "__main__":
    main()
