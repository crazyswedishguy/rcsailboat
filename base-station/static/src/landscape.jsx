// landscape.jsx — Landscape helm layout (two-handed driving mode).
// Left column: sail trim slider.  Centre: mini-map + motor + release/stop.
// Right column: attitude horizon + rudder drag pad.
// All other tabs render in a scrollable area with a compact top bar.

// Props mirror the App's state for every control the landscape view needs:
//   T, tab, setTab, d, stale
//   sail, setSail, rudder, setRudder, rudderTrim, setRudderTrim
//   throttle, setThrottle, motorMode, setMotorMode
//   homePos, setHomePos, deviceStatus, consoleLines, consoleEnabled
//   controlRole, controllerLabel, pendingLabel, pendingExpiresIn, wsRef
//   theme, setTheme, setOrient
//   onRequestControl, onReleaseControl, onAcceptHandoff, onDismissRequest, onStop

// ── Leaflet mini-map for landscape control view ───────────────────────────────
const LandMap = ({ T, d, stale, homePos }) => {
  const { useRef, useEffect } = React;
  const containerRef = useRef(null);
  const lfRef        = useRef(null);

  function makeBoatIcon(L, hdg, isStale) {
    const color = isStale ? T.faint : T.accent;
    return L.divIcon({
      html: `<svg width="18" height="18" viewBox="0 0 22 22"
        style="overflow:visible;opacity:${isStale ? 0.55 : 1}">
        <circle cx="11" cy="11" r="8" fill="${T.surface}" stroke="${color}" stroke-width="2"/>
        <polygon points="11,3 14.5,15 11,13 7.5,15" fill="${color}"
          transform="rotate(${hdg||0},11,11)"/>
      </svg>`,
      iconSize: [18, 18], iconAnchor: [9, 9], className: '',
    });
  }

  function makeHomeIcon(L) {
    return L.divIcon({
      html: `<svg width="16" height="16" viewBox="0 0 20 20">
        <circle cx="10" cy="10" r="9" fill="${T.safe}" opacity="0.9"
          stroke="#fff" stroke-width="1.5"/>
        <text x="10" y="14" text-anchor="middle" font-size="10" font-weight="700"
          fill="#fff" font-family="monospace">H</text>
      </svg>`,
      iconSize: [16, 16], iconAnchor: [8, 8], className: '',
    });
  }

  useEffect(() => {
    const L = window.L;
    if (!L || !containerRef.current || lfRef.current) return;
    const center = homePos ? [homePos.lat, homePos.lng]
                 : (d.lat  ? [d.lat, d.lon] : [41.158, -73.769]); // Chappaqua, NY
    const map = L.map(containerRef.current, {
      center, zoom: 14, zoomControl: false, attributionControl: false,
    });
    L.tileLayer('/tiles/{z}/{x}/{y}.png', {
      maxZoom: 17, minZoom: 8,
      errorTileUrl: 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=',
    }).addTo(map);
    const initPos = (d.lat && d.lon) ? [d.lat, d.lon] : center;
    const boat = L.marker(initPos, { icon: makeBoatIcon(L, d.hdg, false), zIndexOffset: 100 }).addTo(map);
    const home = homePos ? L.marker([homePos.lat, homePos.lng], { icon: makeHomeIcon(L) }).addTo(map) : null;
    lfRef.current = { map, boat, home };
    return () => { map.remove(); lfRef.current = null; };
  }, []);

  useEffect(() => {
    const L = window.L;
    const lf = lfRef.current;
    if (!L || !lf) return;
    lf.boat.setIcon(makeBoatIcon(L, d.hdg, stale.gps));
    if (!d.lat || !d.lon || stale.gps) return;
    lf.boat.setLatLng([d.lat, d.lon]);
  }, [d.lat, d.lon, d.hdg, stale.gps]);

  useEffect(() => {
    const L = window.L;
    const lf = lfRef.current;
    if (!L || !lf) return;
    if (!homePos) { if (lf.home) { lf.home.remove(); lf.home = null; } return; }
    const p = [homePos.lat, homePos.lng];
    if (lf.home) { lf.home.setLatLng(p); }
    else { lf.home = L.marker(p, { icon: makeHomeIcon(L) }).addTo(lf.map); }
  }, [homePos]);

  if (!window.L) {
    return (
      <div style={{ width:'100%', height:'100%', display:'flex',
        alignItems:'center', justifyContent:'center', background:T.inset }}>
        <span style={{ fontFamily:_MONO, fontSize:9, color:T.faint }}>Map not ready</span>
      </div>
    );
  }
  return (
    <div style={{ position:'relative', width:'100%', height:'100%' }}>
      <div ref={containerRef} style={{ width:'100%', height:'100%',
        background: T.id==='dusk' ? '#0e1f36' : '#d4e4f0' }}/>
      {stale.gps && (
        <div style={{ position:'absolute', top:6, left:0, right:0, zIndex:1000,
          display:'flex', justifyContent:'center', pointerEvents:'none' }}>
          <div style={{ padding:'3px 8px', borderRadius:5,
            background: d.lat ? 'rgba(243,156,18,0.90)' : 'rgba(192,57,43,0.90)',
            color:'#fff', fontFamily:_MONO, fontSize:8, fontWeight:700,
            letterSpacing:'0.05em' }}>
            {d.lat ? 'GPS STALE' : 'NO GPS FIX'}
          </div>
        </div>
      )}
    </div>
  );
};

