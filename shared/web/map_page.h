#pragma once
// Embedded GPS track viewer page — served by both the boat (wifi_ctrl.cpp)
// and the XIAO bridge (crsf-bridge main.cpp).
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
