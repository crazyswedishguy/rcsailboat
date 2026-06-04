// con.jsx — Console tab (portrait).
// Streams raw Serial.print() output from the ESP32 over WebSocket.
// Lines are parsed for keywords to assign a log level, then rendered with
// colour coding and a filter bar.

// Props: { T, lines, enabled }
//   lines   — array of raw string lines received from the ESP32 USB serial
//   enabled — false when ESP32_DBG_PORT is not configured on the Pi

const Con = ({ T, lines, enabled }) => {
  const { useState, useRef, useEffect } = React;

  // Levels in filter order.  'ALL' is a pseudo-level meaning no filter.
  const FILTERS = ['ALL','OK','INFO','DATA','WARN','ERR'];

  // Colour for each log level
  const LEVEL_COLOR = {
    INFO: T.dim,
    WARN: T.warn,
    ERR:  T.danger,
    OK:   T.safe,
    DATA: T.accent,
  };

  const [filter, setFilter] = useState('ALL');
  const endRef = useRef(null);
  const scrollRef = useRef(null);

  // Parse a raw line into { t, lvl, msg }.
  // Timestamp is the browser wall-clock time when the line was shown (we don't
  // get hardware timestamps over raw serial), presented as HH:MM:SS.
  function parseLine(raw, idx) {
    const lo = raw.toLowerCase();
    // Match on common keywords — order matters (ERR checked before WARN)
    const lvl = lo.includes('error')  || lo.includes('fault') ? 'ERR'
              : lo.includes('warn')                            ? 'WARN'
              : lo.includes('ok')  || lo.includes('ready')
                                    || lo.includes('done')     ? 'OK'
              : lo.includes('data') || lo.match(/\d+\.\d+°/)   ? 'DATA'
              : 'INFO';
    return { id:idx, lvl, msg:raw };
  }

  const parsed = lines.map(parseLine);
  const shown  = filter==='ALL' ? parsed : parsed.filter(l => l.lvl===filter);

  // Auto-scroll to bottom when new lines arrive, but only if we're already
  // near the bottom (so the user can scroll up to read without being yanked).
  useEffect(() => {
    const el = scrollRef.current;
    if (!el) return;
    const atBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 80;
    if (atBottom) endRef.current?.scrollIntoView({ behavior:'smooth' });
  }, [lines]);

  // Reset scroll when filter changes
  useEffect(() => {
    endRef.current?.scrollIntoView({ behavior:'instant' });
  }, [filter]);

  return (
    <div style={{ display:'flex', flexDirection:'column', height:'100%', minHeight:400 }}>

      {/* ── Filter bar ───────────────────────────────────────────────────── */}
      <Inset T={T} style={{ display:'flex', gap:5, padding:'9px 16px',
        borderBottom:`1px solid ${T.border}`, flexShrink:0, overflowX:'auto' }}>
        {FILTERS.map(f => {
          const on = f===filter;
          const col = f==='ALL' ? T.dim : LEVEL_COLOR[f]||T.dim;
          return (
            <div key={f} onClick={() => setFilter(f)} style={{
              padding:'5px 12px', borderRadius:7, cursor:'pointer',
              background:on?T.surface:'transparent',
              border:on?`1px solid ${T.border}`:'1px solid transparent',
              fontFamily:_MONO, fontSize:10, fontWeight:700,
              color:on?col:T.faint, whiteSpace:'nowrap',
              boxShadow:on?'0 1px 2px rgba(28,52,86,0.10)':'none' }}>
              {f}
            </div>
          );
        })}
        <span style={{ flex:1 }}/>
        <div onClick={() => {}} style={{ padding:'5px 10px', borderRadius:7,
          background:T.dangerSoft, fontFamily:_MONO, fontSize:10, fontWeight:700,
          color:T.danger, cursor:'pointer', whiteSpace:'nowrap' }}>
          CLEAR
        </div>
      </Inset>

      {/* ── Log entries ──────────────────────────────────────────────────── */}
      <div ref={scrollRef} style={{ flex:1, overflowY:'auto', padding:'4px 0' }}>

        {/* Shown when the port isn't configured on the Pi */}
        {!enabled && (
          <div style={{ padding:'18px 16px', fontFamily:_MONO, fontSize:11, color:T.faint }}>
            USB console not configured — set{' '}
            <span style={{ color:T.accent }}>ESP32_DBG_PORT</span>{' '}
            in /etc/rcsailboat.env on the Pi.
          </div>
        )}

        {/* Shown when configured but no data has arrived yet */}
        {enabled && shown.length===0 && (
          <div style={{ padding:'18px 16px', fontFamily:_MONO, fontSize:11, color:T.faint }}>
            {filter==='ALL' ? 'Waiting for output from ESP32…' : `No ${filter} entries`}
          </div>
        )}

        {shown.map((l) => (
          <div key={l.id} style={{ display:'flex', gap:10, padding:'5px 16px',
            borderBottom:`1px solid ${T.border}`, alignItems:'baseline' }}>
            {/* Level badge */}
            <span style={{ fontFamily:_MONO, fontSize:9, fontWeight:800,
              letterSpacing:'0.10em', color:LEVEL_COLOR[l.lvl]||T.dim,
              flexShrink:0, minWidth:32 }}>
              {l.lvl}
            </span>
            {/* Message */}
            <span style={{ fontFamily:_MONO, fontSize:11, color:T.text,
              lineHeight:1.5, wordBreak:'break-all' }}>
              {l.msg}
            </span>
          </div>
        ))}
        <div ref={endRef}/>
      </div>

      {/* ── Footer: live indicator ───────────────────────────────────────── */}
      <Inset T={T} style={{ display:'flex', alignItems:'center', gap:8,
        padding:'8px 16px', borderTop:`1px solid ${T.border}`, flexShrink:0 }}>
        <span style={{ width:7, height:7, borderRadius:99,
          background:enabled?T.safe:T.faint }}/>
        <span style={{ fontFamily:_MONO, fontSize:10, fontWeight:600,
          color:T.dim, letterSpacing:'0.06em' }}>
          {enabled ? 'LIVE' : 'OFFLINE'} · {shown.length} entries
        </span>
        <span style={{ flex:1 }}/>
        <span style={{ fontFamily:_MONO, fontSize:10, color:T.faint }}>
          {lines.length} total
        </span>
      </Inset>

    </div>
  );
};
