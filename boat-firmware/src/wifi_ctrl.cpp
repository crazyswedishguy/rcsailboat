// wifi_ctrl.cpp — WiFi Direct AP + embedded web control server.
//
// The ESP32 broadcasts its own Wi-Fi access point ("Mistral") so any phone,
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
#ifdef COMPASS_ENABLED
#include "compass.h"
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
#include "web/control_page.h"  // HTML_PAGE[] PROGMEM — shared with XIAO bridge

// ── Embedded GPS track map page ───────────────────────────────────────────────
#include "web/map_page.h"      // MAP_HTML[] PROGMEM — shared with XIAO bridge

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

// Deferred restart — set by handle_restart(), executed in wifi_ctrl_update()
// so the HTTP response is sent and the TCP connection drains before reboot.
static bool          s_restart_pending = false;
static unsigned long s_restart_ms      = 0;

// ── HTTP handlers ─────────────────────────────────────────────────────────────

static void handle_diag_page()
{
    s_srv.send_P(200, "text/html; charset=utf-8", DIAG_HTML);
}

static void handle_diag_json()
{
    // String IDs match HTML card element IDs. numId is the numeric DEV_* index
    // for the repair endpoint (/repair?id=numId). Non-I2C devices omit numId.
    char buf[2048];
    int  n = 0;

    static const char *DEV_IDS[] = {"ft", "qmi", "pca", "ina"};

    n += snprintf(buf, sizeof(buf), "[");

    // ── I2C devices (repairable via /repair?id=numId) — ft (touch) omitted ─
    bool first = true;
    for (uint8_t i = 0; i < DEV_COUNT; i++) {
        if (i == DEV_FT3168) continue;  // touch controller not surfaced in device status
        const DeviceInfo *d  = diag_info((DevId)i);
        bool              ok = d->present && d->enabled;
        n += snprintf(buf + n, sizeof(buf) - n,
            "%s{\"id\":\"%s\",\"numId\":%d,\"name\":\"%s\",\"role\":\"%s\","
            "\"addr\":\"0x%02X\",\"level\":\"%s\",\"stat\":\"%s\","
            "\"repairable\":true}",
            first ? "" : ",", DEV_IDS[i], (int)i, d->name, d->role, d->addr,
            ok ? "ok" : "absent",
            ok ? "OK"  : "Absent");
        first = false;
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
    // Shows "warn/Unverified" until the sensor has triggered at least once,
    // confirming physical connection. With INPUT_PULLUP a disconnected pin
    // reads HIGH (same as dry), so we cannot confirm connection any other way.
    {
        bool wet      = bilge_water_detected();
        bool verified = bilge_sensor_verified();
        n += snprintf(buf + n, sizeof(buf) - n,
            ",{\"id\":\"bilge\",\"name\":\"Bilge Sensor\",\"role\":\"Water detection\","
            "\"level\":\"%s\",\"stat\":\"%s\",\"repairable\":false}",
            wet ? "warn" : (verified ? "ok" : "warn"),
            wet ? "WET"  : (verified ? "Dry" : "Unverified"));
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

    // ── GPS (UART — not I²C, not repairable) ──────────────────────────────────
#ifdef GPS_ENABLED
    {
        bool  fix  = gps_has_fix();
        uint8_t sv = gps_satellites();
        char   sv_stat[24];
        if (fix) snprintf(sv_stat, sizeof(sv_stat), "%u sats", (unsigned)sv);
        else     snprintf(sv_stat, sizeof(sv_stat), "No fix");
        n += snprintf(buf + n, sizeof(buf) - n,
            ",{\"id\":\"gps\",\"name\":\"GPS\",\"role\":\"Navigation\","
            "\"level\":\"%s\",\"stat\":\"%s\",\"repairable\":false}",
            fix ? "ok" : "warn", sv_stat);
    }
#else
    n += snprintf(buf + n, sizeof(buf) - n,
        ",{\"id\":\"gps\",\"name\":\"GPS\",\"role\":\"Navigation\","
        "\"level\":\"absent\",\"stat\":\"Not compiled in\",\"repairable\":false}");
#endif

    // ── Compass (I²C 0x1E, handled outside diag table — BN-880 external module) ──
#ifdef COMPASS_ENABLED
    {
        bool ok = compass_ok();
        n += snprintf(buf + n, sizeof(buf) - n,
            ",{\"id\":\"compass\",\"name\":\"Compass\",\"role\":\"Magnetic heading\","
            "\"level\":\"%s\",\"stat\":\"%s\",\"repairable\":false}",
            ok ? "ok" : "warn",
            ok ? "Ready" : "Not found");
    }
#else
    n += snprintf(buf + n, sizeof(buf) - n,
        ",{\"id\":\"compass\",\"name\":\"Compass\",\"role\":\"Magnetic heading\","
        "\"level\":\"absent\",\"stat\":\"Not compiled in\",\"repairable\":false}");
#endif

    // ── ELRS RX (UART — not I²C, not repairable) ──────────────────────────────
#ifdef FORCE_ELRS_MODE
    n += snprintf(buf + n, sizeof(buf) - n,
        ",{\"id\":\"elrs\",\"name\":\"ELRS RX\",\"role\":\"RC control\","
        "\"level\":\"ok\",\"stat\":\"Active\",\"repairable\":false}");
#endif

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

static void handle_restart()
{
    s_srv.send(200, "text/plain", "Rebooting...");
    // Don't call ESP.restart() here — we're still inside the HTTP handler.
    // Set a flag and let wifi_ctrl_update() restart after the response drains.
    s_restart_pending = true;
    s_restart_ms      = millis();
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
    float v   = power_voltage_v();
    float a   = power_current_a();
    float mah = power_mah_used();
    // Crude 3S LiPo SoC from voltage — no coulomb counter in WiFi mode
    int   pct = (int)constrain((v - 10.5f) / (12.6f - 10.5f) * 100.0f, 0.0f, 100.0f);
    float tmp = temperatureRead();   // ESP32-S3 internal sensor (°C)

    char buf[320];
    int n = snprintf(buf, sizeof(buf),
        "{\"v\":%.2f,\"a\":%.2f,\"mah\":%.0f,\"pct\":%d,\"temp\":%.0f"
        ",\"roll\":%.1f,\"pitch\":%.1f",
        v, a, mah, pct, tmp, imu_roll_deg(), imu_pitch_deg());

    // Heading: compass takes priority (accurate at rest); GPS CoG as fallback.
#if defined(COMPASS_ENABLED)
    n += snprintf(buf + n, sizeof(buf) - n, ",\"hdg\":%.0f", compass_heading_deg());
#elif defined(GPS_ENABLED)
    n += snprintf(buf + n, sizeof(buf) - n, ",\"hdg\":%.0f", gps_heading_deg());
#endif

#ifdef GPS_ENABLED
    n += snprintf(buf + n, sizeof(buf) - n,
        ",\"lat\":%.6f,\"lng\":%.6f,\"alt\":%.0f"
        ",\"speed\":%.1f,\"sats\":%d,\"fix\":%s",
        gps_lat(), gps_lng(), gps_altitude_m(),
        gps_speed_kn(),
        (int)gps_satellites(),
        gps_has_fix() ? "true" : "false");
#endif

    snprintf(buf + n, sizeof(buf) - n, "}");
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
    s_srv.on("/repair",    HTTP_GET,  handle_repair);
    s_srv.on("/restart",   HTTP_POST, handle_restart);
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
#ifdef FORCE_ELRS_MODE
    // Skip AP startup — boot directly in ELRS mode for bench testing.
    s_mode = CtrlMode::ELRS;
    Serial.println("wifi_ctrl: FORCE_ELRS_MODE — AP skipped, starting in ELRS mode");
#else
    start_ap();
    s_mode = CtrlMode::WIFI;
#endif
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

    // Deferred restart: give TCP 400 ms to drain after the response was sent
    if (s_restart_pending && (millis() - s_restart_ms) >= 400) {
        Serial.println("wifi_ctrl: restarting now");
        ESP.restart();
    }

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