// ── Compact throttle control ──────────────────────────────────────────────────
const LandThrottle = ({ T, motorMode, throttle, setThrottle }) => {
  const { useRef, useState } = React;
  const ref = useRef(null);
  const [drag, setDrag] = useState(false);
  const engaged = motorMode !== 'off';
  const thrVal  = engaged ? Math.round(throttle*100) : 0;
  const pct     = 50 + thrVal/2;
  const fwd     = thrVal >= 0;
  const fc      = fwd ? T.warn : T.danger;
  function fromPtr(e) {
    const r = ref.current.getBoundingClientRect();
    return Math.max(-1, Math.min(1, ((e.clientX-r.left)/r.width - 0.5)*2));
  }
  return (
    <div style={{ display:'flex', alignItems:'center', gap:8 }}>
      <div ref={ref} style={{ position:'relative', flex:1, height:26,
        background:T.inset, borderRadius:13, border:`1px solid ${T.border}`,
        opacity:engaged?1:0.45, touchAction:'none',
        cursor:engaged?'ew-resize':'default' }}
        onPointerDown={e=>{ if(!engaged) return; setDrag(true); ref.current.setPointerCapture(e.pointerId); setThrottle(fromPtr(e)); }}
        onPointerMove={e=>{ if(drag) setThrottle(fromPtr(e)); }}
        onPointerUp={()=>{ setDrag(false); if(motorMode==='hold') setThrottle(0); }}
        onPointerCancel={()=>{ setDrag(false); if(motorMode==='hold') setThrottle(0); }}>
        <div style={{ position:'absolute',left:'50%',top:'18%',bottom:'18%',width:1.5,background:T.borderStrong,transform:'translateX(-50%)' }}/>
        {engaged && Math.abs(thrVal)>2 && (
          <div style={{ position:'absolute',top:'22%',bottom:'22%',
            left:fwd?'50%':`${pct}%`, width:`${Math.abs(thrVal)/2}%`,
            background:fc, opacity:0.2, borderRadius:5 }}/>
        )}
        {engaged
          ? <div style={{ position:'absolute',top:'50%',left:`${pct}%`,
              width:24,height:24,transform:'translate(-50%,-50%)',borderRadius:'50%',
              background:T.surface, border:`2px solid ${fc}`,
              boxShadow:'0 1px 4px rgba(28,52,86,0.16)',
              transition:drag?'none':'left 0.05s' }}/>
          : <span style={{ position:'absolute',top:0,left:0,right:0,bottom:0,
              display:'flex',alignItems:'center',justifyContent:'center',
              fontFamily:_MONO,fontSize:8,fontWeight:600,letterSpacing:'0.10em',
              textTransform:'uppercase',color:T.faint }}>
              Pick Hold or Set
            </span>
        }
        <span style={{ position:'absolute',left:6,bottom:2,fontFamily:_MONO,fontSize:7,fontWeight:700,color:T.faint }}>REV</span>
        <span style={{ position:'absolute',right:6,bottom:2,fontFamily:_MONO,fontSize:7,fontWeight:700,color:T.faint }}>FWD</span>
      </div>
      <span style={{ fontFamily:_MONO,fontWeight:700,fontSize:12,
        color:engaged?fc:T.faint, fontFeatureSettings:'"tnum","zero"' }}>
        {thrVal>0?'+':''}{thrVal}%
      </span>
      {motorMode==='hold' && <span style={{ fontFamily:_MONO,fontSize:9,fontWeight:700,color:T.warn,padding:'4px 7px',borderRadius:6,background:T.warnSoft,whiteSpace:'nowrap' }}>Hold</span>}
      {motorMode==='set'  && <span onClick={()=>setThrottle(0)} style={{ fontFamily:_MONO,fontSize:9,fontWeight:700,color:T.danger,padding:'4px 7px',borderRadius:6,background:T.dangerSoft,border:`1px solid ${T.danger}`,whiteSpace:'nowrap',cursor:'pointer' }}>⊘ Neut</span>}
    </div>
  );
};

