// wifi_ctrl.cpp — WiFi Direct AP + embedded web control server.
//
// The ESP32 broadcasts its own Wi-Fi access point ("darkandstormy") so any phone,
// tablet, or laptop can connect and control the boat without a Raspberry Pi.
// This is the default mode at boot and serves as a useful bench-testing interface
// before the ELRS radio link is wired up.
//
// HTTP endpoints served on port 80:
//   GET  /           — embedded HTML control page (PROGMEM, no SD card needed)
//   GET  /map        — embedded GPS track viewer page
//   GET  /control    — accepts rudder/sail/throttle/arm query params from the control page
//   GET  /telemetry  — JSON: voltage, current, mAh, roll, pitch
//   GET  /status     — JSON: bilge wet, pump active, capsized
//   GET  /pump       — JSON: toggle bilge pump on/off
//   GET  /track      — JSON array of [lat, lng] pairs for the GPS track viewer
//   GET  /tiles/z/x/y.png — serves offline map tiles from the SD card
//   GET  /update     — OTA firmware update page (file picker)
//   POST /update     — OTA firmware upload; flashes .bin and reboots on success
//
// Control timeout:
//   If no /control request arrives within CTRL_TIMEOUT_MS (500 ms), the boat is
//   considered abandoned and all servo outputs go to neutral. This prevents
//   the boat from running away if the browser tab is closed or the phone locks.
//
// Mode switching:
//   The display touchscreen can switch from WiFi to ELRS mode. The switch is
//   requested by calling wifi_ctrl_set_mode() from the LVGL task and applied
//   in wifi_ctrl_update() on the main task (LVGL task does not own the AP lifecycle).

#include "wifi_ctrl.h"
#include "bilge.h"
#include "config.h"
#include "diag.h"
#include "imu.h"
#include "power.h"
#include "sdlog.h"
#include "servos.h"

#include <Arduino.h>
#include <SD.h>
#include <Update.h>
#include <WiFi.h>
#include <WebServer.h>

#ifdef GPS_ENABLED
#include "gps.h"
#endif

// ── Control frame timeout ─────────────────────────────────────────────────────
#define CTRL_TIMEOUT_MS 500

// ── GPS track buffer (GPS_ENABLED only) ──────────────────────────────────────
#ifdef GPS_ENABLED
struct GpsPt { float lat; float lng; };
static GpsPt         s_track[500];
static int           s_track_len     = 0;
static unsigned long s_track_last_ms = 0;
#endif

// ── Embedded control page ─────────────────────────────────────────────────────
static const char HTML_PAGE[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1, user-scalable=no">
  <title>Dark &amp; Stormy</title>
  <style>
    :root {
      --bg:      #0d1520;
      --surf:    #121e30;
      --surf2:   #1a2a40;
      --bdr:     #253650;
      --acc:     #3ecfcf;
      --acc-dim: rgba(62,207,207,0.15);
      --safe:    #3ecf82;
      --danger:  #e8573a;
      --warn:    #e8a83a;
      --text:    #dce8f0;
      --dim:     #5a7a96;
    }
    .light {
      --bg:      #edf2f7;
      --surf:    #ffffff;
      --surf2:   #dde6ef;
      --bdr:     #b8ccd8;
      --acc:     #1a8fa0;
      --acc-dim: rgba(26,143,160,0.12);
      --safe:    #1a8f55;
      --danger:  #c0391e;
      --warn:    #b07a10;
      --text:    #1a2530;
      --dim:     #5a7a96;
    }
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    html, body {
      height: 100%;
      background: var(--bg); color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      overscroll-behavior: none;
      user-select: none; -webkit-user-select: none;
      -webkit-tap-highlight-color: transparent;
    }
    #app {
      max-width: 480px; margin: 0 auto;
      display: flex; flex-direction: column;
      min-height: 100%; padding: 12px 12px 20px; gap: 10px;
    }

    /* ── HEADER ── */
    header { display: flex; align-items: center; gap: 8px; }
    .logo { font-size: 17px; font-weight: 800; letter-spacing: 0.05em; color: var(--acc); flex: 1; }
    .mode-lbl {
      font-size: 9px; font-weight: 700; letter-spacing: 0.08em;
      text-transform: uppercase; color: var(--dim);
      padding: 3px 8px; border: 1px solid var(--bdr); border-radius: 99px;
    }
    #status {
      font-size: 9px; font-weight: 700; letter-spacing: 0.07em; text-transform: uppercase;
      padding: 3px 10px; border-radius: 99px;
      border: 1px solid var(--danger); color: var(--danger);
      background: rgba(232,87,58,0.1); transition: all 0.2s;
    }
    #status.on { border-color: var(--safe); color: var(--safe); background: rgba(62,207,130,0.1); }

    /* ── HEARTBEAT BAR ── */
    #hb { height: 3px; background: var(--surf2); border-radius: 2px; overflow: hidden; }
    #hb-f { height: 100%; width: 0; background: var(--acc); border-radius: 2px; }

    /* ── CARD ── */
    .card { background: var(--surf); border: 1px solid var(--bdr); border-radius: 12px; padding: 11px 13px; }
    .clbl {
      font-size: 9px; font-weight: 700; letter-spacing: 0.09em;
      text-transform: uppercase; color: var(--dim); margin-bottom: 6px;
      display: block;
    }

    /* ── TELEM STRIP ── */
    #telem { display: flex; justify-content: space-between; align-items: center; padding: 8px 12px; }
    .tg { display: flex; flex-direction: column; align-items: center; gap: 2px; }
    .tg-l { font-size: 8px; font-weight: 700; letter-spacing: 0.09em; text-transform: uppercase; color: var(--dim); }
    .tg-v { font-size: 12px; font-weight: 700; font-variant-numeric: tabular-nums; color: var(--text); font-family: 'Courier New', monospace; }
    .tg-v.w { color: var(--warn); }
    .tg-v.d { color: var(--danger); }

    /* ── BILGE ── */
    #bilge {
      display: flex; align-items: center; gap: 8px;
      padding: 7px 12px; border-radius: 10px;
      background: var(--surf2); border: 1px solid var(--bdr); transition: all 0.3s;
    }
    #bilge.wet { background: rgba(232,87,58,0.1); border-color: var(--danger); }
    .bdot { width: 8px; height: 8px; border-radius: 50%; background: var(--safe); flex-shrink: 0; transition: all 0.3s; }
    #bilge.wet .bdot { background: var(--danger); box-shadow: 0 0 8px var(--danger); }
    #bilge-txt { font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.06em; color: var(--dim); flex: 1; transition: color 0.3s; }
    #bilge.wet #bilge-txt { color: var(--danger); }
    #pump-btn {
      font-size: 10px; font-weight: 700; padding: 4px 10px;
      border-radius: 6px; border: 1px solid var(--bdr);
      background: var(--surf2); color: var(--dim); cursor: pointer;
      text-transform: uppercase; letter-spacing: 0.06em;
    }
    #pump-btn.on { background: rgba(232,168,58,0.15); border-color: var(--warn); color: var(--warn); }

    /* ── CONTROLS ROW ── */
    .ctrl-row { display: flex; gap: 10px; align-items: flex-start; }

    /* ── THROTTLE ── */
    #t-card { display: flex; flex-direction: column; align-items: center; gap: 6px; }
    #t-track {
      position: relative; width: 44px; height: 158px;
      background: var(--surf2); border-radius: 22px;
      touch-action: none; cursor: ns-resize;
    }
    #t-track-line {
      position: absolute; left: 50%; top: 6%; bottom: 6%;
      width: 6px; transform: translateX(-50%);
      border-radius: 3px; background: var(--bdr);
    }
    #t-ctr-mark {
      position: absolute; left: 18%; right: 18%; top: 50%; height: 1px; background: var(--dim);
    }
    #t-fill {
      position: absolute; left: 50%; width: 6px; transform: translateX(-50%);
      border-radius: 3px; background: var(--acc); display: none;
    }
    #t-thumb {
      position: absolute; left: 50%; width: 34px; height: 34px;
      transform: translate(-50%, -50%); border-radius: 50%;
      background: var(--surf); border: 3px solid var(--acc);
      pointer-events: none;
    }
    #t-val { font-size: 11px; font-weight: 700; font-family: 'Courier New', monospace; color: var(--acc); }
    #t-lock {
      font-size: 9px; font-weight: 700; padding: 3px 8px; border-radius: 6px;
      border: 1px solid var(--bdr); background: var(--surf2); color: var(--dim);
      cursor: pointer; text-transform: uppercase; letter-spacing: 0.07em;
    }
    #t-lock.on { background: var(--acc-dim); border-color: var(--acc); color: var(--acc); }

    /* ── SAIL ARC ── */
    #s-card { flex: 1; display: flex; flex-direction: column; align-items: center; gap: 6px; }
    #sail-svg { touch-action: none; display: block; }
    .sail-btns { display: flex; gap: 6px; }
    .s-btn {
      flex: 1; padding: 6px 8px; border-radius: 7px;
      border: 1px solid var(--bdr); background: var(--surf2); color: var(--dim);
      font-size: 10px; font-weight: 700; cursor: pointer; font-family: inherit;
    }
    .s-btn:active { background: var(--acc); color: var(--bg); border-color: var(--acc); }
    #s-val { font-size: 11px; font-weight: 700; font-family: 'Courier New', monospace; color: var(--acc); }

    /* ── RUDDER ── */
    #r-card { display: flex; flex-direction: column; gap: 8px; }
    #r-header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 2px; }
    #r-pad {
      position: relative; width: 100%; height: 78px;
      background: var(--surf2); border-radius: 12px;
      border: 2px solid var(--bdr); touch-action: none;
      cursor: crosshair; overflow: hidden; transition: border-color 0.12s;
    }
    #r-pad.active { border-color: var(--acc); }
    #r-fill { position: absolute; top: 0; bottom: 0; background: var(--acc-dim); display: none; }
    #r-ctr-line { position: absolute; left: 50%; top: 12%; bottom: 12%; width: 1px; background: var(--bdr); transform: translateX(-50%); }
    #r-trim-tick { position: absolute; top: 0; bottom: 0; width: 2px; background: rgba(232,168,58,0.6); transform: translateX(-50%); display: none; }
    #r-thumb {
      position: absolute; top: 50%; width: 40px; height: 40px;
      transform: translate(-50%, -50%); border-radius: 50%;
      border: 3px solid var(--acc); background: var(--surf);
      pointer-events: none;
    }
    .r-lbl { position: absolute; bottom: 7px; font-size: 9px; font-weight: 700; color: var(--dim); text-transform: uppercase; letter-spacing: 0.07em; }
    #r-lbl-l { left: 10px; }
    #r-lbl-r { right: 10px; }
    #r-bigval {
      position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%);
      font-size: 20px; font-weight: 800; font-family: 'Courier New', monospace;
      color: var(--dim); pointer-events: none;
    }
    #r-bigval.active { color: var(--acc); }
    /* Trim buttons */
    #r-trim-row { display: flex; align-items: center; gap: 6px; }
    .trim-lbl { font-size: 9px; font-weight: 700; letter-spacing: 0.08em; text-transform: uppercase; color: var(--dim); }
    .trim-btn {
      padding: 4px 10px; border-radius: 6px; cursor: pointer;
      border: 1px solid var(--bdr); background: var(--surf2); color: var(--dim);
      font-size: 10px; font-weight: 700; font-family: inherit;
    }
    .trim-btn.sel { background: var(--acc-dim); border-color: var(--acc); color: var(--acc); }
    #r-trim-val { margin-left: auto; font-size: 11px; font-weight: 700; font-family: 'Courier New', monospace; color: var(--dim); }

    /* ── ARM / STOP ── */
    .action-row { display: flex; gap: 8px; }
    .act-btn {
      flex: 1; padding: 14px; border: 2px solid transparent;
      border-radius: 10px; font-size: 13px; font-weight: 800;
      letter-spacing: 0.06em; text-transform: uppercase;
      cursor: pointer; font-family: inherit; transition: opacity 0.1s;
    }
    .act-btn:active { opacity: 0.72; }
    #btn-arm { background: var(--surf2); color: var(--dim); border-color: var(--bdr); }
    #btn-arm.armed { background: rgba(62,207,130,0.12); border-color: var(--safe); color: var(--safe); }
    #btn-stop { flex: 1.4; background: var(--danger); color: #fff; font-size: 14px; border-color: transparent; }

    /* ── TAB BAR ── */
    #tab-bar { display:flex; gap:2px; background:var(--surf); border-radius:10px;
               padding:3px; border:1px solid var(--bdr); }
    .tab { flex:1; padding:8px 4px; border:none; border-radius:8px; cursor:pointer;
           font-family:inherit; font-size:11px; font-weight:700;
           background:transparent; color:var(--dim); transition:all 0.15s; }
    .tab.active { background:var(--bg); color:var(--acc);
                  box-shadow:0 1px 3px rgba(0,0,0,0.2); }

    /* ── PAGES ── */
    #page-ctrl, #page-map, #page-instr, #page-dev { display:flex; flex-direction:column; gap:10px; }
    #page-map, #page-instr, #page-dev { display:none; }

    /* ── INSTRUMENTS ── */
    .gauge-grid { display:grid; grid-template-columns:1fr 1fr; gap:8px; }
    .gauge { background:var(--surf); border:1px solid var(--bdr); border-radius:10px; padding:9px 11px; display:flex; flex-direction:column; gap:3px; }
    .gauge.w { border-color:var(--warn); }
    .gauge.d { border-color:var(--danger); }
    .g-lbl { font-size:8px; font-weight:700; letter-spacing:0.09em; text-transform:uppercase; color:var(--dim); }
    .g-val { font-size:18px; font-weight:700; font-family:'Courier New',monospace; color:var(--acc); }
    .g-val.w { color:var(--warn); }
    .g-val.d { color:var(--danger); }
    .g-sub { font-size:9px; font-family:'Courier New',monospace; color:var(--dim); }
    .g-bar { height:3px; background:var(--surf2); border-radius:2px; margin-top:2px; }
    .g-bar-fill { height:100%; border-radius:2px; background:var(--acc); transition:width 0.3s; }
    .g-bar-fill.w { background:var(--warn); }
    .g-bar-fill.d { background:var(--danger); }

    /* ── DEVICES ── */
    .dev-group-lbl { font-size:8px; font-weight:700; letter-spacing:0.09em; text-transform:uppercase; color:var(--dim); margin-bottom:2px; }
    .dev-grid { display:grid; grid-template-columns:1fr 1fr; gap:6px; margin-bottom:10px; }
    .dev-card { border-radius:10px; padding:9px 11px; display:flex; flex-direction:column; gap:3px; }
    .dev-card.ok     { background:rgba(62,207,130,0.08);  border:1px solid var(--safe); }
    .dev-card.warn   { background:rgba(232,168,58,0.08);  border:1px solid var(--warn); }
    .dev-card.error  { background:rgba(232,87,58,0.08);   border:1px solid var(--danger); }
    .dev-card.absent { background:var(--surf2);            border:1px solid var(--bdr); }
    .dev-name { font-size:11px; font-weight:700; color:var(--text); }
    .dev-detail { font-size:9px; font-family:'Courier New',monospace; color:var(--dim); }
    .dev-note { font-size:10px; color:var(--text); opacity:0.75; }
    .dev-status { font-size:9px; font-weight:700; letter-spacing:0.07em; text-transform:uppercase; }
    .dev-status.ok     { color:var(--safe); }
    .dev-status.warn   { color:var(--warn); }
    .dev-status.error  { color:var(--danger); }
    .dev-status.absent { color:var(--dim); }
    .dev-dot { width:7px; height:7px; border-radius:50%; display:inline-block; margin-right:5px; flex-shrink:0; }
    .dev-dot.ok     { background:var(--safe); box-shadow:0 0 5px var(--safe); }
    .dev-dot.warn   { background:var(--warn); }
    .dev-dot.error  { background:var(--danger); }
    .dev-dot.absent { background:var(--dim); }
    .repair-btn {
      margin-top:3px; padding:4px 0; border-radius:6px; cursor:pointer;
      font-size:10px; font-weight:700; text-transform:uppercase;
      letter-spacing:0.05em; width:100%; font-family:inherit;
      border:1px solid var(--bdr); background:var(--surf2); color:var(--dim);
      transition:all 0.2s;
    }
    .repair-btn.warn  { border-color:var(--warn);   background:rgba(232,168,58,0.1);  color:var(--warn); }
    .repair-btn.error { border-color:var(--danger); background:rgba(232,87,58,0.1);   color:var(--danger); }
    .repair-btn.absent{ border-color:var(--acc);    background:var(--acc-dim);         color:var(--acc); }
    .dev-legend { display:flex; gap:12px; justify-content:center; padding:8px 12px;
      background:var(--surf); border-radius:8px; border:1px solid var(--bdr); }
    .dev-legend span { font-size:10px; color:var(--dim); display:flex; align-items:center; gap:4px; }

    #theme-btn {
      background: var(--surf2); border: 1px solid var(--bdr); color: var(--dim);
      font-size: 9px; font-weight: 700; padding: 3px 8px; border-radius: 6px;
      cursor: pointer; text-transform: uppercase; letter-spacing: 0.07em; font-family: inherit;
    }
  </style>
