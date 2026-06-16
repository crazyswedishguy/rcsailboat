// ── Console / Log screen ──
function Con2({ T }) {
  const val = (s,c) => ({ fontFamily:_MONO, fontWeight:600, fontSize:s||11, color:c||T.text, lineHeight:1.5 });

  const LEVELS = { INFO: T.dim, WARN: T.warn, ERR: T.danger, OK: T.safe, DATA: T.accent };

  const logs = [
    { t:'0:47:12', lvl:'OK',   msg:'System boot complete. All modules initialised.' },
    { t:'0:47:12', lvl:'OK',   msg:'GPS fix acquired: 9 sats, HDOP 0.9, 3D fix.' },
    { t:'0:47:13', lvl:'OK',   msg:'ELRS link established. LQ 94%, RSSI −72 dBm.' },
    { t:'0:47:13', lvl:'DATA', msg:'Home set: 50.82340°N  −1.41230°E' },
    { t:'0:47:14', lvl:'OK',   msg:'IMU calibrated. Heel −4.1° Pitch +1.8° Hdg 247°' },
    { t:'0:47:15', lvl:'INFO', msg:'Bilge sensor OK — dry.' },
    { t:'0:47:15', lvl:'INFO', msg:'Battery: 11.80 V  68%  2.40 A  340 mAh consumed.' },
    { t:'0:47:30', lvl:'INFO', msg:'Helm taken by operator.' },
    { t:'0:47:31', lvl:'INFO', msg:'Sail trim: 59%. Rudder: +6°.' },
    { t:'0:47:42', lvl:'WARN', msg:'LQ dropped to 71% — check transmitter orientation.' },
    { t:'0:47:44', lvl:'INFO', msg:'LQ recovered: 94%.' },
    { t:'0:47:55', lvl:'DATA', msg:'Position: 50.82301°N  −1.41089°E  Hdg 247°  3.2 kn' },
    { t:'0:48:00', lvl:'INFO', msg:'Motor mode: OFF (aux).' },
    { t:'0:48:01', lvl:'INFO', msg:'Rudder trim: +6%.' },
    { t:'0:48:10', lvl:'DATA', msg:'Home distance: 128 m  Bearing 064°' },
    { t:'0:48:12', lvl:'INFO', msg:'Sail winch load: 0.8 A.' },
    { t:'0:48:20', lvl:'INFO', msg:'MCU temp: 42°C — nominal.' },
    { t:'0:48:30', lvl:'DATA', msg:'Position: 50.82296°N  −1.41062°E  Hdg 249°  3.1 kn' },
  ];

  const [filter, setFilter] = React.useState('ALL');
  const filters = ['ALL','OK','INFO','DATA','WARN','ERR'];
  const shown = filter === 'ALL' ? logs : logs.filter(l => l.lvl === filter);
  const endRef = React.useRef(null);
  React.useEffect(() => { if (endRef.current) endRef.current.scrollIntoView({ block:'end' }); }, [filter]);

  return (
    <div style={{ display:'flex', flexDirection:'column', height:'100%' }}>
      {/* filter bar */}
      <HInset T={T} style={{ display:'flex', gap:5, padding:'9px 16px', borderBottom:`1px solid ${T.border}`, flexShrink:0, overflowX:'auto' }}>
        {filters.map(f => {
          const on = f === filter;
          const c = f === 'ALL' ? T.dim : LEVELS[f] || T.dim;
          return (
            <div key={f} onClick={() => setFilter(f)}
              style={{ padding:'5px 12px', borderRadius:7, background: on ? T.surface : 'transparent', border: on ? `1px solid ${T.border}` : '1px solid transparent', fontFamily:_MONO, fontSize:10, fontWeight:700, color: on ? c : T.faint, cursor:'pointer', whiteSpace:'nowrap', boxShadow: on ? '0 1px 2px rgba(28,52,86,0.10)' : 'none' }}>
              {f}
            </div>
          );
        })}
        <span style={{ flex:1 }}/>
        <div style={{ padding:'5px 10px', borderRadius:7, background:T.dangerSoft, fontFamily:_MONO, fontSize:10, fontWeight:700, color:T.danger, cursor:'pointer', whiteSpace:'nowrap' }}>CLEAR</div>
      </HInset>

      {/* log entries */}
      <div style={{ flex:1, overflowY:'auto', padding:'6px 0' }}>
        {shown.map((l, i) => (
          <div key={i} style={{ display:'flex', gap:10, padding:'5px 16px', borderBottom:`1px solid ${T.border}`, alignItems:'baseline' }}>
            <span style={{ fontFamily:_MONO, fontSize:9.5, color:T.faint, flexShrink:0, letterSpacing:'0.04em' }}>{l.t}</span>
            <span style={{ fontFamily:_MONO, fontSize:9, fontWeight:800, letterSpacing:'0.10em', color: LEVELS[l.lvl] || T.dim, flexShrink:0, minWidth:32 }}>{l.lvl}</span>
            <span style={{ fontFamily:_MONO, fontSize:11, color:T.text, lineHeight:1.5 }}>{l.msg}</span>
          </div>
        ))}
        <div ref={endRef}/>
      </div>

      {/* bottom: live indicator */}
      <HInset T={T} style={{ display:'flex', alignItems:'center', gap:8, padding:'8px 16px', borderTop:`1px solid ${T.border}`, flexShrink:0 }}>
        <span style={{ width:7, height:7, borderRadius:99, background:T.safe }}/>
        <span style={{ fontFamily:_MONO, fontSize:10, fontWeight:600, color:T.dim, letterSpacing:'0.06em' }}>LIVE · {shown.length} entries</span>
        <span style={{ flex:1 }}/>
        <span style={{ fontFamily:_MONO, fontSize:10, color:T.faint }}>tap entry to copy</span>
      </HInset>
    </div>
  );
}
Object.assign(window, { Con2 });
