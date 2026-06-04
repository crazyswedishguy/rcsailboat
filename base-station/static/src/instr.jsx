// instr.jsx — Instruments tab (portrait).
// Full telemetry breakdown in section cards.  Stale data is rendered in T.faint
// and values that have never been received show "—" rather than a misleading 0.

// Props: { T, d, stale }

const Instr = ({ T, d, stale }) => {
  const lbl = (x) => ({ fontFamily:_MONO, fontSize:8.5, fontWeight:600,
    letterSpacing:'0.13em', textTransform:'uppercase', color:T.dim, ...(x||{}) });
  const val = (s,c) => ({ fontFamily:_MONO, fontWeight:700, fontSize:s||20,
    color:c||T.text, fontFeatureSettings:'"tnum","zero"', lineHeight:1 });

  // Section header — accent-coloured label on an inset row
  const SectionHead = ({ title }) => (
    <Inset T={T} style={{ padding:'7px 16px', borderBottom:`1px solid ${T.border}` }}>
      <span style={lbl({ color:T.accent })}>{title}</span>
    </Inset>
  );

  // 2-column grid of labelled values.  alert overrides the value colour when set.
  const DataGrid = ({ rows }) => (
    <div style={{ display:'grid', gridTemplateColumns:'1fr 1fr' }}>
      {rows.map(([key, value, unit, alert], i) => (
        <div key={key} style={{ padding:'11px 14px', display:'flex',
          flexDirection:'column', gap:5,
          borderRight:i%2===0?`1px solid ${T.border}`:'none',
          borderTop:i>=2?`1px solid ${T.border}`:'none' }}>
          <span style={lbl()}>{key}</span>
          <div style={{ display:'flex', alignItems:'baseline', gap:4 }}>
            <span style={val(20, alert||T.text)}>{value}</span>
            {unit && <span style={{ fontFamily:_MONO, fontSize:10,
              fontWeight:600, color:T.dim }}>{unit}</span>}
          </div>
        </div>
      ))}
    </div>
  );

  // ── Data rows for each section ─────────────────────────────────────────────
  // alert values drive colour: null = normal, T.warn = caution, T.danger = critical.
  // sv() and sc() (from ds.jsx) handle never-received and stale cases.

  const st = stale.telem; // shorthand
  const sg = stale.gps;

  const powerRows = [
    ['VOLTAGE',  sv(st, d.volts.toFixed(2)), 'V',
      d.volts<10.5?T.danger : d.volts<11.0?T.warn : sc(st,null,T)],
    ['CURRENT',  sv(st, d.amps.toFixed(2)),  'A',  sc(st,null,T)],
    ['CONSUMED', sv(st, d.mah),              'mAh', sc(st,null,T)],
    ['CHARGE',   sv(st, d.chg),              '%',
      d.chg<20?T.danger : d.chg<30?T.warn : sc(st,null,T)],
  ];

  const linkRows = [
    ['LINK QUAL', sv(st, d.lq),   '%',   d.lq<70?T.warn : sc(st,null,T)],
    ['RSSI',      sv(st, d.rssi), 'dBm', d.rssi<-90?T.danger : d.rssi<-80?T.warn : sc(st,null,T)],
    ['PROTOCOL',  'ELRS',         '',    null],
    ['RATE',      '50',           'Hz',  null],
  ];

  const navRows = [
    ['LAT',     sv(sg, d.lat.toFixed(5)), '°', sc(sg,null,T)],
    ['LON',     sv(sg, d.lon.toFixed(5)), '°', sc(sg,null,T)],
    ['SPEED',   sv(sg, d.spd.toFixed(2)), 'kn', sc(sg,null,T)],
    ['HEADING', sv(sg, d.hdg),            '°', sc(sg,null,T)],
    ['SATS',    sv(sg, d.sats),           '',   d.sats<6?T.warn : sc(sg,null,T)],
    ['HOME',    d.homeDist ? `${d.homeDist}m` : '—', '', null],
  ];

  const ctrlRows = [
    ['HEEL',   `${d.roll>=0?'+':''}${d.roll.toFixed(2)}`, '°', Math.abs(d.roll)>30?T.warn:sc(st,null,T)],
    ['PITCH',  `${d.pitch>=0?'+':''}${d.pitch.toFixed(2)}`, '°', sc(st,null,T)],
    ['SAIL',   `${d.sail}`,                                '%', null],
    ['RUDDER', `${d.rud>=0?'+':''}${d.rud}`,               '°', null],
  ];

  return (
    <div style={{ display:'flex', flexDirection:'column' }}>

      {/* ── Attitude hero — horizon + 2×2 readouts ──────────────────────── */}
      <Card T={T} style={{ display:'flex', alignItems:'center' }}>
        <div style={{ padding:'14px 16px', borderRight:`1px solid ${T.border}`, flexShrink:0 }}>
          <AttHorizon roll={d.roll} pitch={d.pitch} size={112} T={T}/>
        </div>
        <div style={{ flex:1, padding:'14px 16px', display:'flex',
          flexDirection:'column', gap:10 }}>
          <div style={lbl({ color:T.accent })}>ATTITUDE</div>
          <div style={{ display:'grid', gridTemplateColumns:'1fr 1fr', gap:'10px 16px' }}>
            {[
              ['HEEL',  `${d.roll>=0?'+':''}${d.roll.toFixed(1)}°`],
              ['PITCH', `${d.pitch>=0?'+':''}${d.pitch.toFixed(1)}°`],
              ['HDG',   `${d.hdg}°`],
              ['SPD',   `${d.spd.toFixed(1)} kn`],
            ].map(([k,v]) => (
              <div key={k} style={{ display:'flex', flexDirection:'column', gap:3 }}>
                <span style={lbl()}>{k}</span>
                <span style={val(18, sc(st,T.text,T))}>{v}</span>
              </div>
            ))}
          </div>
        </div>
      </Card>

      {/* ── Sectioned data grids ─────────────────────────────────────────── */}
      {[
        { title:'POWER',      rows:powerRows },
        { title:'LINK',       rows:linkRows  },
        { title:'NAVIGATION', rows:navRows   },
        { title:'CONTROLS',   rows:ctrlRows  },
      ].map(({ title, rows }) => (
        <Card key={title} T={T}>
          <SectionHead title={title}/>
          <DataGrid rows={rows}/>
        </Card>
      ))}

      {/* ── Environment section ──────────────────────────────────────────── */}
      <Card T={T}>
        <SectionHead title="ENVIRONMENT"/>
        <div style={{ display:'flex', alignItems:'center', gap:14, padding:'12px 16px' }}>
          <span style={{ width:8, height:8, borderRadius:99, flexShrink:0,
            background:d.bilgeWet ? T.danger : T.safe }}/>
          <div style={{ flex:1 }}>
            <div style={lbl({ color:T.text })}>
              {d.bilgeWet ? 'WATER DETECTED IN BILGE' : 'BILGE DRY'}
            </div>
            <div style={{ fontFamily:_MONO, fontSize:10, color:sc(st,T.dim,T), marginTop:3 }}>
              MCU temp {sv(st, `${d.temp}°C`)}
            </div>
          </div>
        </div>
      </Card>

    </div>
  );
};