</head>
<body>
<div id="app">

  <header>
    <span class="logo">&#9973; Dark &amp; Stormy</span>
    <button id="theme-btn" onclick="toggleTheme()">&#9728; Day</button>
    <div id="status">Offline</div>
  </header>

  <div id="hb"><div id="hb-f"></div></div>

  <!-- Tab bar -->
  <div id="tab-bar">
    <button class="tab active" id="tab-ctrl"  onclick="switchTab('ctrl')">Control</button>
    <button class="tab"        id="tab-instr" onclick="switchTab('instr')">Instr</button>
    <button class="tab"        id="tab-map"   onclick="switchTab('map')">Map</button>
    <button class="tab"        id="tab-dev"   onclick="switchTab('dev')">Devices</button>
  </div>

  <!-- CONTROL PAGE -->
  <div id="page-ctrl">
  <!-- Telem strip -->
  <div class="card" id="telem">
    <div class="tg"><div class="tg-l">Battery</div><div class="tg-v" id="tl-v">--.-V</div><div class="tg-v" style="font-size:10px" id="tl-a">-.-A</div></div>
    <div class="tg"><div class="tg-l">GPS</div><div class="tg-v" id="tl-s">--sat</div><div class="tg-v" style="font-size:10px" id="tl-spd">--km/h</div></div>
    <div class="tg"><div class="tg-l">Attitude</div><div class="tg-v" id="tl-r">R--&#176;</div><div class="tg-v" style="font-size:10px" id="tl-p">P--&#176;</div></div>
    <div class="tg"><div class="tg-l">Temp</div><div class="tg-v" id="tl-t">--&#176;C</div></div>
  </div>

  <!-- Bilge -->
  <div id="bilge">
    <div class="bdot"></div>
    <span id="bilge-txt">Bilge OK</span>
    <button id="pump-btn" onclick="togglePump()">Pump: OFF</button>
  </div>

  <!-- Controls row -->
  <div class="ctrl-row">
    <!-- Throttle -->
    <div class="card" id="t-card">
      <span class="clbl">Throttle</span>
      <div id="t-track">
        <div id="t-track-line"></div>
        <div id="t-ctr-mark"></div>
        <div id="t-fill"></div>
        <div id="t-thumb" style="top:50%"></div>
      </div>
      <div id="t-val">+0%</div>
      <button id="t-lock" onclick="toggleThrottleLock()">&#8617; Return</button>
    </div>

    <!-- Sail arc -->
    <div class="card" id="s-card">
      <span class="clbl">Sail Trim</span>
      <svg id="sail-svg" viewBox="0 0 152 152" width="148" height="148">
        <path fill="rgba(62,207,207,0.08)" d="M 18,18 L 138,18 A 120,120 0 0,1 18,138 Z"/>
        <path fill="none" stroke="#253650" stroke-width="2.5" stroke-linecap="round"
              stroke-dasharray="8 5" d="M 138,18 A 120,120 0 0,1 18,138"/>
        <path id="s-sail" stroke="#3ecfcf" stroke-width="1.8" fill="rgba(62,207,207,0.18)"/>
        <line id="s-boom" stroke="#5a7a96" stroke-width="1.8" stroke-linecap="round"/>
        <circle cx="18" cy="18" r="5" fill="#dce8f0"/>
        <circle id="s-thumb" cx="138" cy="18" r="11" fill="#3ecfcf" fill-opacity="0.9"
                style="cursor:grab;filter:drop-shadow(0 0 6px #3ecfcf)"/>
        <text x="142" y="22" font-size="8" fill="#5a7a96">out</text>
        <text x="18" y="150" font-size="8" text-anchor="middle" fill="#5a7a96">in</text>
      </svg>
      <div class="sail-btns">
        <button class="s-btn" id="s-trim">&#9668; Trim</button>
        <button class="s-btn" id="s-ease">Ease &#9658;</button>
      </div>
      <div id="s-val">0% trimmed</div>
    </div>
  </div>

  <!-- Rudder -->
  <div class="card" id="r-card">
    <div id="r-header">
      <span class="clbl" style="margin-bottom:0">Rudder</span>
    </div>
    <div id="r-pad">
      <div id="r-fill"></div>
      <div id="r-ctr-line"></div>
      <div id="r-trim-tick"></div>
      <div id="r-thumb"></div>
      <span class="r-lbl" id="r-lbl-l">&#9668; Port</span>
      <span class="r-lbl" id="r-lbl-r">Stbd &#9658;</span>
      <div id="r-bigval">0</div>
    </div>
    <div id="r-trim-row">
      <span class="trim-lbl">Trim:</span>
      <button class="trim-btn" id="trim-n1" onclick="setTrim(-1)">&#8722;1%</button>
      <button class="trim-btn sel" id="trim-0"  onclick="setTrim(0)">CTR</button>
      <button class="trim-btn" id="trim-p1" onclick="setTrim(1)">+1%</button>
      <span id="r-trim-val">+0%</span>
    </div>
  </div>

  <!-- Arm / Stop -->
  <div class="action-row">
    <button class="act-btn" id="btn-arm">Disarmed</button>
    <button class="act-btn" id="btn-stop">&#9632; Stop</button>
  </div>
  </div><!-- /page-ctrl -->

  <!-- MAP PAGE -->
  <div id="page-map" style="display:none">
    <!-- GPS fix strip -->
    <div class="card" id="gps-strip">
      <div style="display:flex;justify-content:space-between;align-items:center">
        <div class="tg"><div class="tg-l">Lat</div><div class="tg-v" id="m-lat">--&#176;</div></div>
        <div class="tg"><div class="tg-l">Lon</div><div class="tg-v" id="m-lon">--&#176;</div></div>
        <div class="tg"><div class="tg-l">Alt</div><div class="tg-v" id="m-alt">--m</div></div>
        <div class="tg"><div class="tg-l">Hdg</div><div class="tg-v" id="m-hdg">--&#176;</div></div>
        <div class="tg"><div class="tg-l">Spd</div><div class="tg-v" id="m-spd">--</div></div>
        <div class="tg"><div class="tg-l">Sats</div><div class="tg-v" id="m-sats">--</div></div>
      </div>
    </div>
    <!-- Track canvas -->
    <div class="card" style="padding:0;overflow:hidden;position:relative">
      <canvas id="track-cv" style="display:block;width:100%;background:var(--surf2)"></canvas>
      <div id="map-overlay" style="position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);
        font-size:12px;color:var(--dim);text-align:center;pointer-events:none">No GPS fix</div>
      <canvas id="tile-cv" style="position:absolute;inset:0;width:100%;pointer-events:none"></canvas>
    </div>
    <div style="display:flex;gap:8px">
      <button class="act-btn" style="background:var(--surf2);color:var(--dim);border:1px solid var(--bdr);font-size:11px;padding:10px" onclick="loadTrack()">&#8635; Refresh</button>
      <button class="act-btn" style="background:var(--surf2);color:var(--dim);border:1px solid var(--bdr);font-size:11px;padding:10px" id="clear-btn" onclick="clearTrack()">&#10005; Clear</button>
    </div>
  </div><!-- /page-map -->

  <!-- INSTRUMENTS PAGE -->
  <div id="page-instr">
    <!-- Attitude horizon -->
    <div class="card" style="display:flex;flex-direction:column;align-items:center;gap:10px;padding:14px">
      <div class="g-lbl">Attitude</div>
      <svg id="att-svg" viewBox="-60 -60 120 120" width="180" height="180">
        <defs><clipPath id="att-clip"><circle cx="0" cy="0" r="52"/></clipPath></defs>
        <g id="att-horizon" clip-path="url(#att-clip)">
          <rect id="att-sky"   x="-60" y="-120" width="120" height="120" fill="rgba(62,207,207,0.18)"/>
          <rect id="att-gnd"  x="-60" y="0"    width="120" height="120" fill="rgba(120,80,40,0.28)"/>
          <line id="att-hor"  x1="-60" y1="0" x2="60" y2="0" stroke="#3ecfcf" stroke-width="1.5"/>
          <line x1="-54" y1="-15" x2="-27" y2="-15" stroke="rgba(220,232,240,0.3)" stroke-width="0.5"/>
          <line x1="-54" y1="-30" x2="-27" y2="-30" stroke="rgba(220,232,240,0.3)" stroke-width="0.5"/>
          <line x1="27"  y1="-15" x2="54"  y2="-15" stroke="rgba(220,232,240,0.3)" stroke-width="0.5"/>
          <line x1="27"  y1="-30" x2="54"  y2="-30" stroke="rgba(220,232,240,0.3)" stroke-width="0.5"/>
          <line x1="-54" y1="15"  x2="-27" y2="15"  stroke="rgba(220,232,240,0.3)" stroke-width="0.5"/>
          <line x1="27"  y1="15"  x2="54"  y2="15"  stroke="rgba(220,232,240,0.3)" stroke-width="0.5"/>
        </g>
        <circle cx="0" cy="0" r="52" fill="none" stroke="#253650" stroke-width="2"/>
        <line x1="-16" y1="0" x2="-5" y2="0" stroke="#dce8f0" stroke-width="2.5" stroke-linecap="round"/>
        <line x1="5"   y1="0" x2="16" y2="0" stroke="#dce8f0" stroke-width="2.5" stroke-linecap="round"/>
        <circle cx="0" cy="0" r="2.5" fill="#3ecfcf"/>
        <polygon id="att-ptr" points="0,-48 -3,-42 3,-42" fill="#3ecfcf"/>
      </svg>
      <div style="display:flex;gap:20px">
        <div style="text-align:center"><div class="g-lbl">Roll</div><div class="g-val" id="att-roll" style="font-size:15px">--&#176;</div></div>
        <div style="text-align:center"><div class="g-lbl">Pitch</div><div class="g-val" id="att-pitch" style="font-size:15px">--&#176;</div></div>
        <div style="text-align:center"><div class="g-lbl">Heading</div><div class="g-val" id="att-hdg" style="font-size:15px">--&#176;</div></div>
      </div>
    </div>
    <!-- Gauge grid -->
    <div class="gauge-grid">
      <div class="card gauge" id="g-batt">
        <div class="g-lbl">Battery</div>
        <div class="g-val" id="g-v">--.-V</div>
        <div class="g-sub" id="g-a">-.-A</div>
        <div class="g-bar"><div class="g-bar-fill" id="g-v-bar" style="width:0%"></div></div>
      </div>
      <div class="card gauge" id="g-charge">
        <div class="g-lbl">Charge</div>
        <div class="g-val" id="g-pct">--%</div>
        <div class="g-sub" id="g-mah">-- mAh</div>
        <div class="g-bar"><div class="g-bar-fill" id="g-pct-bar" style="width:0%"></div></div>
      </div>
      <div class="card gauge" id="g-temp">
        <div class="g-lbl">MCU Temp</div>
        <div class="g-val" id="g-temp-v">--&#176;C</div>
        <div class="g-sub">ESP32-S3</div>
        <div class="g-bar"><div class="g-bar-fill" id="g-temp-bar" style="width:0%"></div></div>
      </div>
      <div class="card gauge" id="g-bilge">
        <div class="g-lbl">Bilge</div>
        <div class="g-val" id="g-bilge-v" style="font-size:15px">DRY</div>
        <div class="g-sub" id="g-pump-v">Pump off</div>
      </div>
    </div>
  </div><!-- /page-instr -->

  <!-- DEVICES PAGE -->
  <div id="page-dev">
    <div style="display:flex;align-items:center;gap:8px">
      <span class="g-lbl" style="flex:1">Device Status</span>
      <button class="repair-btn absent" style="width:auto;padding:5px 12px" id="reprobe-all-btn" onclick="reprobeAll()">&#8635; Reprobe All</button>
    </div>

    <div class="dev-group-lbl">I&#178;C Bus</div>
    <div class="dev-grid">
      <div class="dev-card ok" id="dev-qmi">
        <div style="display:flex;align-items:center"><span class="dev-dot ok"></span><span class="dev-name">IMU</span><span class="dev-status ok" style="margin-left:auto">OK</span></div>
        <div class="dev-detail">QMI8658 &middot; 0x6B</div>
        <div class="dev-note">62.5 Hz</div>
      </div>
      <div class="dev-card ok" id="dev-pca">
        <div style="display:flex;align-items:center"><span class="dev-dot ok"></span><span class="dev-name">Servo Driver</span><span class="dev-status ok" style="margin-left:auto">OK</span></div>
        <div class="dev-detail">PCA9685 &middot; 0x40</div>
        <div class="dev-note">50 Hz PWM</div>
      </div>
      <div class="dev-card ok" id="dev-ina">
        <div style="display:flex;align-items:center"><span class="dev-dot ok"></span><span class="dev-name">Current Sensor</span><span class="dev-status ok" style="margin-left:auto">OK</span></div>
        <div class="dev-detail">INA228 &middot; 0x41</div>
        <div class="dev-note" id="dev-ina-note">--V &middot; --A</div>
      </div>
      <div class="dev-card ok" id="dev-ft">
        <div style="display:flex;align-items:center"><span class="dev-dot ok"></span><span class="dev-name">Touch</span><span class="dev-status ok" style="margin-left:auto">OK</span></div>
        <div class="dev-detail">FT3168 &middot; 0x38</div>
        <div class="dev-note">Onboard</div>
      </div>
    </div>

    <div class="dev-group-lbl">Actuators</div>
    <div class="dev-grid">
      <div class="dev-card ok" id="dev-rudder">
        <div style="display:flex;align-items:center"><span class="dev-dot ok"></span><span class="dev-name">Rudder</span><span class="dev-status ok" style="margin-left:auto">OK</span></div>
        <div class="dev-detail">PCA9685 Ch 0</div>
        <div class="dev-note">7.5 kg servo</div>
      </div>
      <div class="dev-card ok" id="dev-winch">
        <div style="display:flex;align-items:center"><span class="dev-dot ok"></span><span class="dev-name">Sail Winch</span><span class="dev-status ok" style="margin-left:auto">OK</span></div>
        <div class="dev-detail">PCA9685 Ch 1</div>
        <div class="dev-note">HS-785HB</div>
      </div>
      <div class="dev-card ok" id="dev-esc">
        <div style="display:flex;align-items:center"><span class="dev-dot ok"></span><span class="dev-name">ESC</span><span class="dev-status ok" style="margin-left:auto">OK</span></div>
        <div class="dev-detail">PCA9685 Ch 2</div>
        <div class="dev-note">Quicrun 1060</div>
      </div>
      <div class="dev-card ok" id="dev-pump">
        <div style="display:flex;align-items:center"><span class="dev-dot ok"></span><span class="dev-name">Bilge Pump</span><span class="dev-status ok" style="margin-left:auto">OK</span></div>
        <div class="dev-detail">MOSFET &middot; GPIO3</div>
        <div class="dev-note" id="dev-pump-note">Off</div>
      </div>
    </div>

    <div class="dev-group-lbl">Sensors</div>
    <div class="dev-grid">
      <div class="dev-card ok" id="dev-bilge">
        <div style="display:flex;align-items:center"><span class="dev-dot ok"></span><span class="dev-name">Bilge Sensor</span><span class="dev-status ok" style="margin-left:auto">OK</span></div>
        <div class="dev-detail">Float &middot; GPIO2</div>
        <div class="dev-note" id="dev-bilge-note">Dry</div>
      </div>
      <div class="dev-card ok" id="dev-sd">
        <div style="display:flex;align-items:center"><span class="dev-dot ok"></span><span class="dev-name">SD Card</span><span class="dev-status ok" style="margin-left:auto">OK</span></div>
        <div class="dev-detail">SPI3 &middot; GPIO38&#8211;41</div>
        <div class="dev-note">Logging</div>
      </div>
      <div class="dev-card absent" id="dev-gps">
        <div style="display:flex;align-items:center"><span class="dev-dot absent"></span><span class="dev-name">GPS</span><span class="dev-status absent" style="margin-left:auto">N/A</span></div>
        <div class="dev-detail">BN-880 &middot; UART2</div>
        <div class="dev-note">GPS_ENABLED build</div>
        <button class="repair-btn absent" onclick="repairDev('gps',this)">&#8635; Repair</button>
      </div>
      <div class="dev-card absent" id="dev-compass">
        <div style="display:flex;align-items:center"><span class="dev-dot absent"></span><span class="dev-name">Compass</span><span class="dev-status absent" style="margin-left:auto">N/A</span></div>
        <div class="dev-detail">HMC5883L &middot; 0x1E</div>
        <div class="dev-note">COMPASS_ENABLED</div>
        <button class="repair-btn absent" onclick="repairDev('compass',this)">&#8635; Repair</button>
      </div>
    </div>

    <div class="dev-group-lbl">Radio</div>
    <div class="dev-grid">
      <div class="dev-card absent" id="dev-elrs">
        <div style="display:flex;align-items:center"><span class="dev-dot absent"></span><span class="dev-name">ELRS RX</span><span class="dev-status absent" style="margin-left:auto">N/A</span></div>
        <div class="dev-detail">RP3 &middot; UART1</div>
        <div class="dev-note">ELRS mode only</div>
        <button class="repair-btn absent" onclick="repairDev('elrs',this)">&#8635; Repair</button>
      </div>
      <div class="dev-card ok" id="dev-wifi">
        <div style="display:flex;align-items:center"><span class="dev-dot ok"></span><span class="dev-name">WiFi AP</span><span class="dev-status ok" style="margin-left:auto">OK</span></div>
        <div class="dev-detail">darkandstormy</div>
        <div class="dev-note">192.168.4.1</div>
      </div>
    </div>

    <div class="dev-legend">
      <span><span class="dev-dot ok"></span>OK</span>
      <span><span class="dev-dot warn"></span>Warn</span>
      <span><span class="dev-dot error"></span>Fault</span>
      <span><span class="dev-dot absent"></span>N/A</span>
    </div>
  </div><!-- /page-dev -->

