#include "wifi_ctrl.h"
#include "config.h"
#include "imu.h"
#include "power.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ── Control frame timeout ─────────────────────────────────────────────────────
#define CTRL_TIMEOUT_MS 500

// ── Embedded control page (served from Flash) ─────────────────────────────────
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
</div>
<script>
var armed=false;
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
      document.getElementById('rl').textContent=d.roll!==undefined?d.roll.toFixed(1)+'°':'---';
      document.getElementById('pt').textContent=d.pitch!==undefined?d.pitch.toFixed(1)+'°':'---';
    })
    .catch(function(){});
},500);
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

// ── AP lifecycle ──────────────────────────────────────────────────────────────
static void start_ap()
{
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    Serial.printf("wifi_ctrl: AP up — SSID=%s  IP=%s\n",
                  WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());

    s_srv.on("/",          handle_root);
    s_srv.on("/control",   handle_control);
    s_srv.on("/telemetry", handle_telemetry);
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
