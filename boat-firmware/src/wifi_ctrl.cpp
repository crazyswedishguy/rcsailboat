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
#include "imu.h"
#include "power.h"
#include "sdlog.h"

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
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<meta charset="utf-8">
<title>Dark &amp; Stormy</title>
<style>
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;padding:14px;background:#0d1117;color:#e6edf3;font-family:-apple-system,BlinkMacSystemFont,sans-serif;max-width:480px;margin:auto}
h1{margin:0;font-size:22px;color:#2dd4bf}
.sub{color:#6e7681;font-size:12px;margin:2px 0 12px}
#bar{height:6px;background:#21262d;border-radius:3px;margin-bottom:14px;overflow:hidden}
#bfill{height:100%;width:0;background:#2dd4bf;border-radius:3px;transition:width .3s}
.ctrl{margin:10px 0}
.ctrl label{display:flex;justify-content:space-between;margin-bottom:5px;font-size:13px;color:#8b949e}
.ctrl label span:last-child{color:#e6edf3;font-weight:600;min-width:40px;text-align:right}
input[type=range]{width:100%;height:42px;accent-color:#2dd4bf;cursor:pointer}
#armBtn{display:block;width:100%;padding:15px;font-size:18px;font-weight:700;border:none;border-radius:10px;cursor:pointer;margin:14px 0}
.disarmed{background:#991b1b;color:#fca5a5}
.armed{background:#166534;color:#86efac}
.telem{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:14px}
.tc{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:9px;text-align:center}
.tc .lb{font-size:10px;color:#8b949e;text-transform:uppercase;letter-spacing:.5px}
.tc .vl{font-size:20px;font-weight:700;margin-top:3px}
.row{display:flex;gap:8px;margin-top:10px}
.btn2{flex:1;padding:12px;font-size:15px;font-weight:700;border:none;border-radius:8px;cursor:pointer;color:#e6edf3;text-align:center;text-decoration:none;display:block}
</style>
</head>
<body>
<h1>&#9973; Dark &amp; Stormy</h1>
<p class="sub">RC Sailboat &mdash; WiFi Direct</p>
<div id="bar"><div id="bfill"></div></div>
<button id="armBtn" class="disarmed" onclick="toggleArm()">&#128274; DISARMED &mdash; Tap to Arm</button>
<div class="ctrl">
  <label><span>RUDDER</span><span id="rv">0</span></label>
  <input type="range" id="r" min="-100" max="100" value="0"
         oninput="document.getElementById('rv').textContent=this.value">
</div>
<div class="ctrl">
  <label><span>SAIL TRIM</span><span id="sv">0</span></label>
  <input type="range" id="s" min="0" max="100" value="0"
         oninput="document.getElementById('sv').textContent=this.value">
</div>
<div class="ctrl">
  <label><span>THROTTLE</span><span id="tv">0</span></label>
  <input type="range" id="t" min="-100" max="100" value="0"
         oninput="document.getElementById('tv').textContent=this.value">
</div>
<div class="telem">
  <div class="tc"><div class="lb">Battery</div><div class="vl" id="bv">---</div></div>
  <div class="tc"><div class="lb">Current</div><div class="vl" id="bc">---</div></div>
  <div class="tc"><div class="lb">Roll</div><div class="vl" id="rl">---</div></div>
  <div class="tc"><div class="lb">Pitch</div><div class="vl" id="pt">---</div></div>
  <div class="tc"><div class="lb">Bilge</div><div class="vl" id="bg" style="font-size:15px">---</div></div>
  <div class="tc"><div class="lb">Pump</div><div class="vl" id="pm" style="font-size:15px">---</div></div>
</div>
<div class="row" style="margin-top:14px">
  <button id="pumpBtn" class="btn2" style="background:#374151" onclick="togglePump()">Pump: OFF</button>
  <a href="/map" class="btn2" style="background:#1e3a5f;line-height:1.6">&#127754; Track Map</a>
  <a href="/update" class="btn2" style="background:#1c1a2e;line-height:1.6;color:#a78bfa">&#128268; OTA</a>
</div>
<script>
var armed=false, pumpOn=false;
function toggleArm(){
  armed=!armed;
  var b=document.getElementById('armBtn');
  b.className=armed?'armed':'disarmed';
  b.innerHTML=armed?'&#9875; ARMED &mdash; Tap to Disarm':'&#128274; DISARMED &mdash; Tap to Arm';
  if(!armed){
    ['r','s','t'].forEach(function(x){document.getElementById(x).value=0});
    ['rv','sv','tv'].forEach(function(x){document.getElementById(x).textContent='0'});
  }
}
function togglePump(){
  pumpOn=!pumpOn;
  fetch('/pump?on='+(pumpOn?1:0));
  document.getElementById('pumpBtn').textContent='Pump: '+(pumpOn?'ON':'OFF');
  document.getElementById('pumpBtn').style.background=pumpOn?'#166534':'#374151';
}
setInterval(function(){
  var r=document.getElementById('r').value;
  var s=document.getElementById('s').value;
  var t=armed?document.getElementById('t').value:0;
  fetch('/control?r='+r+'&s='+s+'&t='+t+'&a='+(armed?1:0))
    .then(function(){
      var f=document.getElementById('bfill');
      f.style.width='100%';
      setTimeout(function(){f.style.width='0'},200);
    })
    .catch(function(){document.getElementById('bfill').style.width='0'});
},50);
setInterval(function(){
  fetch('/telemetry')
    .then(function(x){return x.json()})
    .then(function(d){
      document.getElementById('bv').textContent=d.v!==undefined?d.v.toFixed(1)+'V':'---';
      document.getElementById('bc').textContent=d.a!==undefined?d.a.toFixed(2)+'A':'---';
      document.getElementById('rl').textContent=d.roll!==undefined?d.roll.toFixed(1)+'\xb0':'---';
      document.getElementById('pt').textContent=d.pitch!==undefined?d.pitch.toFixed(1)+'\xb0':'---';
    })
    .catch(function(){});
},500);
setInterval(function(){
  fetch('/status')
    .then(function(x){return x.json()})
    .then(function(d){
      var bg=document.getElementById('bg');
      bg.textContent=d.wet?'WET ⚠':'DRY';
      bg.style.color=d.wet?'#f59e0b':'#86efac';
      document.getElementById('pm').textContent=d.pump?'ON':'OFF';
      document.getElementById('pm').style.color=d.pump?'#f59e0b':'#86efac';
      if(d.pump!==pumpOn){
        pumpOn=d.pump;
        document.getElementById('pumpBtn').textContent='Pump: '+(pumpOn?'ON':'OFF');
        document.getElementById('pumpBtn').style.background=pumpOn?'#166534':'#374151';
      }
    })
    .catch(function(){});
},2000);
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

// Serves /tiles/{z}/{x}/{y}.png from the SD card (if mounted).
// All other unrecognised paths get a plain 404.
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
    s_srv.send(404, "text/plain", "Not found");
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
    WiFi.mode(WIFI_AP);
    // Explicit DHCP config required for reliable iOS association.
    // Without softAPConfig(), the ESP32 DHCP server sometimes delays
    // responding, causing iOS to show a spinning wheel indefinitely.
    IPAddress ip(192, 168, 4, 1);
    IPAddress gw(192, 168, 4, 1);
    IPAddress nm(255, 255, 255, 0);
    WiFi.softAPConfig(ip, gw, nm);
    // Channel 6 is clear of channels 1 and 11; avoids co-channel issues
    // with most home routers and improves iOS DHCP reliability.
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS, 6);
    Serial.printf("wifi_ctrl: AP up — SSID=%s  IP=%s\n",
                  WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());

    s_srv.on("/",          handle_root);
    s_srv.on("/map",       handle_map);
    s_srv.on("/control",   handle_control);
    s_srv.on("/telemetry", handle_telemetry);
    s_srv.on("/status",    handle_status);
    s_srv.on("/pump",      handle_pump);
    s_srv.on("/track",     handle_track);
    // OTA: GET serves the upload page; POST receives the binary + flashes + reboots.
    s_srv.on("/update", HTTP_GET,  handle_ota_get);
    s_srv.on("/update", HTTP_POST, handle_ota_post, handle_ota_upload);
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