</div><!-- #app -->

<script>
// ── STATE ──────────────────────────────────────────────────────────
var rudder   = 0;
var rudderTrim = 0;
var sail     = 0;
var throttle = 0;
var tLocked  = false;
var armed    = false;
var pumpOn   = false;
var darkMode = true;

// ── THEME ──────────────────────────────────────────────────────────
function toggleTheme() {
  darkMode = !darkMode;
  document.body.classList.toggle('light', !darkMode);
  document.getElementById('theme-btn').textContent = darkMode ? '☀ Day' : '🌙 Night';
}

// ── THROTTLE ──────────────────────────────────────────────────────
var tTrack = document.getElementById('t-track');
var tThumb = document.getElementById('t-thumb');
var tFill  = document.getElementById('t-fill');
var tVal   = document.getElementById('t-val');
var tDrag  = false;

function renderThrottle() {
  var pct = (1 - (throttle + 1) / 2) * 100;
  tThumb.style.top = pct + '%';
  tVal.textContent = (throttle >= 0 ? '+' : '') + Math.round(throttle * 100) + '%';
  if (Math.abs(throttle) > 0.01) {
    tFill.style.display = 'block';
    if (throttle > 0) {
      tFill.style.top = pct + '%';
      tFill.style.height = (throttle * 50) + '%';
    } else {
      tFill.style.top = '50%';
      tFill.style.height = (Math.abs(throttle) * 50) + '%';
    }
  } else {
    tFill.style.display = 'none';
  }
}
renderThrottle();

