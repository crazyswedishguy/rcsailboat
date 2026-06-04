// ctrl.jsx — Control tab (portrait).
// The primary helming screen: attitude, bilge, sail trim, rudder, and motor.
// All control inputs (sail/rudder/throttle) are local React state managed in
// the parent App and passed down — changes here trigger WS sends in App.

// Props:
//   T             — active theme object (from THEMES)
//   d             — telemetry + control data object (built in App)
//   stale         — { telem, gps, link } staleness flags
//   sail          — normalised sail trim 0.0 (eased) … 1.0 (trimmed)
//   setSail       — setter for sail
//   rudder        — normalised rudder position -1.0 (port) … +1.0 (starboard)
//   setRudder     — setter for rudder
//   rudderTrim    — normalised trim offset (rudder self-centres here on release)
//   setRudderTrim — setter for rudderTrim
//   throttle      — normalised throttle -1.0 (rev) … +1.0 (fwd)
//   setThrottle   — setter for throttle
//   motorMode     — 'off' | 'hold' | 'set'
//   setMotorMode  — setter for motorMode
//   onRelease     — callback: release helm control
//   onStop        — callback: emergency stop (zeros throttle then releases)

const Ctrl = ({ T, d, stale, sail, setSail, rudder, setRudder,
                rudderTrim, setRudderTrim, throttle, setThrottle,
                motorMode, setMotorMode, onStop }) => {

  const { useRef, useState } = React;

  // ── Rudder drag ─────────────────────────────────────────────────────────────
  // Pointer events are used (instead of mouse/touch) so the browser routes both
  // touch and mouse through a single handler and we can capture the pointer to
  // receive moves outside the element.
  const rudRef = useRef(null);
  const [rudDrag, setRudDrag] = useState(false);

  function rudFromPtr(e) {
    const r = rudRef.current.getBoundingClientRect();
    // Map pointer x within track → -1.0 … +1.0
    return Math.max(-1, Math.min(1, ((e.clientX - r.left) / r.width - 0.5) * 2));
  }

  // ── Throttle drag ───────────────────────────────────────────────────────────
  const thrRef = useRef(null);
  const [thrDrag, setThrDrag] = useState(false);

  function thrFromPtr(e) {
    const r = thrRef.current.getBoundingClientRect();
    return Math.max(-1, Math.min(1, ((e.clientX - r.left) / r.width - 0.5) * 2));
  }

  // ── Derived display values ──────────────────────────────────────────────────
  const motorOn = motorMode !== 'off';
  // Throttle value shown on the track — only applies in Hold/Set modes
  const thrVal  = motorOn ? d.thr : 0;
  const thrPct  = 50 + thrVal / 2;           // 0–100 position along the track
  const thrFwd  = thrVal >= 0;
  const thrFill = thrFwd ? T.warn : T.danger; // amber = forward, red = reverse

  // Inline style factories — keeps JSX readable without a CSS file
  const lbl = (x) => ({ fontFamily:_MONO, fontSize:8.5, fontWeight:600,
    letterSpacing:'0.13em', textTransform:'uppercase', color:T.dim, ...(x||{}) });
  const val = (s,c) => ({ fontFamily:_MONO, fontWeight:700, fontSize:s||21,
    color:c||T.text, fontFeatureSettings:'"tnum","zero"', lineHeight:1 });
  const btnStyle = (bg,col,brd) => ({
    display:'flex', alignItems:'center', justifyContent:'center',
    padding:'13px 8px', borderRadius:8, background:bg, color:col,
    border:brd||'none', fontFamily:_MONO, fontSize:12, fontWeight:800,
    letterSpacing:'0.06em', cursor:'pointer', userSelect:'none',
  });

  return (
    <div style={{ display:'flex', flexDirection:'column' }}>

      {/* ── Attitude + nav 2×2 grid ─────────────────────────────────────────── */}
      <Card T={T} style={{ display:'flex' }}>
        <div style={{ padding:'13px 14px', borderRight:`1px solid ${T.border}`, flexShrink:0 }}>
          <AttHorizon roll={d.roll} pitch={d.pitch} size={100} T={T}/>
        </div>
        <div style={{ flex:1, display:'grid', gridTemplateRows:'1fr 1fr',
          gridTemplateColumns:'1fr 1fr' }}>
          {[
            ['HEEL',  sc(stale.telem, null, T), `${d.roll>=0?'+':''}${d.roll.toFixed(1)}°`],
            ['HDG',   sc(stale.telem, null, T), `${d.hdg}°`],
            ['SPEED', sc(stale.gps,   null, T), `${d.spd.toFixed(1)} kn`],
            ['HOME',  null,                      d.homeDist ? `${d.homeDist}m` : '—'],
          ].map(([label, col, value], i) => (
            <div key={label} style={{ padding:'10px 13px', display:'flex',
              flexDirection:'column', gap:4, justifyContent:'center',
              borderRight:i%2===0?`1px solid ${T.border}`:'none',
              borderTop:i>=2?`1px solid ${T.border}`:'none' }}>
              <div style={lbl()}>{label}</div>
              {label==='HOME'
                ? <div style={{ display:'flex', alignItems:'center', gap:5 }}>
                    {/* Arrow icon rotates to show bearing toward home */}
                    <svg width="12" height="12" viewBox="0 0 24 24" fill={T.accent}
                      style={{ transform:`rotate(${d.homeBrg}deg)`, flexShrink:0 }}>
                      <path d="M12 2l5 10-5-3-5 3z"/>
                    </svg>
                    <span style={val(17, col||T.text)}>{value}</span>
                  </div>
                : <span style={val(17, col||T.text)}>{value}</span>}
            </div>
          ))}
        </div>
      </Card>

      {/* ── Bilge row ──────────────────────────────────────────────────────────*/}
      <Inset T={T} style={{ display:'flex', alignItems:'center', gap:9,
        padding:'8px 16px', borderBottom:`1px solid ${T.border}` }}>
        <span style={{ width:7, height:7, borderRadius:99, flexShrink:0,
          background: d.bilgeWet ? T.danger : T.safe }}/>
        <span style={lbl({ color:T.text })}>
          {d.bilgeWet ? 'WATER IN BILGE' : 'BILGE DRY'}
        </span>
        <span style={{ flex:1 }}/>
        {/* MCU temperature — shown faint when stale */}
        <span style={{ fontFamily:_MONO, fontSize:10, fontWeight:600,
          color:sc(stale.telem, T.dim, T) }}>
          {sv(stale.telem && d.temp===0, `${d.temp}°C`)}
        </span>
      </Inset>

      {/* ── Sail trim card ─────────────────────────────────────────────────── */}
      <Card T={T} style={{ display:'flex' }}>
        <div style={{ padding:'13px 14px', borderRight:`1px solid ${T.border}`, flexShrink:0 }}>
          <SailArc value={sail} size={116} T={T} onChange={setSail}/>
        </div>
        <div style={{ flex:1, padding:'13px 14px', display:'flex',
          flexDirection:'column', gap:10, justifyContent:'center' }}>
          <div style={{ display:'flex', alignItems:'baseline', justifyContent:'space-between' }}>
            <span style={lbl()}>SAIL TRIM</span>
            <span style={val(26,T.accent)}>{d.sail}<span style={{ fontSize:13 }}>%</span></span>
          </div>
          {/* Step buttons — each press trims/eases by 5% */}
          <div style={{ display:'grid', gridTemplateColumns:'1fr 1fr', gap:6 }}>
            <div style={btnStyle(T.inset, T.text, `1px solid ${T.border}`)}
              onClick={() => setSail(s => Math.max(0, s-0.05))}>◀ IN</div>
            <div style={btnStyle(T.inset, T.text, `1px solid ${T.border}`)}
              onClick={() => setSail(s => Math.min(1, s+0.05))}>OUT ▶</div>
          </div>
        </div>
      </Card>

      {/* ── Rudder card ────────────────────────────────────────────────────── */}
      <Card T={T} style={{ padding:'13px 16px' }}>
        <div style={{ display:'flex', justifyContent:'space-between',
          alignItems:'baseline', marginBottom:9 }}>
          <span style={lbl()}>RUDDER</span>
          <div style={{ display:'flex', gap:12, alignItems:'baseline' }}>
            <span style={{ fontFamily:_MONO, fontSize:10, fontWeight:700,
              color:T.warn }}>TRIM {rudderTrim>=0?'+':''}{Math.round(rudderTrim*100)}%</span>
            <span style={val(18,T.accent)}>
              {d.rud>=0?'+':''}{d.rud}%
            </span>
          </div>
        </div>

        {/* Drag track — pointer events let us track the finger anywhere */}
        <div ref={rudRef} style={{ position:'relative', height:52, background:T.inset,
          borderRadius:8, border:`1px solid ${T.border}`, overflow:'hidden',
          touchAction:'none', cursor:'ew-resize' }}
          onPointerDown={e => {
            setRudDrag(true);
            rudRef.current.setPointerCapture(e.pointerId);
            setRudder(rudFromPtr(e));
          }}
          onPointerMove={e => rudDrag && setRudder(rudFromPtr(e))}
          onPointerUp={() => {
            setRudDrag(false);
            // Self-centre to trim offset on release
            setRudder(rudderTrim);
          }}
          onPointerCancel={() => { setRudDrag(false); setRudder(rudderTrim); }}>
          {/* Nine tick marks */}
          {[...Array(9)].map((_,i) => (
            <div key={i} style={{ position:'absolute', top:0, bottom:0,
              left:`${i/8*100}%`, width:1,
              background:i===4 ? T.borderStrong : T.border }}/>
          ))}
          {/* Amber trim marker line */}
          <div style={{ position:'absolute', top:0, bottom:0,
            left:`${(rudderTrim*0.42+0.5)*100}%`, width:2.5,
            transform:'translateX(-50%)', background:T.warn, opacity:0.85 }}/>
          {/* Thumb — positioned by normalised rudder value */}
          <div style={{ position:'absolute', top:'50%',
            left:`${(rudder*0.42+0.5)*100}%`,
            width:40, height:40, transform:'translate(-50%,-50%)',
            borderRadius:'50%', background:T.surface,
            border:`2.5px solid ${T.accent}`,
            boxShadow: rudDrag ? `0 0 0 4px ${T.accentSoft}` : '0 2px 6px rgba(28,52,86,0.16)',
            display:'flex', alignItems:'center', justifyContent:'center',
            transition: rudDrag ? 'none' : 'left 0.06s' }}>
            <span style={val(10,T.accent)}>{d.rud>=0?'+':''}{d.rud}%</span>
          </div>
          <span style={{ position:'absolute', left:9, bottom:5,
            fontFamily:_MONO, fontSize:8.5, fontWeight:700, color:T.faint,
            letterSpacing:'0.06em' }}>PORT</span>
          <span style={{ position:'absolute', right:9, bottom:5,
            fontFamily:_MONO, fontSize:8.5, fontWeight:700, color:T.faint,
            letterSpacing:'0.06em' }}>STBD</span>
        </div>

        {/* Trim stepper — each press also moves the rudder so position updates immediately */}
        <div style={{ display:'grid', gridTemplateColumns:'1fr 1.4fr 1fr', gap:5, marginTop:8 }}>
          {[
            ['− TRIM', () => { const t = Math.round(Math.max(-100, rudderTrim*100-1))/100; setRudderTrim(t); setRudder(t); }],
            ['CENTRE', () => { setRudderTrim(0); setRudder(0); }],
            ['TRIM +', () => { const t = Math.round(Math.min(100, rudderTrim*100+1))/100; setRudderTrim(t); setRudder(t); }],
          ].map(([label, fn]) => (
            <div key={label} style={btnStyle(T.inset, T.dim, `1px solid ${T.border}`)}
              onClick={fn}>{label}</div>
          ))}
        </div>
      </Card>

      {/* ── Motor card (auxiliary) ─────────────────────────────────────────── */}
      <Card T={T} style={{ padding:'11px 16px' }}>
        {/* Header: icon + label + Off/Hold/Set selector */}
        <div style={{ display:'flex', alignItems:'center', gap:10,
          marginBottom:motorOn ? 10 : 0 }}>
          <div style={{ width:30, height:30, borderRadius:7, background:T.inset,
            display:'flex', alignItems:'center', justifyContent:'center', flexShrink:0 }}>
            {/* Sunburst icon — filled amber when motor is engaged */}
            <svg width="15" height="15" viewBox="0 0 24 24" fill="none"
              stroke={motorOn?T.warn:T.dim} strokeWidth="2"
              strokeLinecap="round" strokeLinejoin="round">
              <circle cx="12" cy="12" r="3.4"/>
              <path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1"/>
            </svg>
          </div>
          <div style={{ flex:1 }}>
            <span style={{ fontSize:12.5, fontWeight:700, color:motorOn?T.warn:T.text }}>Motor</span>
            <span style={{ fontFamily:_MONO, fontSize:8, fontWeight:600,
              letterSpacing:'0.10em', color:T.faint, marginLeft:7 }}>AUX</span>
          </div>
          {/* Mode selector: Off / Hold / Set */}
          <div style={{ display:'flex', gap:3, padding:3, borderRadius:9,
            background:T.inset, border:`1px solid ${T.border}` }}>
            {[['off','Off'],['hold','Hold'],['set','Set']].map(([id,label]) => {
              const on = id === motorMode;
              return (
                <div key={id} onClick={() => {
                  setMotorMode(id);
                  // Switching to Off resets throttle to neutral
                  if (id === 'off' || id === 'hold') setThrottle(0);
                }} style={{ padding:'5px 11px', borderRadius:6,
                  background:on?T.surface:'transparent',
                  boxShadow:on?'0 1px 2px rgba(28,52,86,0.12)':'none',
                  fontSize:11, fontWeight:700, cursor:'pointer',
                  color:on?(id==='off'?T.dim:T.warn):T.dim }}>
                  {label}
                </div>
              );
            })}
          </div>
        </div>

        {/* Throttle track — only shown when Hold or Set */}
        {motorOn && (
          <div style={{ display:'flex', alignItems:'center', gap:10 }}>
            <div ref={thrRef} style={{ position:'relative', flex:1, height:30,
              background:T.inset, borderRadius:15, border:`1px solid ${T.border}`,
              touchAction:'none', cursor:'ew-resize' }}
              onPointerDown={e => {
                setThrDrag(true);
                thrRef.current.setPointerCapture(e.pointerId);
                setThrottle(thrFromPtr(e));
              }}
              onPointerMove={e => thrDrag && setThrottle(thrFromPtr(e))}
              onPointerUp={() => {
                setThrDrag(false);
                // Hold mode: release returns to neutral (like a spring)
                if (motorMode === 'hold') setThrottle(0);
              }}
              onPointerCancel={() => {
                setThrDrag(false);
                if (motorMode === 'hold') setThrottle(0);
              }}>
              {/* Centre tick */}
              <div style={{ position:'absolute', left:'50%', top:'18%', bottom:'18%',
                width:1.5, background:T.borderStrong, transform:'translateX(-50%)' }}/>
              {/* Coloured fill from centre toward thumb */}
              {Math.abs(thrVal)>2 && (
                <div style={{ position:'absolute', top:'22%', bottom:'22%',
                  left:thrFwd?'50%':`${thrPct}%`,
                  width:`${Math.abs(thrVal)/2}%`,
                  background:thrFill, opacity:0.20, borderRadius:6 }}/>
              )}
              {/* Thumb */}
              <div style={{ position:'absolute', top:'50%', left:`${thrPct}%`,
                width:28, height:28, transform:'translate(-50%,-50%)',
                borderRadius:'50%', background:T.surface,
                border:`2.5px solid ${thrFill}`,
                boxShadow:'0 2px 5px rgba(28,52,86,0.16)',
                transition: thrDrag ? 'none' : 'left 0.05s' }}/>
              <span style={{ position:'absolute', left:7, bottom:3,
                fontFamily:_MONO, fontSize:7.5, fontWeight:700, color:T.faint }}>REV</span>
              <span style={{ position:'absolute', right:7, bottom:3,
                fontFamily:_MONO, fontSize:7.5, fontWeight:700, color:T.faint }}>FWD</span>
            </div>
            {/* Percentage readout */}
            <div style={{ minWidth:42, textAlign:'right' }}>
              <div style={val(13, thrFwd?T.warn:T.danger)}>
                {thrVal>0?'+':''}{thrVal}%
              </div>
              <div style={{ fontFamily:_MONO, fontSize:7.5, fontWeight:700,
                color:thrFwd?T.warn:T.danger }}>
                {thrVal===0?'NEUT':thrFwd?'FWD':'REV'}
              </div>
            </div>
            {/* Mode hint chip */}
            {motorMode==='hold' && (
              <span style={{ fontFamily:_MONO, fontSize:9, fontWeight:700, color:T.warn,
                padding:'5px 8px', borderRadius:7, background:T.warnSoft,
                whiteSpace:'nowrap' }}>Hold to drive</span>
            )}
            {motorMode==='set' && (
              <span onClick={() => setThrottle(0)} style={{ fontFamily:_MONO, fontSize:10,
                fontWeight:700, color:T.danger, padding:'6px 10px', borderRadius:7,
                background:T.dangerSoft, border:`1px solid ${T.danger}`,
                whiteSpace:'nowrap', cursor:'pointer' }}>⊘ Neutral</span>
            )}
          </div>
        )}
      </Card>

      {/* ── Stop row ───────────────────────────────────────────────────────── */}
      <div style={{ padding:'14px 16px 20px', background:T.bg }}>
        <div onClick={onStop}
          style={{ ...btnStyle(T.danger, '#fff'),
            borderRadius:10,
            boxShadow:'0 3px 14px rgba(192,57,43,0.28)', gap:8 }}>
          <span style={{ width:11, height:11, background:'#fff',
            borderRadius:2, display:'inline-block' }}/>
          STOP
        </div>
      </div>
    </div>
  );
};