// ── Main landscape component ──────────────────────────────────────────────────
const Landscape = ({
  T, tab, setTab, d, stale,
  sail, setSail, rudder, setRudder, rudderTrim, setRudderTrim,
  throttle, setThrottle, motorMode, setMotorMode,
  homePos, setHomePos, deviceStatus, consoleLines, consoleEnabled,
  controlRole, controllerLabel, pendingLabel, pendingExpiresIn, wsRef,
  theme, setTheme, setOrient,
  onRequestControl, onReleaseControl, onAcceptHandoff, onDismissRequest, onPump, onStop,
}) => {
  const { useRef, useState } = React;
  const inControl = controlRole === 'controller';
  const engaged   = motorMode !== 'off';

  const battLow  = !stale.telem && d.chg < 20;
  const battCrit = !stale.telem && d.chg < 10;

  const lbl = (x) => ({ fontFamily:_MONO, fontSize:8.5, fontWeight:600,
    letterSpacing:'0.12em', textTransform:'uppercase', color:T.dim, ...(x||{}) });
  const val = (s,c) => ({ fontFamily:_MONO, fontWeight:700, fontSize:s||14,
    color:c||T.text, fontFeatureSettings:'"tnum","zero"', lineHeight:1 });
  const card = { background:T.surface, border:`1px solid ${T.border}`,
    borderRadius:14, boxShadow:'0 1px 3px rgba(28,52,86,0.05)' };

  const TABS = [['control','Control'],['instr','Instr'],['map','Map'],['devices','Devices'],['console','Console']];

  // Status pill label and style — shows who has the helm
  const pillLabel = inControl ? 'Helm active'
    : controlRole==='requesting' ? `Requesting…`
    : controllerLabel ? `${controllerLabel} helming`
    : stale.telem ? 'No link'
    : 'Observer';
  const pillBg = inControl ? T.accent
    : controlRole==='requesting' ? T.warnSoft
    : stale.telem ? T.dangerSoft
    : T.safeSoft;
  const pillColor = inControl ? '#fff'
    : controlRole==='requesting' ? T.warn
    : stale.telem ? T.danger
    : T.safe;

  // Rudder drag for right column
  const rudRef = useRef(null);
  const [rudDrag, setRudDrag] = useState(false);
  function rudFromPtr(e) {
    const r = rudRef.current.getBoundingClientRect();
    return Math.max(-1, Math.min(1, ((e.clientX-r.left)/r.width - 0.5)*2));
  }

  return (
    <div style={{ width:'100%', height:'100%', background:T.bg,
      display:'flex', flexDirection:'column', overflow:'hidden' }}>

      {/* ── Top bar ──────────────────────────────────────────────────────── */}
      <div style={{ display:'flex', alignItems:'center', gap:10, padding:'6px 14px',
        background:T.surface, borderBottom:`2px solid ${T.accent}`, flexShrink:0 }}>
        {/* Logo + wordmark */}
        <div style={{ display:'flex', alignItems:'center', gap:8, flexShrink:0 }}>
          <Logo T={T} height={20}/>
          <span style={{ fontSize:15, fontWeight:700, letterSpacing:'-0.01em', color:T.accent }}>Mistral</span>
        </div>
        {/* Tab rail */}
        <div style={{ display:'flex', gap:1, padding:3, borderRadius:10,
          background:T.inset, border:`1px solid ${T.border}`, flexShrink:0 }}>
          {TABS.map(([id,label]) => {
            const on = id===tab;
            return (
              <div key={id} onClick={()=>setTab(id)} style={{ padding:'5px 11px',
                borderRadius:7, background:on?T.surface:'transparent',
                boxShadow:on?'0 1px 2px rgba(28,52,86,0.10)':'none',
                fontFamily:_MONO, fontSize:10, fontWeight:700,
                color:on?T.accent:T.faint, cursor:'pointer', whiteSpace:'nowrap' }}>
                {label}
              </div>
            );
          })}
        </div>
        <span style={{ flex:1 }}/>
        {/* Vital readouts */}
        {[['Batt',`${d.chg}%`, battCrit?T.danger:battLow?T.warn:null],['LQ',`${d.lq}%`],['Sats',`${d.sats}`],['Spd',`${d.spd.toFixed(1)} kn`]].map(([k,v,c]) => (
          <div key={k} style={{ display:'flex', alignItems:'baseline', gap:5 }}>
            <span style={lbl({ fontSize:8 })}>{k}</span>
            <span style={val(11.5, c||T.text)}>{v}</span>
          </div>
        ))}
        {/* Bilge status + pump toggle */}
        <div style={{ display:'flex', alignItems:'center', gap:6 }}>
          <span style={{ width:6, height:6, borderRadius:99, flexShrink:0,
            background: d.bilgeWet ? T.danger : T.safe }}/>
          {d.capsized && (
            <span style={{ fontFamily:_MONO, fontSize:8, fontWeight:700, color:T.danger }}>
              CAPSIZED
            </span>
          )}
          <div onClick={() => inControl && onPump && onPump(!d.pumpActive)}
            style={{ fontFamily:_MONO, fontSize:8, fontWeight:700,
              color: d.pumpActive ? T.warn : T.dim,
              padding:'3px 7px', borderRadius:5,
              background: d.pumpActive ? T.warnSoft : T.inset,
              border:`1px solid ${d.pumpActive ? T.warn : T.border}`,
              cursor: inControl ? 'pointer' : 'default',
              opacity: inControl ? 1 : 0.40 }}>
            PUMP {d.pumpActive ? 'ON' : 'OFF'}
          </div>
        </div>
        {/* Theme toggle */}
        <div onClick={()=>setTheme(t=>t==='day'?'dusk':'day')}
          style={{ fontFamily:_MONO,fontSize:9,fontWeight:700,color:T.faint,
            padding:'4px 9px',borderRadius:6,background:T.inset,
            border:`1px solid ${T.border}`,cursor:'pointer',flexShrink:0 }}>
          {theme==='day'?'☀ DAY':'☾ DUSK'}
        </div>
        {/* Orientation toggle */}
        <div onClick={()=>setOrient('portrait')}
          style={{ fontFamily:_MONO,fontSize:9,fontWeight:700,color:T.faint,
            padding:'4px 9px',borderRadius:6,background:T.inset,
            border:`1px solid ${T.border}`,cursor:'pointer',flexShrink:0 }}>
          ⇅ PORTRAIT
        </div>
        {/* Helm status pill */}
        <div onClick={()=>{ if(tab==='control') inControl?onReleaseControl():onRequestControl(); }}
          style={{ display:'inline-flex',alignItems:'center',gap:6,
            padding:'5px 11px',borderRadius:99, background:pillBg,
            color:pillColor, fontSize:10,fontWeight:700,
            letterSpacing:'0.04em',textTransform:'uppercase',
            flexShrink:0,cursor:tab==='control'?'pointer':'default' }}>
          <span style={{ width:6,height:6,borderRadius:99,background:pillColor }}/>
          {pillLabel}
        </div>
      </div>

      {/* ── Low battery warning ──────────────────────────────────────────────── */}
      {battLow && (
        <div style={{ display:'flex',alignItems:'center',gap:8,padding:'6px 16px',
          background:battCrit?T.dangerSoft:T.warnSoft,
          borderBottom:`1px solid ${battCrit?T.danger:T.warn}`,flexShrink:0 }}>
          <span style={{ fontFamily:_MONO,fontSize:10,fontWeight:700,
            color:battCrit?T.danger:T.warn }}>
            {battCrit?'⚠ BATTERY CRITICAL':'⚠ BATTERY LOW'} · {d.volts.toFixed(1)} V · {d.chg}%
          </span>
        </div>
      )}

      {/* ── Helm take/request banner (Control tab only, when not in control) */}
      {tab==='control' && !inControl && (
        <div onClick={onRequestControl}
          style={{ display:'flex',alignItems:'center',gap:10,padding:'8px 16px',
            background:controlRole==='requesting'?T.warnSoft:T.inset,
            borderBottom:`1px solid ${T.border}`,cursor:'pointer',flexShrink:0 }}>
          <span style={{ fontFamily:_MONO,fontSize:11,fontWeight:700,
            letterSpacing:'0.09em',
            color:controlRole==='requesting'?T.warn:T.dim }}>
            {controlRole==='requesting'
              ? `REQUESTING HELM…${pendingExpiresIn!=null?` ${pendingExpiresIn}s`:''}`
              : controllerLabel ? `TAP TO REQUEST HELM FROM ${controllerLabel.toUpperCase()}`
              : 'TAP TO TAKE HELM'}
          </span>
          <span style={{ flex:1 }}/>
          <span style={{ fontFamily:_MONO,fontSize:9,color:T.faint }}>STOP CUTS POWER · CENTRES RUDDER</span>
        </div>
      )}

      {/* ── Pending request notification (controller sees this) ──────────── */}
      {tab==='control' && inControl && pendingLabel && (
        <div style={{ display:'flex',alignItems:'center',gap:8,padding:'7px 16px',
          background:T.accentSoft,borderBottom:`1px solid ${T.accentLine}`,flexShrink:0 }}>
          <span style={{ fontFamily:_MONO,fontSize:10,fontWeight:700,color:T.accent,flex:1 }}>
            {pendingLabel} wants the helm{pendingExpiresIn!=null?` · ${pendingExpiresIn}s`:''}
          </span>
          <div onClick={onAcceptHandoff} style={{ padding:'4px 12px',borderRadius:7,
            background:T.safeSoft,border:`1px solid ${T.safe}`,
            fontFamily:_MONO,fontSize:10,fontWeight:700,color:T.safe,cursor:'pointer' }}>
            Hand off
          </div>
          <div onClick={onDismissRequest} style={{ padding:'4px 12px',borderRadius:7,
            background:T.inset,border:`1px solid ${T.border}`,
            fontFamily:_MONO,fontSize:10,fontWeight:700,color:T.dim,cursor:'pointer' }}>
            Dismiss
          </div>
        </div>
      )}

      {/* ── Body: Control layout or scrollable other tabs ─────────────────── */}
      {tab==='control' ? (
        <div style={{ flex:1, display:'flex', gap:10, padding:11, minHeight:0 }}>

          {/* LEFT — Sail trim (SailArc widget + step buttons) */}
          <div style={{ ...card, width:136, padding:12, display:'flex',
            flexDirection:'column', alignItems:'center', gap:8,
            opacity:inControl?1:0.38, pointerEvents:inControl?'auto':'none',
            transition:'opacity 0.15s' }}>
            <div style={{ display:'flex', alignItems:'baseline',
              justifyContent:'space-between', width:'100%' }}>
              <span style={lbl()}>Sail</span>
              <span style={val(14,T.accent)}>{d.sail}%</span>
            </div>
            {/* SailArc widget — draggable, same as portrait */}
            <SailArc value={sail} size={108} T={T} onChange={setSail}/>
            {/* Step buttons for fine adjustment */}
            <div style={{ display:'grid', gridTemplateColumns:'1fr 1fr', gap:5, width:'100%' }}>
              {[['◀ IN', () => setSail(s=>Math.max(0,s-0.05))],
                ['OUT ▶', () => setSail(s=>Math.min(1,s+0.05))]
              ].map(([label,fn]) => (
                <div key={label} onClick={fn} style={{ textAlign:'center',
                  padding:'6px 4px', borderRadius:7, background:T.inset,
                  border:`1px solid ${T.border}`, fontFamily:_MONO, fontSize:10,
                  fontWeight:700, color:T.dim, cursor:'pointer' }}>
                  {label}
                </div>
              ))}
            </div>
          </div>

          {/* CENTRE — Map + motor + buttons */}
          <div style={{ flex:1, display:'flex', flexDirection:'column', gap:10, minWidth:0 }}>
            {/* Mini map */}
            <div style={{ ...card, flex:1, overflow:'hidden', position:'relative', minHeight:0 }}>
              <LandMap T={T} d={d} stale={stale} homePos={homePos}/>
              {/* Distance badge */}
              <div style={{ position:'absolute',top:8,right:10,
                display:'flex',alignItems:'center',gap:6,padding:'4px 9px',
                borderRadius:8,background:T.id==='dusk'?'rgba(18,29,46,0.88)':'rgba(255,255,255,0.88)',
                border:`1px solid ${T.border}`,whiteSpace:'nowrap' }}>
                <span style={val(12)}>{d.homeDist||0} m</span>
                <span style={val(10,T.dim)}>{String(d.homeBrg||0).padStart(3,'0')}°</span>
              </div>
              {/* Vitals overlay at bottom */}
              <div style={{ position:'absolute',left:0,right:0,bottom:0,
                display:'flex',gap:14,padding:'6px 12px',
                background:T.id==='dusk'?'linear-gradient(transparent,rgba(18,29,46,0.90) 50%)':'linear-gradient(transparent,rgba(255,255,255,0.90) 50%)' }}>
                {[['Hdg',`${d.hdg}°`],['Heel',`${d.roll.toFixed(1)}°`],['Pitch',`${d.pitch.toFixed(1)}°`]].map(([k,v])=>(
                  <div key={k} style={{ display:'flex',alignItems:'baseline',gap:5 }}>
                    <span style={lbl({fontSize:8})}>{k}</span>
                    <span style={val(11.5)}>{v}</span>
                  </div>
                ))}
              </div>
            </div>
            {/* Motor card + release/stop */}
            <div style={{ display:'flex', gap:10, flexShrink:0 }}>
              <div style={{ ...card, flex:1, padding:'8px 12px',
                display:'flex',flexDirection:'column',gap:7,
                border:engaged?`1.5px solid ${T.warn}`:`1px solid ${T.border}`,
                opacity:inControl?1:0.38, pointerEvents:inControl?'auto':'none',
                transition:'opacity 0.15s' }}>
                <div style={{ display:'flex',alignItems:'center',gap:8 }}>
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                    stroke={engaged?T.warn:T.dim} strokeWidth="2"
                    strokeLinecap="round" strokeLinejoin="round">
                    <circle cx="12" cy="12" r="3.4"/>
                    <path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1"/>
                  </svg>
                  <span style={{ fontSize:11.5,fontWeight:700,color:engaged?T.warn:T.text }}>Motor</span>
                  <span style={lbl({fontSize:7.5,color:T.faint})}>· aux</span>
                  <span style={{ flex:1 }}/>
                  <div style={{ display:'flex',gap:2,padding:2,borderRadius:8,background:T.inset,border:`1px solid ${T.border}` }}>
                    {[['off','Off'],['hold','Hold'],['set','Set']].map(([id,label])=>{
                      const on=id===motorMode;
                      return <div key={id} onClick={()=>{setMotorMode(id);if(id==='off'||id==='hold')setThrottle(0);}}
                        style={{ padding:'4px 9px',borderRadius:6,background:on?T.surface:'transparent',
                          boxShadow:on?'0 1px 2px rgba(28,52,86,0.10)':'none',
                          fontSize:10,fontWeight:700,cursor:'pointer',
                          color:on?(id==='off'?T.dim:T.warn):T.dim }}>{label}</div>;
                    })}
                  </div>
                </div>
                <LandThrottle T={T} motorMode={motorMode} throttle={throttle} setThrottle={setThrottle}/>
              </div>
              <div onClick={onStop}
                style={{ width:130,borderRadius:12,background:T.danger,color:'#fff',
                  fontSize:15,fontWeight:800,letterSpacing:'0.08em',
                  display:'flex',alignItems:'center',justifyContent:'center',gap:8,cursor:'pointer' }}>
                <span style={{ width:11,height:11,background:'#fff',borderRadius:2 }}/>STOP
              </div>
            </div>
          </div>

          {/* RIGHT — Attitude + Rudder */}
          <div style={{ width:210,display:'flex',flexDirection:'column',gap:10,minHeight:0 }}>
            {/* Attitude card */}
            <div style={{ ...card,padding:'10px 12px',display:'flex',alignItems:'center',gap:12,flexShrink:0 }}>
              <AttHorizon roll={d.roll} pitch={d.pitch} size={68} T={T}/>
              <div style={{ display:'grid',gridTemplateColumns:'1fr 1fr',gap:'6px 10px' }}>
                {[['Heel',`${d.roll.toFixed(1)}°`],['Pitch',`${d.pitch.toFixed(1)}°`]].map(([k,v])=>(
                  <div key={k} style={{ display:'flex',flexDirection:'column',gap:2 }}>
                    <span style={lbl({fontSize:8})}>{k}</span>
                    <span style={val(14)}>{v}</span>
                  </div>
                ))}
              </div>
            </div>
            {/* Rudder card */}
            <div style={{ ...card,flex:1,padding:'11px 12px',display:'flex',
              flexDirection:'column',gap:8,minHeight:0,
              opacity:inControl?1:0.38, pointerEvents:inControl?'auto':'none',
              transition:'opacity 0.15s' }}>
              <div style={{ display:'flex',alignItems:'baseline',justifyContent:'space-between' }}>
                <span style={lbl()}>Rudder</span>
                <div style={{ display:'flex',gap:9,alignItems:'baseline' }}>
                  <span style={{ fontFamily:_MONO,fontSize:9,fontWeight:700,color:T.warn }}>
                    Trim {rudderTrim>=0?'+':''}{Math.round(rudderTrim*100)}%
                  </span>
                  <span style={val(14,T.accent)}>{d.rud>=0?'+':''}{d.rud}%</span>
                </div>
              </div>
              <div ref={rudRef} style={{ position:'relative',flex:1,minHeight:0,
                background:T.inset,borderRadius:12,border:`1px solid ${T.border}`,
                overflow:'hidden',touchAction:'none',cursor:'ew-resize' }}
                onPointerDown={e=>{setRudDrag(true);rudRef.current.setPointerCapture(e.pointerId);setRudder(rudFromPtr(e));}}
                onPointerMove={e=>{if(rudDrag)setRudder(rudFromPtr(e));}}
                onPointerUp={()=>{setRudDrag(false);setRudder(rudderTrim);}}
                onPointerCancel={()=>{setRudDrag(false);setRudder(rudderTrim);}}>
                <div style={{ position:'absolute',left:'50%',top:'12%',bottom:'12%',
                  width:1.5,background:T.borderStrong,transform:'translateX(-50%)' }}/>
                <div style={{ position:'absolute',top:0,bottom:0,
                  left:`${(rudderTrim*0.42+0.5)*100}%`,
                  width:2,background:T.warn,opacity:0.85,transform:'translateX(-50%)' }}/>
                <div style={{ position:'absolute',top:'50%',
                  left:`${(rudder*0.42+0.5)*100}%`,
                  width:50,height:50,transform:'translate(-50%,-50%)',
                  borderRadius:'50%',background:T.surface,
                  border:`2.5px solid ${T.accent}`,
                  boxShadow:'0 2px 6px rgba(28,52,86,0.18)',
                  display:'flex',alignItems:'center',justifyContent:'center',
                  transition:rudDrag?'none':'left 0.05s' }}>
                  <span style={val(11,T.accent)}>{d.rud>=0?'+':''}{d.rud}%</span>
                </div>
                <span style={{ position:'absolute',left:9,bottom:6,fontFamily:_MONO,fontSize:8.5,fontWeight:700,letterSpacing:'0.06em',textTransform:'uppercase',color:T.faint }}>Port</span>
                <span style={{ position:'absolute',right:9,bottom:6,fontFamily:_MONO,fontSize:8.5,fontWeight:700,letterSpacing:'0.06em',textTransform:'uppercase',color:T.faint }}>Stbd</span>
                <span style={{ position:'absolute',left:0,right:0,top:7,textAlign:'center',fontFamily:_MONO,fontSize:7.5,fontWeight:600,color:T.faint }}>drag · self-centres</span>
              </div>
              <div style={{ display:'grid',gridTemplateColumns:'1fr 1.3fr 1fr',gap:5 }}>
                {[['−',()=>{ const t=Math.round(Math.max(-100,rudderTrim*100-1))/100; setRudderTrim(t); setRudder(t); }],
                  ['Centre',()=>{ setRudderTrim(0); setRudder(0); }],
                  ['+',()=>{ const t=Math.round(Math.min(100,rudderTrim*100+1))/100; setRudderTrim(t); setRudder(t); }]
                ].map(([label,fn])=>(
                  <div key={label} onClick={fn} style={{ textAlign:'center',padding:'8px 4px',
                    borderRadius:7,background:T.inset,border:`1px solid ${T.border}`,
                    fontFamily:_MONO,fontSize:11,fontWeight:700,color:T.dim,cursor:'pointer' }}>
                    {label}
                  </div>
                ))}
              </div>
            </div>
          </div>
        </div>

      ) : (
        /* Other tabs: scroll in remaining space */
        <div style={{ flex:1, overflowY:'auto', background:T.bg }}>
          {tab==='instr'   && <Instr T={T} d={d} stale={stale}/>}
          {tab==='map'     && <MapTab T={T} d={d} stale={stale} homePos={homePos} setHomePos={setHomePos}/>}
          {tab==='devices' && <Dev T={T} d={d} stale={stale} deviceStatus={deviceStatus}/>}
          {tab==='console' && <Con T={T} lines={consoleLines} enabled={consoleEnabled}/>}
        </div>
      )}
    </div>
  );
};