function tGetVal(e) {
  var r = tTrack.getBoundingClientRect();
  return (1 - Math.max(0, Math.min(1, (e.clientY - r.top) / r.height))) * 2 - 1;
}
tTrack.addEventListener('pointerdown', function(e) {
  tDrag = true; tTrack.setPointerCapture(e.pointerId);
  throttle = tGetVal(e); renderThrottle();
});
tTrack.addEventListener('pointermove', function(e) {
  if (!tDrag) return; throttle = tGetVal(e); renderThrottle();
});
function tRelease() {
  tDrag = false;
  if (!tLocked) { throttle = 0; renderThrottle(); }
}
tTrack.addEventListener('pointerup', tRelease);
tTrack.addEventListener('pointercancel', tRelease);

function toggleThrottleLock() {
  tLocked = !tLocked;
  throttle = 0; renderThrottle();
  var btn = document.getElementById('t-lock');
  btn.textContent = tLocked ? '🔒 Lock' : '↩ Return';
  btn.className   = tLocked ? 'on' : '';
}

// ── SAIL ARC ──────────────────────────────────────────────────────
var CX = 18, CY = 18, R = 120;
var svg   = document.getElementById('sail-svg');
var sSail = document.getElementById('s-sail');
var sBoom = document.getElementById('s-boom');
var sThmb = document.getElementById('s-thumb');
var sVal  = document.getElementById('s-val');
var sDrag = false;

function renderSail() {
  var a  = sail * Math.PI / 2;
  var px = CX + R * Math.cos(a);
  var py = CY + R * Math.sin(a);
  var mx = (CX + px) / 2, my = (CY + py) / 2;
  var cx2 = mx + 20 * Math.sin(a), cy2 = my - 20 * Math.cos(a);
  sSail.setAttribute('d', 'M '+CX+','+CY+' Q '+cx2.toFixed(1)+','+cy2.toFixed(1)+' '+px.toFixed(1)+','+py.toFixed(1)+' Z');
  sBoom.setAttribute('x1',CX); sBoom.setAttribute('y1',CY);
  sBoom.setAttribute('x2',px.toFixed(1)); sBoom.setAttribute('y2',py.toFixed(1));
  sThmb.setAttribute('cx',px.toFixed(1)); sThmb.setAttribute('cy',py.toFixed(1));
  sVal.textContent = Math.round(sail * 100) + '% trimmed';
}
renderSail();

function sValFromEvt(e) {
  var rect = svg.getBoundingClientRect();
  var dx = Math.max(0, (e.clientX - rect.left) * (152/rect.width) - CX);
  var dy = Math.max(0, (e.clientY - rect.top)  * (152/rect.height) - CY);
  return Math.min(Math.PI/2, Math.atan2(dy, dx)) / (Math.PI/2);
}
svg.addEventListener('pointerdown', function(e) {
  sDrag = true; svg.setPointerCapture(e.pointerId);
  sail = sValFromEvt(e); renderSail();
});
svg.addEventListener('pointermove', function(e) { if (sDrag) { sail = sValFromEvt(e); renderSail(); } });
svg.addEventListener('pointerup',     function() { sDrag = false; });
svg.addEventListener('pointercancel', function() { sDrag = false; });
document.getElementById('s-trim').addEventListener('click', function() { sail = Math.min(1, sail + 1/18); renderSail(); });
document.getElementById('s-ease').addEventListener('click', function() { sail = Math.max(0, sail - 1/18); renderSail(); });

// ── RUDDER ────────────────────────────────────────────────────────
var rPad   = document.getElementById('r-pad');
var rThumb = document.getElementById('r-thumb');
var rFill  = document.getElementById('r-fill');
var rBigv  = document.getElementById('r-bigval');
var rTrimTick = document.getElementById('r-trim-tick');
var rDrag  = false;

function renderRudder() {
  var pct = (0.5 + rudder * 0.42) * 100;
  rThumb.style.left = pct + '%';
  rBigv.textContent = (rudder >= 0 ? '+' : '') + Math.round(rudder * 100);
  var lo = Math.min(rudder, rudderTrim);
  var hi = Math.max(rudder, rudderTrim);
  if (Math.abs(rudder - rudderTrim) > 0.01) {
    rFill.style.display = 'block';
    rFill.style.left    = ((0.5 + lo * 0.42) * 100) + '%';
    rFill.style.right   = (100 - (0.5 + hi * 0.42) * 100) + '%';
  } else {
    rFill.style.display = 'none';
  }
  if (Math.abs(rudderTrim) > 0.01) {
    rTrimTick.style.display = 'block';
    rTrimTick.style.left = ((0.5 + rudderTrim * 0.42) * 100) + '%';
  } else {
    rTrimTick.style.display = 'none';
  }
}
renderRudder();

function rGetVal(e) {
  var rect = rPad.getBoundingClientRect();
  return (Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width)) - 0.5) * 2;
}
rPad.addEventListener('pointerdown', function(e) {
  rDrag = true; rPad.setPointerCapture(e.pointerId);
  rPad.classList.add('active'); rBigv.classList.add('active');
  rudder = rGetVal(e); renderRudder();
});
rPad.addEventListener('pointermove', function(e) { if (rDrag) { rudder = rGetVal(e); renderRudder(); } });
function rRelease() {
  rDrag = false; rPad.classList.remove('active'); rBigv.classList.remove('active');
  rudder = rudderTrim; renderRudder();
}
rPad.addEventListener('pointerup',     rRelease);
rPad.addEventListener('pointercancel', rRelease);

function setTrim(val) {
  if (val === 0) {
    rudderTrim = 0;
  } else {
    rudderTrim = Math.max(-1, Math.min(1, rudderTrim + (val === -1 ? -0.01 : 0.01)));
  }
  rudder = rudderTrim;
  renderRudder();
  var pct = (rudderTrim * 100).toFixed(0);
  document.getElementById('r-trim-val').textContent = (rudderTrim >= 0 ? '+' : '') + pct + '%';
  document.getElementById('trim-n1').className = 'trim-btn';
  document.getElementById('trim-0').className  = 'trim-btn' + (rudderTrim === 0 ? ' sel' : '');
  document.getElementById('trim-p1').className = 'trim-btn';
}

// ── ARM / STOP ────────────────────────────────────────────────────
var btnArm = document.getElementById('btn-arm');
btnArm.addEventListener('click', function() {
  armed = !armed;
  btnArm.textContent = armed ? '⚓ Armed' : 'Disarmed';
  btnArm.className = 'act-btn' + (armed ? ' armed' : '');
});
document.getElementById('btn-stop').addEventListener('click', function() {
  armed = false; throttle = 0; renderThrottle();
  btnArm.textContent = 'Disarmed'; btnArm.className = 'act-btn';
});

// ── PUMP ──────────────────────────────────────────────────────────
function togglePump() {
  pumpOn = !pumpOn;
  var btn = document.getElementById('pump-btn');
  btn.textContent = 'Pump: ' + (pumpOn ? 'ON' : 'OFF');
  btn.className   = pumpOn ? 'on' : '';
  fetch('/pump?on=' + (pumpOn ? 1 : 0)).catch(function(){});
}

// ── CONTROL POLLING ───────────────────────────────────────────────
var hbF = document.getElementById('hb-f');
var statusEl = document.getElementById('status');
setInterval(function() {
  var r = Math.round(rudder   * 100);
  var s = Math.round(sail     * 100);
  var t = armed ? Math.round(throttle * 100) : 0;
  fetch('/control?r='+r+'&s='+s+'&t='+t+'&a='+(armed?1:0))
    .then(function() {
      hbF.style.transition = 'none';
      hbF.style.width = '100%';
      setTimeout(function(){
        hbF.style.transition = 'width 0.4s ease';
        hbF.style.width = '0';
      }, 80);
      statusEl.textContent = 'Online'; statusEl.className = 'on';
    })
    .catch(function() {
      hbF.style.transition = 'none';
      hbF.style.width = '0';
      statusEl.textContent = 'Offline'; statusEl.className = ''; });
}, 50);

