// ── Instruments screen ──
function Instr2({ T }) {
  const d = TELE;
  const lbl = (x) => ({ fontFamily:_MONO, fontSize:8.5, fontWeight:600, letterSpacing:'0.13em', textTransform:'uppercase', color:T.dim, ...(x||{}) });
  const val = (s,c) => ({ fontFamily:_MONO, fontWeight:700, fontSize:s||21, color:c||T.text, fontFeatureSettings:'"tnum","zero"', lineHeight:1 });

  const powerRows = [
    ['VOLTAGE', d.volts.toFixed(2), 'V', d.volts < 10.5 ? T.danger : d.volts < 11.0 ? T.warn : null],
    ['CURRENT', d.amps.toFixed(2), 'A', null],
    ['CONSUMED', d.mah, 'mAh', null],
    ['CHARGE', d.chg, '%', d.chg < 20 ? T.danger : d.chg < 30 ? T.warn : null],
  ];
  const linkRows = [
    ['LINK QUAL', d.lq, '%', d.lq < 70 ? T.warn : null],
    ['RSSI', d.rssi, 'dBm', d.rssi < -90 ? T.danger : d.rssi < -80 ? T.warn : null],
    ['PROTOCOL', 'ELRS', '', null],
    ['RATE', '50', 'Hz', null],
  ];
  const navRows = [
    ['LAT', d.lat.toFixed(5), '°', null],
    ['LON', d.lon.toFixed(5), '°', null],
    ['SPEED', d.spd.toFixed(2), 'kn', null],
    ['HEADING', d.hdg, '°', null],
    ['SATS', d.sats, '', d.sats < 6 ? T.warn : null],
    ['HOME', d.homeDist, 'm', null],
  ];
  const motionRows = [
    ['HEEL', d.roll.toFixed(2), '°', Math.abs(d.roll) > 30 ? T.warn : null],
    ['PITCH', d.pitch.toFixed(2), '°', null],
    ['SAIL', d.sail, '%', null],
    ['RUDDER', '+'+d.rud, '°', null],
  ];

  const Section = ({ title, rows }) => (
    <HCell T={T} style={{ marginBottom:0 }}>
      <HInset T={T} style={{ padding:'7px 16px', borderBottom:`1px solid ${T.border}` }}>
        <span style={lbl({ color:T.accent })}>{title}</span>
      </HInset>
      <div style={{ display:'grid', gridTemplateColumns:'1fr 1fr' }}>
        {rows.map(([k,v,u,alert], i) => (
          <div key={k} style={{ padding:'11px 14px', borderRight:i%2===0?`1px solid ${T.border}`:'none', borderTop:i>=2?`1px solid ${T.border}`:'none', display:'flex', flexDirection:'column', gap:5 }}>
            <span style={lbl()}>{k}</span>
            <div style={{ display:'flex', alignItems:'baseline', gap:4 }}>
              <span style={val(20, alert||T.text)}>{v}</span>
              <span style={{ fontFamily:_MONO, fontSize:10, fontWeight:600, color:T.dim }}>{u}</span>
            </div>
          </div>
        ))}
      </div>
    </HCell>
  );

  return (
    <div style={{ display:'flex', flexDirection:'column', gap:0 }}>
      {/* attitude hero */}
      <HCell T={T} style={{ display:'flex', alignItems:'center', gap:0 }}>
        <div style={{ padding:'14px 16px', borderRight:`1px solid ${T.border}`, flexShrink:0 }}>
          <H2Att roll={d.roll} pitch={d.pitch} size={112} T={T}/>
        </div>
        <div style={{ flex:1, padding:'14px 16px', display:'flex', flexDirection:'column', gap:10 }}>
          <div style={lbl({ color:T.accent })}>ATTITUDE</div>
          <div style={{ display:'grid', gridTemplateColumns:'1fr 1fr', gap:'10px 16px' }}>
            {[['HEEL', d.roll.toFixed(1)+'°'], ['PITCH', d.pitch.toFixed(1)+'°'], ['HDG', d.hdg+'°'], ['SPD', d.spd.toFixed(1)+' kn']].map(([k,v]) => (
              <div key={k} style={{ display:'flex', flexDirection:'column', gap:3 }}>
                <span style={lbl()}>{k}</span>
                <span style={val(18)}>{v}</span>
              </div>
            ))}
          </div>
        </div>
      </HCell>

      <Section title="POWER" rows={powerRows}/>
      <Section title="LINK" rows={linkRows}/>
      <Section title="NAVIGATION" rows={navRows}/>
      <Section title="CONTROLS" rows={motionRows}/>

      {/* environment */}
      <HCell T={T}>
        <HInset T={T} style={{ padding:'7px 16px', borderBottom:`1px solid ${T.border}` }}>
          <span style={lbl({ color:T.accent })}>ENVIRONMENT</span>
        </HInset>
        <div style={{ display:'flex', alignItems:'center', gap:14, padding:'12px 16px' }}>
          <span style={{ width:8, height:8, borderRadius:99, background:d.bilgeWet?T.danger:T.safe, flexShrink:0 }}/>
          <div style={{ flex:1 }}>
            <div style={lbl({ color:T.text })}>{d.bilgeWet?'WATER DETECTED IN BILGE':'BILGE DRY'}</div>
            <div style={{ fontFamily:_MONO, fontSize:10, color:T.dim, marginTop:3 }}>MCU temp {d.temp}°C</div>
          </div>
        </div>
      </HCell>
    </div>
  );
}
Object.assign(window, { Instr2 });