// ── TAB SWITCHING ─────────────────────────────────────────────────
var TAB_IDS = ['ctrl','instr','map','dev'];
function switchTab(name) {
  TAB_IDS.forEach(function(id) {
    document.getElementById('page-'+id).style.display = name===id ? 'flex' : 'none';
    document.getElementById('tab-'+id).className = 'tab' + (name===id ? ' active' : '');
  });
  if (name==='map')   { resizeCanvases(); loadTrack(); }
  if (name==='instr') { updateInstruments(); }
  if (name==='dev')   { updateDevices(); }
}


// ── MAP / TRACK ───────────────────────────────────────────────────
var trackPts  = [];
var mapZoom   = 16;
var mapCenter = null;

var trackCv  = document.getElementById('track-cv');
var tileCv   = document.getElementById('tile-cv');
var tCtx     = trackCv.getContext('2d');
var tileCtx  = tileCv.getContext('2d');
var overlay  = document.getElementById('map-overlay');

function resizeCanvases() {
  var w = trackCv.offsetWidth;
  trackCv.width  = w; trackCv.height = 300;
  tileCv.width   = w; tileCv.height  = 300;
}

function latLngToTile(lat, lng, z) {
  var n = Math.pow(2, z);
  var x = (lng + 180) / 360 * n;
  var latR = lat * Math.PI / 180;
  var y = (1 - Math.log(Math.tan(latR) + 1/Math.cos(latR)) / Math.PI) / 2 * n;
  return { x: x, y: y };
}

function drawTiles() {
  if (!mapCenter) return;
  var w = tileCv.width, h = tileCv.height;
  tileCtx.clearRect(0, 0, w, h);
  var center = latLngToTile(mapCenter.lat, mapCenter.lng, mapZoom);
  var tileSize = 256;
  var cxPx = w/2, cyPx = h/2;
  var tilesX = Math.ceil(w / tileSize) + 2;
  var tilesY = Math.ceil(h / tileSize) + 2;
  var startX = Math.floor(center.x) - Math.floor(tilesX/2);
  var startY = Math.floor(center.y) - Math.floor(tilesY/2);
  for (var tx = startX; tx <= startX + tilesX; tx++) {
    for (var ty = startY; ty <= startY + tilesY; ty++) {
      (function(tx, ty) {
        var img = new Image();
        img.onload = function() {
          var dx = (tx - center.x) * tileSize + cxPx;
          var dy = (ty - center.y) * tileSize + cyPx;
          tileCtx.drawImage(img, Math.round(dx), Math.round(dy), tileSize, tileSize);
          drawTrackOnCanvas();
        };
        img.src = '/tiles/' + mapZoom + '/' + tx + '/' + ty + '.png';
      })(tx, ty);
    }
  }
}

function latLngToCanvas(lat, lng) {
  if (!mapCenter) return { x: 0, y: 0 };
  var w = trackCv.width, h = trackCv.height;
  var center = latLngToTile(mapCenter.lat, mapCenter.lng, mapZoom);
  var pt     = latLngToTile(lat, lng, mapZoom);
  var tileSize = 256;
  return {
    x: (pt.x - center.x) * tileSize + w/2,
    y: (pt.y - center.y) * tileSize + h/2,
  };
}

function drawTrackOnCanvas() {
  var w = trackCv.width, h = trackCv.height;
  tCtx.clearRect(0, 0, w, h);
  if (trackPts.length === 0) { overlay.style.display='block'; return; }
  overlay.style.display = 'none';
  var acc  = getComputedStyle(document.body).getPropertyValue('--acc').trim()  || '#3ecfcf';
  var safe = getComputedStyle(document.body).getPropertyValue('--safe').trim() || '#3ecf82';
  var warn = getComputedStyle(document.body).getPropertyValue('--warn').trim() || '#e8a83a';
  if (!mapCenter || !document.getElementById('tile-cv').width) {
    var lats = trackPts.map(function(p){return p[0];});
    var lngs = trackPts.map(function(p){return p[1];});
    var minLat=Math.min.apply(null,lats), maxLat=Math.max.apply(null,lats);
    var minLng=Math.min.apply(null,lngs), maxLng=Math.max.apply(null,lngs);
    var dLat=maxLat-minLat||0.0001, dLng=maxLng-minLng||0.0001;
    var pad=24;
    function tx2(lng){ return pad+(lng-minLng)/dLng*(w-2*pad); }
    function ty2(lat){ return h-pad-(lat-minLat)/dLat*(h-2*pad); }
    tCtx.strokeStyle='rgba(255,255,255,0.05)'; tCtx.lineWidth=1;
    for (var i=0;i<=4;i++){
      var gx=pad+i*(w-2*pad)/4, gy=pad+i*(h-2*pad)/4;
      tCtx.beginPath(); tCtx.moveTo(gx,pad); tCtx.lineTo(gx,h-pad); tCtx.stroke();
      tCtx.beginPath(); tCtx.moveTo(pad,gy); tCtx.lineTo(w-pad,gy); tCtx.stroke();
    }
    tCtx.beginPath();
    tCtx.moveTo(tx2(trackPts[0][1]), ty2(trackPts[0][0]));
    for (var i=1;i<trackPts.length;i++) tCtx.lineTo(tx2(trackPts[i][1]), ty2(trackPts[i][0]));
    tCtx.strokeStyle=acc; tCtx.lineWidth=2.5; tCtx.lineJoin='round'; tCtx.stroke();
    tCtx.fillStyle=safe; tCtx.beginPath();
    tCtx.arc(tx2(trackPts[0][1]),ty2(trackPts[0][0]),5,0,2*Math.PI); tCtx.fill();
    var last=trackPts[trackPts.length-1];
    tCtx.fillStyle=warn; tCtx.beginPath();
    tCtx.arc(tx2(last[1]),ty2(last[0]),6,0,2*Math.PI); tCtx.fill();
    tCtx.fillStyle=acc; tCtx.font='16px sans-serif'; tCtx.textAlign='center';
    tCtx.fillText('⛵', tx2(last[1]), ty2(last[0])-10);
    return;
  }
  tCtx.beginPath();
  var first = latLngToCanvas(trackPts[0][0], trackPts[0][1]);
  tCtx.moveTo(first.x, first.y);
  for (var i=1;i<trackPts.length;i++) {
    var p = latLngToCanvas(trackPts[i][0], trackPts[i][1]);
    tCtx.lineTo(p.x, p.y);
  }
  tCtx.strokeStyle=acc; tCtx.lineWidth=2.5; tCtx.lineJoin='round'; tCtx.stroke();
  var sp = latLngToCanvas(trackPts[0][0], trackPts[0][1]);
  tCtx.fillStyle=safe; tCtx.beginPath(); tCtx.arc(sp.x,sp.y,5,0,2*Math.PI); tCtx.fill();
  var lp = latLngToCanvas(trackPts[trackPts.length-1][0], trackPts[trackPts.length-1][1]);
  tCtx.fillStyle=warn; tCtx.beginPath(); tCtx.arc(lp.x,lp.y,6,0,2*Math.PI); tCtx.fill();
  tCtx.fillStyle=acc; tCtx.font='16px sans-serif'; tCtx.textAlign='center';
  tCtx.fillText('⛵', lp.x, lp.y-10);
}

function loadTrack() {
  resizeCanvases();
  fetch('/track').then(function(r){return r.json();}).then(function(d) {
    trackPts = d;
    overlay.textContent = trackPts.length === 0 ? 'No GPS fix' : '';
    if (trackPts.length > 0) {
      var last = trackPts[trackPts.length-1];
      mapCenter = { lat: last[0], lng: last[1] };
      drawTiles();
    }
    drawTrackOnCanvas();
  }).catch(function(){ overlay.textContent = 'Track unavailable'; });
  fetch('/telemetry').then(function(r){return r.json();}).then(function(d){
    if (d.lat  !== undefined) document.getElementById('m-lat').textContent = d.lat.toFixed(4)+'\xb0';
    if (d.lng  !== undefined) document.getElementById('m-lon').textContent = d.lng.toFixed(4)+'\xb0';
    if (d.alt  !== undefined) document.getElementById('m-alt').textContent = d.alt.toFixed(0)+'m';
    if (d.hdg  !== undefined) document.getElementById('m-hdg').textContent = d.hdg.toFixed(0)+'\xb0';
    if (d.speed!== undefined) document.getElementById('m-spd').textContent = d.speed.toFixed(1);
    if (d.sats !== undefined) document.getElementById('m-sats').textContent = d.sats;
  }).catch(function(){});
}

function clearTrack() {
  trackPts = [];
  tCtx.clearRect(0, 0, trackCv.width, trackCv.height);
  tileCtx.clearRect(0, 0, tileCv.width, tileCv.height);
  overlay.textContent = 'No GPS fix';
  overlay.style.display = 'block';
}

setInterval(function() {
  if (document.getElementById('page-map').style.display !== 'none') loadTrack();
}, 5000);

// ── TELEMETRY POLLING ─────────────────────────────────────────────
var instrData = { v:0, a:0, pct:0, mah:0, temp:0, roll:0, pitch:0, yaw:0, sats:0, speed:0 };

setInterval(function() {
  fetch('/telemetry').then(function(r){return r.json();}).then(function(d) {
    if (d.v     !== undefined) instrData.v     = d.v;
    if (d.a     !== undefined) instrData.a     = d.a;
    if (d.pct   !== undefined) instrData.pct   = d.pct;
    if (d.mah   !== undefined) instrData.mah   = d.mah;
    if (d.temp  !== undefined) instrData.temp  = d.temp;
    if (d.roll  !== undefined) instrData.roll  = d.roll;
    if (d.pitch !== undefined) instrData.pitch = d.pitch;
    if (d.yaw   !== undefined) instrData.yaw   = d.yaw;
    if (d.sats  !== undefined) instrData.sats  = d.sats;
    if (d.speed !== undefined) instrData.speed = d.speed;
    // Update control telem strip
    var vEl = document.getElementById('tl-v');
    if (vEl) { vEl.textContent = d.v !== undefined ? d.v.toFixed(1)+'V' : '--.-V'; vEl.className='tg-v'+(d.v<11.0?' d':d.v<11.4?' w':''); }
    var aEl = document.getElementById('tl-a');
    if (aEl) aEl.textContent = d.a !== undefined ? d.a.toFixed(1)+'A' : '-.-A';
    var rEl = document.getElementById('tl-r');
    if (rEl) rEl.textContent = d.roll  !== undefined ? 'R'+(d.roll>=0?'+':'')+d.roll.toFixed(1)+'\xb0' : 'R--\xb0';
    var pEl = document.getElementById('tl-p');
    if (pEl) pEl.textContent = d.pitch !== undefined ? 'P'+(d.pitch>=0?'+':'')+d.pitch.toFixed(1)+'\xb0' : 'P--\xb0';
    var tEl = document.getElementById('tl-t');
    if (tEl) tEl.textContent = d.temp !== undefined ? d.temp.toFixed(0)+'\xb0C' : '--\xb0C';
    var sEl = document.getElementById('tl-s');
    if (sEl) sEl.textContent = d.sats !== undefined ? d.sats+'sat' : '--sat';
    var spdEl = document.getElementById('tl-spd');
    if (spdEl) spdEl.textContent = d.speed !== undefined ? d.speed.toFixed(1)+'km/h' : '--km/h';
    // Update INA note in devices page
    var inaNote = document.getElementById('dev-ina-note');
    if (inaNote && d.v !== undefined) inaNote.textContent = d.v.toFixed(1)+'V \xb7 '+(d.a||0).toFixed(1)+'A';
    // Live-update instruments page if visible
    if (document.getElementById('page-instr').style.display !== 'none') updateInstruments();
  }).catch(function(){});
}, 100);

setInterval(function() {
  fetch('/status').then(function(r){return r.json();}).then(function(d) {
    var bar = document.getElementById('bilge');
    var txt = document.getElementById('bilge-txt');
    if (bar) bar.className = d.wet ? 'wet' : '';
    if (txt) txt.textContent = d.wet ? 'Bilge Wet' : 'Bilge OK';
    if (d.pump !== pumpOn) {
      pumpOn = d.pump;
      var btn = document.getElementById('pump-btn');
      if (btn) { btn.textContent = 'Pump: '+(pumpOn?'ON':'OFF'); btn.className = pumpOn?'on':''; }
    }
    var bv = document.getElementById('g-bilge-v');
    if (bv) { bv.textContent = d.wet ? 'WET ⚠' : 'DRY'; bv.className = 'g-val'+(d.wet?' d':''); }
    var pv = document.getElementById('g-pump-v');
    if (pv) pv.textContent = d.pump ? 'Pump on' : 'Pump off';
  }).catch(function(){});
}, 2000);

// ── INSTRUMENTS ───────────────────────────────────────────────────
function updateInstruments() {
  var roll  = instrData.roll  || 0;
  var pitch = instrData.pitch || 0;
  var pitchOff = pitch * 2.6;
  var g = document.getElementById('att-horizon');
  if (g) g.setAttribute('transform', 'rotate('+roll+')');
  var sky = document.getElementById('att-sky');
  if (sky) sky.setAttribute('y', (-120 + pitchOff).toFixed(1));
  var gnd = document.getElementById('att-gnd');
  if (gnd) gnd.setAttribute('y', pitchOff.toFixed(1));
  var hor = document.getElementById('att-hor');
  if (hor) { hor.setAttribute('y1', pitchOff.toFixed(1)); hor.setAttribute('y2', pitchOff.toFixed(1)); }
  var ptr = document.getElementById('att-ptr');
  if (ptr) ptr.setAttribute('transform', 'rotate('+roll+')');
  var rEl = document.getElementById('att-roll');
  if (rEl) rEl.textContent = (roll>=0?'+':'')+roll.toFixed(1)+'\xb0';
  var pEl = document.getElementById('att-pitch');
  if (pEl) pEl.textContent = (pitch>=0?'+':'')+pitch.toFixed(1)+'\xb0';
  var hEl = document.getElementById('att-hdg');
  if (hEl) hEl.textContent = (instrData.yaw||0).toFixed(0)+'\xb0';
  var v = instrData.v || 0;
  var vEl = document.getElementById('g-v');
  if (vEl) { vEl.textContent = v.toFixed(1)+'V'; vEl.className='g-val'+(v<11.0?' d':v<11.4?' w':''); }
  var aEl = document.getElementById('g-a');
  if (aEl) aEl.textContent = (instrData.a||0).toFixed(1)+'A';
  var vBar = document.getElementById('g-v-bar');
  if (vBar) { vBar.style.width=Math.max(0,Math.min(100,(v-10.5)/(12.6-10.5)*100))+'%'; vBar.className='g-bar-fill'+(v<11.0?' d':v<11.4?' w':''); }
  var pct = instrData.pct || 0;
  var pEl2 = document.getElementById('g-pct');
  if (pEl2) { pEl2.textContent=pct+'%'; pEl2.className='g-val'+(pct<15?' d':pct<30?' w':''); }
  var mahEl = document.getElementById('g-mah');
  if (mahEl) mahEl.textContent=(instrData.mah||0).toFixed(0)+' mAh';
  var pBar = document.getElementById('g-pct-bar');
  if (pBar) { pBar.style.width=pct+'%'; pBar.className='g-bar-fill'+(pct<15?' d':pct<30?' w':''); }
  var tmp = instrData.temp || 0;
  var tEl = document.getElementById('g-temp-v');
  if (tEl) { tEl.textContent=tmp.toFixed(0)+'\xb0C'; tEl.className='g-val'+(tmp>80?' d':tmp>60?' w':''); }
  var tBar = document.getElementById('g-temp-bar');
  if (tBar) { tBar.style.width=Math.max(0,Math.min(100,(tmp-20)/80*100))+'%'; tBar.className='g-bar-fill'+(tmp>80?' d':tmp>60?' w':''); }
}

// ── DEVICES ───────────────────────────────────────────────────────
function updateDevices() {
  fetch('/diag.json').then(function(r){return r.json();}).then(function(d) {
    d.forEach(function(item) {
      var card = document.getElementById('dev-'+item.id);
      if (!card) return;
      var level = item.level || 'absent';
      card.className = 'dev-card ' + level;
      var dot = card.querySelector('.dev-dot');
      if (dot) dot.className = 'dev-dot ' + level;
      var statusEl = card.querySelector('.dev-status');
      if (statusEl) { statusEl.className = 'dev-status ' + level; statusEl.textContent = {ok:'OK',warn:'WARN',error:'FAULT',absent:'N/A'}[level]||'--'; }
      var noteEl = card.querySelector('.dev-note');
      if (noteEl && item.stat !== undefined) noteEl.textContent = item.stat;
      // Repair button: show for repairable absent/error devices
      var repBtn = card.querySelector('.repair-btn');
      if (item.repairable && level !== 'ok') {
        if (!repBtn) {
          repBtn = document.createElement('button');
          repBtn.onclick = (function(nid, b) { return function() { repairDev(nid, b); }; })(item.numId, repBtn);
          card.appendChild(repBtn);
        }
        repBtn.textContent = '↻ Repair';
        repBtn.className = 'repair-btn ' + level;
      } else {
        if (repBtn) repBtn.remove();
      }
    });
  }).catch(function(){});
}

function repairDev(id, btn) {
  var orig = btn.textContent;
  btn.textContent = '⟳ Probing…'; btn.disabled = true;
  fetch('/repair?id='+id).catch(function(){});
  setTimeout(function() { btn.textContent = orig; btn.disabled = false; updateDevices(); }, 2000);
}

function reprobeAll() {
  var btn = document.getElementById('reprobe-all-btn');
  btn.textContent = '⟳ Probing…'; btn.disabled = true;
  fetch('/repair?id=all').catch(function(){});
  setTimeout(function() { btn.textContent = '⟳ Reprobe All'; btn.disabled = false; updateDevices(); }, 2500);
}
</script>
</body>
</html>
)html";

// ── Embedded GPS track map page ───────────────────────────────────────────────
static const char MAP_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<meta charset="utf-8">
<title>Track &mdash; Dark &amp; Stormy</title>
<style>
*{box-sizing:border-box}
body{margin:0;background:#0d1117;color:#e6edf3;font-family:-apple-system,sans-serif}
h2{margin:10px;font-size:18px;color:#2dd4bf}
#info{margin:0 10px 6px;font-size:12px;color:#8b949e;min-height:16px}
canvas{display:block;background:#161b22;margin:0 auto;border-radius:8px;max-width:100%}
.row{display:flex;gap:8px;margin:10px;justify-content:center}
.btn{padding:9px 18px;background:#21262d;border:1px solid #30363d;color:#e6edf3;border-radius:6px;cursor:pointer;font-size:13px;text-decoration:none;display:inline-block}
</style>
</head>
<body>
<h2>&#9973; GPS Track</h2>
<div id="info">Loading&hellip;</div>
<canvas id="cv"></canvas>
<div class="row">
  <span class="btn" onclick="load()">&#8635; Refresh</span>
  <a href="/" class="btn">&#8592; Control</a>
</div>
<script>
var pts=[];
var cv=document.getElementById('cv');
var ctx=cv.getContext('2d');
function resize(){
  var w=Math.min(window.innerWidth-20,480);
  cv.width=w; cv.height=w;
}
resize();
window.addEventListener('resize',function(){resize();draw();});
function draw(){
  var W=cv.width,H=cv.height;
  ctx.fillStyle='#161b22'; ctx.fillRect(0,0,W,H);
  if(pts.length<2){
    ctx.fillStyle='#8b949e'; ctx.font='14px sans-serif'; ctx.textAlign='center';
    ctx.fillText(pts.length===0?'No GPS fix yet':'Collecting more points…',W/2,H/2);
    return;
  }
  var lats=pts.map(function(p){return p[0];}),lngs=pts.map(function(p){return p[1];});
  var minLat=Math.min.apply(null,lats),maxLat=Math.max.apply(null,lats);
  var minLng=Math.min.apply(null,lngs),maxLng=Math.max.apply(null,lngs);
  var dLat=maxLat-minLat||0.0001,dLng=maxLng-minLng||0.0001;
  var pad=24;
  function tx(lng){return pad+(lng-minLng)/dLng*(W-2*pad);}
  function ty(lat){return H-pad-(lat-minLat)/dLat*(H-2*pad);}
  // Grid lines
  ctx.strokeStyle='#21262d'; ctx.lineWidth=1;
  for(var i=0;i<=4;i++){
    var gx=pad+i*(W-2*pad)/4,gy=pad+i*(H-2*pad)/4;
    ctx.beginPath();ctx.moveTo(gx,pad);ctx.lineTo(gx,H-pad);ctx.stroke();
    ctx.beginPath();ctx.moveTo(pad,gy);ctx.lineTo(W-pad,gy);ctx.stroke();
  }
  // Track polyline
  ctx.beginPath();
  ctx.moveTo(tx(pts[0][1]),ty(pts[0][0]));
  for(var i=1;i<pts.length;i++) ctx.lineTo(tx(pts[i][1]),ty(pts[i][0]));
  ctx.strokeStyle='#2dd4bf'; ctx.lineWidth=2; ctx.lineJoin='round'; ctx.stroke();
  // Start dot (green)
  ctx.fillStyle='#22c55e';
  ctx.beginPath(); ctx.arc(tx(pts[0][1]),ty(pts[0][0]),5,0,2*Math.PI); ctx.fill();
  // Latest dot (amber)
  var l=pts[pts.length-1];
  ctx.fillStyle='#f59e0b';
  ctx.beginPath(); ctx.arc(tx(l[1]),ty(l[0]),5,0,2*Math.PI); ctx.fill();
}
function load(){
  document.getElementById('info').textContent='Loading…';
  fetch('/track').then(function(r){return r.json();}).then(function(d){
    pts=d;
    var msg=pts.length+' point'+(pts.length!==1?'s':'');
    if(pts.length>0){
      var l=pts[pts.length-1];
      msg+=' — latest: '+l[0].toFixed(5)+', '+l[1].toFixed(5);
    }
    document.getElementById('info').textContent=msg;
    draw();
  }).catch(function(){document.getElementById('info').textContent='Error loading track.';});
}
load();
setInterval(load,5000);
</script>
</body>
</html>
)html";

// ── Embedded OTA update page ──────────────────────────────────────────────────
static const char OTA_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<meta charset="utf-8">
<title>Firmware Update &mdash; Dark &amp; Stormy</title>
<style>
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;padding:14px;background:#0d1117;color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,sans-serif;max-width:480px;margin:auto}
h1{margin:0;font-size:22px;color:#a78bfa}
.sub{color:#6e7681;font-size:12px;margin:2px 0 14px}
.card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:14px;margin:14px 0}
.step{color:#8b949e;font-size:13px;margin:7px 0;line-height:1.5}
.step code{background:#21262d;padding:2px 6px;border-radius:4px;font-family:monospace;font-size:12px;color:#e6edf3;word-break:break-all}
input[type=file]{display:block;width:100%;padding:10px;background:#21262d;border:1px dashed #30363d;border-radius:8px;color:#e6edf3;margin:12px 0;cursor:pointer;font-size:13px}
#uploadBtn{display:block;width:100%;padding:14px;font-size:16px;font-weight:700;border:none;border-radius:10px;cursor:pointer;background:#312e81;color:#a78bfa;margin-top:4px}
#uploadBtn:disabled{opacity:.4;cursor:default}
#bar{height:8px;background:#21262d;border-radius:4px;margin:12px 0;overflow:hidden;display:none}
#bfill{height:100%;width:0;background:#a78bfa;border-radius:4px;transition:width .15s}
#status{text-align:center;font-size:14px;min-height:20px;margin:8px 0;font-weight:600}
.ok{color:#86efac}.err{color:#fca5a5}.info{color:#93c5fd}
a.back{display:inline-block;margin-top:12px;color:#6e7681;font-size:13px;text-decoration:none}
</style>
</head>
<body>
<h1>&#128268; Firmware Update</h1>
<p class="sub">Dark &amp; Stormy &mdash; OTA Flash</p>
<div class="card">
  <div class="step"><b>Step 1</b> &mdash; Build the firmware on your computer:<br>
    <code>pio run -e esp32-s3-full</code>
  </div>
  <div class="step"><b>Step 2</b> &mdash; Locate the binary (Windows):<br>
    <code>%LOCALAPPDATA%\pio-build\rcsailboat\esp32-s3-full\firmware.bin</code>
  </div>
  <div class="step"><b>Step 3</b> &mdash; Select the <code>.bin</code> file and upload below.</div>
</div>
<input type="file" id="file" accept=".bin">
<div id="bar"><div id="bfill"></div></div>
<div id="status" class="info"></div>
<button id="uploadBtn" onclick="doUpload()">&#8593;&nbsp; Upload &amp; Flash</button>
<br>
<a href="/" class="back">&#8592; Back to control</a>
<script>
function doUpload(){
  var f=document.getElementById('file').files[0];
  var st=document.getElementById('status');
  if(!f){st.className='err';st.textContent='Select a .bin file first.';return;}
  var btn=document.getElementById('uploadBtn');
  var bar=document.getElementById('bar');
  btn.disabled=true;
  bar.style.display='block';
  st.className='info'; st.textContent='Uploading…';
  var fd=new FormData();
  fd.append('firmware',f,f.name);
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/update');
  xhr.upload.onprogress=function(e){
    if(e.lengthComputable){
      var pct=Math.round(e.loaded/e.total*100);
      document.getElementById('bfill').style.width=pct+'%';
      st.textContent='Uploading… '+pct+'%';
    }
  };
  xhr.onload=function(){
    document.getElementById('bfill').style.width='100%';
    if(xhr.status===200){
      st.className='ok';
      st.textContent='Flashed! Rebooting in 3 s…';
      setTimeout(function(){window.location='/';},5000);
    } else {
      st.className='err';
      st.textContent='Failed: '+xhr.responseText;
      btn.disabled=false;
    }
  };
  xhr.onerror=function(){
    st.className='err';
    st.textContent='Upload error — check Wi-Fi connection.';
    btn.disabled=false;
  };
  xhr.send(fd);
}
</script>
</body>
</html>
)html";

// ── Embedded device-diagnostics page ─────────────────────────────────────────
static const char DIAG_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta charset="utf-8">
<title>Devices &mdash; Dark &amp; Stormy</title>
<style>
*{box-sizing:border-box}
body{margin:0;background:#0a0a0a;color:#e5e7eb;font-family:-apple-system,sans-serif;padding:16px}
h2{color:#60a5fa;margin:0 0 4px}
.sub{color:#6b7280;font-size:12px;margin:0 0 16px}
.back{color:#9ca3af;text-decoration:none;font-size:14px;display:inline-block;margin-bottom:14px}
.dev{background:#111827;border-radius:8px;padding:11px 13px;margin-bottom:8px;display:flex;align-items:center;gap:11px}
.dot{width:14px;height:14px;border-radius:50%;flex-shrink:0}
.dot-ok{background:#22c55e}
.dot-warn{background:#f59e0b}
.dot-absent{background:#ef4444}
.dot-disabled{background:#4b5563}
.info{flex:1;min-width:0}
.name{font-weight:700;font-size:15px}
.meta{font-size:11px;color:#6b7280;margin-top:1px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.stat{font-size:12px;font-weight:600;margin-top:3px}
.stat-ok{color:#22c55e}.stat-warn{color:#f59e0b}.stat-absent{color:#ef4444}.stat-disabled{color:#6b7280}
.btn{border:none;padding:7px 12px;border-radius:6px;cursor:pointer;font-size:12px;font-weight:600;flex-shrink:0}
.btn-repair{background:#b91c1c;color:#fff}
.btn-repair:hover{background:#dc2626}
.btn-passive{background:#1f2937;color:#6b7280;cursor:default}
#msg{min-height:16px;font-size:12px;color:#9ca3af;margin-bottom:10px}
</style>
</head>
<body>
<a class="back" href="/">&#8592; Back</a>
<h2>&#128246; Devices</h2>
<p class="sub">Tap Repair to re-probe an absent I&sup2;C device.</p>
<div id="msg"></div>
<div id="devs">Loading&hellip;</div>
<script>
function render(data){
  var h='';
  data.forEach(function(d){
    var lv=d.level||'ok';
    var meta=d.role+(d.addr?' &middot; '+d.addr:'');
    h+='<div class="dev">'
     +'<div class="dot dot-'+lv+'"></div>'
     +'<div class="info">'
     +'<div class="name">'+d.name+'</div>'
     +'<div class="meta">'+meta+'</div>'
     +'<div class="stat stat-'+lv+'">'+d.stat+'</div>'
     +'</div>';
    if(d.repairable&&lv==='absent'){
      h+='<button class="btn btn-repair" onclick="repair('+d.id+')">Repair</button>';
    } else {
      h+='<button class="btn btn-passive" disabled>'+d.stat+'</button>';
    }
    h+='</div>';
  });
  document.getElementById('devs').innerHTML=h;
}
function load(){
  fetch('/diag.json').then(function(r){return r.json();}).then(render).catch(function(){
    document.getElementById('devs').textContent='Error loading device list.';
  });
}
function repair(id){
  document.getElementById('msg').textContent='Probing…';
  fetch('/repair?id='+id).then(function(r){return r.json();}).then(function(d){
    document.getElementById('msg').textContent=d.ok?'Found — re-initialised.':'Still absent.';
    load();
  });
}
load();
setInterval(load,3000);
</script>
</body>
</html>
)html";

// ── Module state ──────────────────────────────────────────────────────────────
static WebServer s_srv(80);
static CtrlMode  s_mode     = CtrlMode::WIFI;
static bool      s_ap_up    = false;
static uint8_t   s_clients  = 0;

// Shared control values (all written/read on main task — no mutex needed)
static float         s_rudder   = 0.0f;
static float         s_sail     = 0.0f;
static float         s_throttle = 0.0f;
static bool          s_armed    = false;
static unsigned long s_last_cmd = 0;

// Mode-switch request (written from LVGL task, read from main task)
static volatile bool      s_switch_pending = false;
static volatile CtrlMode  s_switch_target  = CtrlMode::WIFI;

// ── HTTP handlers ─────────────────────────────────────────────────────────────

static void handle_diag_page()
{
    s_srv.send_P(200, "text/html; charset=utf-8", DIAG_HTML);
}

static void handle_diag_json()
{
    // String IDs match HTML card element IDs. numId is the numeric DEV_* index
    // for the repair endpoint (/repair?id=numId). Non-I2C devices omit numId.
    char buf[1500];
    int  n = 0;

    static const char *DEV_IDS[] = {"ft", "qmi", "pca", "ina"};

    n += snprintf(buf, sizeof(buf), "[");

    // ── I2C devices (repairable via /repair?id=numId) ──────────────────────
    for (uint8_t i = 0; i < DEV_COUNT; i++) {
        const DeviceInfo *d  = diag_info((DevId)i);
        bool              ok = d->present && d->enabled;
        n += snprintf(buf + n, sizeof(buf) - n,
            "%s{\"id\":\"%s\",\"numId\":%d,\"name\":\"%s\",\"role\":\"%s\","
            "\"addr\":\"0x%02X\",\"level\":\"%s\",\"stat\":\"%s\","
            "\"repairable\":true}",
            i ? "," : "", DEV_IDS[i], (int)i, d->name, d->role, d->addr,
            ok ? "ok" : "absent",
            ok ? "OK"  : "Absent");
    }

    // ── SD Card ────────────────────────────────────────────────────────────
    {
        bool ok = sdlog_is_ready();
        n += snprintf(buf + n, sizeof(buf) - n,
            ",{\"id\":\"sd\",\"name\":\"SD Card\",\"role\":\"Log storage\","
            "\"level\":\"%s\",\"stat\":\"%s\",\"repairable\":false}",
            ok ? "ok" : "absent", ok ? "Ready" : "No card");
    }

    // ── Bilge Sensor ───────────────────────────────────────────────────────
    {
        bool wet = bilge_water_detected();
        n += snprintf(buf + n, sizeof(buf) - n,
            ",{\"id\":\"bilge\",\"name\":\"Bilge Sensor\",\"role\":\"Water detection\","
            "\"level\":\"%s\",\"stat\":\"%s\",\"repairable\":false}",
            wet ? "warn" : "ok", wet ? "WET" : "Dry");
    }

    // ── Bilge Pump ─────────────────────────────────────────────────────────
    // No way to detect physical connection — absent until pump is running.
    {
        bool on = bilge_pump_active();
        n += snprintf(buf + n, sizeof(buf) - n,
            ",{\"id\":\"pump\",\"name\":\"Bilge Pump\",\"role\":\"Manual drain\","
            "\"level\":\"%s\",\"stat\":\"%s\",\"repairable\":false}",
            on ? "warn" : "absent", on ? "Running" : "Unmonitored");
    }

    // ── Actuators (Rudder / Sail Winch / Motor ESC) ────────────────────────
    static const char *ACT_IDS[]   = {"rudder",    "winch",      "esc"};
    static const char *ACT_NAMES[] = {"Rudder",    "Sail Winch", "Motor ESC"};
    static const char *ACT_ROLES[] = {"Steering",  "Sail trim",  "Propulsion"};
    static const int   ACT_CH[]    = {pwm_ch::RUDDER, pwm_ch::SAIL_WINCH, pwm_ch::MOTOR_ESC};
    bool drv = diag_ok(DEV_PCA9685);
    for (int i = 0; i < 3; i++) {
        char stat[16];
        if (drv) snprintf(stat, sizeof(stat), "%.0f%%",
                          servos_get_commanded(ACT_CH[i]) * 100.0f);
        else     snprintf(stat, sizeof(stat), "No driver");
        n += snprintf(buf + n, sizeof(buf) - n,
            ",{\"id\":\"%s\",\"name\":\"%s\",\"role\":\"%s\","
            "\"level\":\"%s\",\"stat\":\"%s\",\"repairable\":false}",
            ACT_IDS[i], ACT_NAMES[i], ACT_ROLES[i],
            drv ? "ok" : "absent", stat);
    }

    n += snprintf(buf + n, sizeof(buf) - n, "]");
    s_srv.send(200, "application/json", buf);
}

static void handle_repair()
{
    if (!s_srv.hasArg("id")) { s_srv.send(400, "text/plain", "missing id"); return; }
    String id_str = s_srv.arg("id");
    if (id_str == "all") {
        for (uint8_t i = 0; i < DEV_COUNT; i++) diag_reprobe((DevId)i);
        s_srv.send(200, "application/json", "{\"ok\":true}");
        return;
    }
    int id = id_str.toInt();
    if (id < 0 || id >= (int)DEV_COUNT) { s_srv.send(400, "text/plain", "bad id"); return; }
    diag_reprobe((DevId)id);   // safe: runs in loop() via wifi_ctrl_update() on core 1
    static const char *DEV_IDS[] = {"ft", "qmi", "pca", "ina"};
    const DeviceInfo *d = diag_info((DevId)id);
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"id\":\"%s\",\"name\":\"%s\",\"ok\":%s}",
             DEV_IDS[id], d->name, (d->present && d->enabled) ? "true" : "false");
    s_srv.send(200, "application/json", buf);
}

static void handle_root()
{
    s_srv.send_P(200, "text/html; charset=utf-8", HTML_PAGE);
}

static void handle_map()
{
    s_srv.send_P(200, "text/html; charset=utf-8", MAP_HTML);
}

static void handle_control()
{
    float r =  s_srv.arg("r").toFloat() / 100.0f;
    float sl = s_srv.arg("s").toFloat() / 100.0f;
    float t =  s_srv.arg("t").toFloat() / 100.0f;
    bool  a =  s_srv.arg("a") == "1";

    s_rudder   = constrain(r,  -1.0f, 1.0f);
    s_sail     = constrain(sl,  0.0f, 1.0f);
    s_throttle = a ? constrain(t, -1.0f, 1.0f) : 0.0f;
    s_armed    = a;
    s_last_cmd = millis();

    s_srv.send(200, "text/plain", "OK");
}

static void handle_telemetry()
{
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"v\":%.2f,\"a\":%.2f,\"mah\":%.0f,\"roll\":%.1f,\"pitch\":%.1f}",
             power_voltage_v(), power_current_a(), power_mah_used(),
             imu_roll_deg(), imu_pitch_deg());
    s_srv.send(200, "application/json", buf);
}

static void handle_status()
{
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"wet\":%s,\"pump\":%s,\"capsized\":%s}",
             bilge_water_detected() ? "true"  : "false",
             bilge_pump_active()    ? "true"  : "false",
             imu_is_capsized()      ? "true"  : "false");
    s_srv.send(200, "application/json", buf);
}

static void handle_pump()
{
    bool on = s_srv.hasArg("on") && s_srv.arg("on") == "1";
    bilge_pump_set(on);
    s_srv.send(200, "text/plain", "OK");
}

static void handle_track()
{
#ifdef GPS_ENABLED
    // Build a compact JSON array: [[lat,lng],...]
    // Reserve ~20 chars per point + brackets.
    String json;
    json.reserve(s_track_len * 22 + 4);
    json = "[";
    for (int i = 0; i < s_track_len; i++) {
        if (i) json += ",";
        char pt[24];
        snprintf(pt, sizeof(pt), "[%.6f,%.6f]", s_track[i].lat, s_track[i].lng);
        json += pt;
    }
    json += "]";
    s_srv.send(200, "application/json", json);
#else
    s_srv.send(200, "application/json", "[]");
#endif
}

// Return Apple's expected captive-portal success page.
// iOS makes an HTTP request to captive.apple.com/hotspot-detect.html on every
// new WiFi association. If the response is anything other than this exact page,
// iOS shows a "Sign In to Network" popup. The user dismissing that popup causes
// iOS to forget the session and prompt for the password again — the "strange
// loop". Returning 200 + success content tells iOS the network has internet and
// skips the popup entirely.
static void handle_apple_connectivity_check()
{
    s_srv.send(200, "text/html",
        "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
}

// Redirect any unrecognised URL to the control page (catch-all captive portal).
static void handle_captive_redirect()
{
    s_srv.sendHeader("Location", "http://192.168.4.1/");
    s_srv.send(302, "text/plain", "");
}

// Serves /tiles/{z}/{x}/{y}.png from the SD card (if mounted).
// All other unrecognised paths redirect to the control page (captive portal).
static void handle_not_found()
{
    String path = s_srv.uri();
    if (path.startsWith("/tiles/") && sdlog_is_ready()) {
        File f = SD.open(path.c_str());
        if (f && !f.isDirectory()) {
            s_srv.streamFile(f, "image/png");
            f.close();
            return;
        }
        if (f) f.close();
    }
    handle_captive_redirect();
}

// ── OTA handlers ─────────────────────────────────────────────────────────────

// GET /update — serve the OTA upload page.
static void handle_ota_get()
{
    s_srv.send_P(200, "text/html; charset=utf-8", OTA_HTML);
}

// Upload handler — called by WebServer for each multipart chunk.
// UPLOAD_FILE_START: opens the OTA flash partition.
// UPLOAD_FILE_WRITE: writes each chunk to flash.
// UPLOAD_FILE_END:   finalises the write; marks the new partition bootable.
// If any step fails, Update.hasError() will be true when the POST handler runs.
static void handle_ota_upload()
{
    HTTPUpload &upload = s_srv.upload();

    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("ota: start — %s (%u B)\n",
                      upload.filename.c_str(), upload.totalSize);
        // UPDATE_SIZE_UNKNOWN lets the OTA library grow the write area as needed.
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Serial.printf("ota: begin failed: %s\n", Update.errorString());
        }

    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Serial.printf("ota: write failed: %s\n", Update.errorString());
        }

    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {   // true = set the new partition as the boot target
            Serial.printf("ota: complete — %u bytes written\n", upload.totalSize);
        } else {
            Serial.printf("ota: end failed: %s\n", Update.errorString());
        }
    }
}

// POST /update — called after the upload finishes.
// Sends the result, closes the connection, then reboots if successful.
// The reboot must happen after the HTTP response is sent, so we drain the
// client first, wait briefly for TCP teardown, then call ESP.restart().
static void handle_ota_post()
{
    bool ok = !Update.hasError();
    s_srv.sendHeader("Connection", "close");
    s_srv.send(200, "text/plain", ok ? "OK" : Update.errorString());
    s_srv.client().stop();    // ensure response bytes are flushed before reboot
    delay(300);
    if (ok) {
        Serial.println("ota: rebooting into new firmware");
        ESP.restart();
    }
}

// ── AP lifecycle ──────────────────────────────────────────────────────────────
static void start_ap()
{
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.printf("wifi_ctrl: AP up — SSID=%s  IP=%s\n",
                  WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());

    s_srv.on("/",          handle_root);
    s_srv.on("/map",       handle_map);
    s_srv.on("/control",   handle_control);
    s_srv.on("/telemetry", handle_telemetry);
    s_srv.on("/status",    handle_status);
    s_srv.on("/pump",      handle_pump);
    s_srv.on("/track",     handle_track);
    s_srv.on("/diag",      HTTP_GET, handle_diag_page);
    s_srv.on("/diag.json", HTTP_GET, handle_diag_json);
    s_srv.on("/repair",    HTTP_GET, handle_repair);
    // OTA: GET serves the upload page; POST receives the binary + flashes + reboots.
    s_srv.on("/update", HTTP_GET,  handle_ota_get);
    s_srv.on("/update", HTTP_POST, handle_ota_post, handle_ota_upload);
    // Explicit captive-portal routes (iOS, Android, Windows connectivity checks)
    s_srv.on("/hotspot-detect.html",       handle_apple_connectivity_check);
    s_srv.on("/library/test/success.html", handle_apple_connectivity_check);
    s_srv.on("/hotspotdetect.html",        handle_apple_connectivity_check);
    s_srv.on("/generate_204",              []() { s_srv.send(204, "text/plain", ""); });
    s_srv.on("/ncsi.txt",                  handle_captive_redirect);
    s_srv.on("/connecttest.txt",           handle_captive_redirect);
    s_srv.onNotFound(handle_not_found);
    s_srv.begin();

    // Reset timeout so we don't immediately failsafe on mode entry
    s_last_cmd  = millis();
    s_armed     = false;
    s_rudder    = s_sail = s_throttle = 0.0f;
    s_ap_up     = true;
}

static void stop_ap()
{
    s_srv.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    s_ap_up    = false;
    s_armed    = false;
    s_rudder   = s_sail = s_throttle = 0.0f;
    Serial.println("wifi_ctrl: AP stopped");
}

// ── Public API ────────────────────────────────────────────────────────────────
void wifi_ctrl_init()
{
    start_ap();
    s_mode = CtrlMode::WIFI;
}

void wifi_ctrl_update()
{
    // Apply any pending mode switch (flag set by LVGL task)
    if (s_switch_pending) {
        s_switch_pending = false;
        CtrlMode target = s_switch_target;
        if (target != s_mode) {
            if (target == CtrlMode::ELRS) {
                stop_ap();
            } else {
                start_ap();
            }
            s_mode = target;
        }
    }

    if (!s_ap_up) return;

    s_srv.handleClient();
    s_clients = (uint8_t)WiFi.softAPgetStationNum();

#ifdef GPS_ENABLED
    // Record a track point every 5 s when we have a GPS fix.
    if (gps_has_fix() && (millis() - s_track_last_ms) >= 5000) {
        s_track_last_ms = millis();
        if (s_track_len < 500) {
            s_track[s_track_len++] = { (float)gps_lat(), (float)gps_lng() };
        }
    }
#endif
}

void wifi_ctrl_set_mode(CtrlMode m)
{
    s_switch_target  = m;
    s_switch_pending = true;
}

CtrlMode    wifi_ctrl_mode()         { return s_mode; }
bool        wifi_ctrl_active()       { return s_ap_up; }
uint8_t     wifi_ctrl_clients()      { return s_clients; }
float       wifi_ctrl_rudder()       { return s_rudder; }
float       wifi_ctrl_sail()         { return s_sail; }
float       wifi_ctrl_throttle()     { return s_throttle; }
bool        wifi_ctrl_armed()        { return s_armed; }
unsigned long wifi_ctrl_last_cmd_ms(){ return s_last_cmd; }
bool        wifi_ctrl_timed_out()
{
    return s_mode == CtrlMode::WIFI &&
           (millis() - s_last_cmd) > CTRL_TIMEOUT_MS;
}
