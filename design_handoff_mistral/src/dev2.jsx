// ── Devices screen ──
function Dev2({ T }) {
  const d = TELE;
  const lbl = (x) => ({ fontFamily:_MONO, fontSize:8.5, fontWeight:600, letterSpacing:'0.13em', textTransform:'uppercase', color:T.dim, ...(x||{}) });
  const val = (s,c) => ({ fontFamily:_MONO, fontWeight:700, fontSize:s||13, color:c||T.text, fontFeatureSettings:'"tnum","zero"', lineHeight:1 });

  const Dot = ({ ok, warn }) => (
    <span style={{ width:8, height:8, borderRadius:99, flexShrink:0, background: ok ? T.safe : warn ? T.warn : T.danger }}/>
  );

  const devices = [
    {
      id:'rpi', name:'Raspberry Pi', sub:'Main controller · Python 3.11',
      ok:true, stats:[['CPU','18%'],['RAM','312 MB'],['TEMP',d.temp+'°C'],['UPTIME','0:47:12']],
    },
    {
      id:'elrs', name:'ELRS Receiver', sub:'ExpressLRS 3.3.1 · UART2',
      ok:true, stats:[['LQ',d.lq+'%'],['RSSI',d.rssi+' dBm'],['SNR','+4 dB'],['RATE','50 Hz']],
    },
    {
      id:'gps', name:'GPS Module', sub:'u-blox M9N · I²C',
      ok:true, stats:[['SATS',d.sats+''],['HDOP','0.9'],['FIX','3D'],['ALT',d.alt+'m']],
    },
    {
      id:'imu', name:'IMU / Compass', sub:'BNO055 · I²C',
      ok:true, stats:[['HEEL',d.roll.toFixed(1)+'°'],['PITCH',d.pitch.toFixed(1)+'°'],['HDG',d.hdg+'°'],['CAL','3/3']],
    },
    {
      id:'servo_rud', name:'Rudder Servo', sub:'PWM · Channel 1',
      ok:true, stats:[['POS','+'+d.rud+'°'],['RANGE','±35°'],['FREQ','50 Hz'],['TRIM','+'+Math.round(d.trim*100)+'%']],
    },
    {
      id:'servo_sail', name:'Sail Winch', sub:'PWM · Channel 2',
      ok:true, stats:[['POS',d.sail+'%'],['RANGE','0–100%'],['FREQ','50 Hz'],['LOAD','0.8 A']],
    },
    {
      id:'motor', name:'Motor ESC', sub:'PWM · Channel 3 · Aux',
      ok:true, stats:[['STATE','OFF'],['V', d.volts.toFixed(1)+' V'],['A','0.0 A'],['TEMP','38°C']],
    },
    {
      id:'bilge', name:'Bilge Sensor', sub:'Digital · GPIO 17',
      ok:true, stats:[['STATE','DRY'],['TRIPS','0'],['SINCE','47 min'],['THRESH','3 mm']],
    },
    {
      id:'batt', name:'Battery Monitor', sub:'INA219 · I²C',
      ok:true, stats:[['V',d.volts.toFixed(2)+' V'],['A',d.amps.toFixed(2)+' A'],['mAh',d.mah+''],['CHG',d.chg+'%']],
    },
  ];

  return (
    <div style={{ display:'flex', flexDirection:'column' }}>
      {/* summary strip */}
      <HInset T={T} style={{ display:'flex', alignItems:'center', gap:10, padding:'9px 16px', borderBottom:`1px solid ${T.border}` }}>
        <Dot ok/>
        <span style={lbl({ color:T.safe })}>ALL SYSTEMS NOMINAL</span>
        <span style={{ flex:1 }}/>
        <span style={{ fontFamily:_MONO, fontSize:10, color:T.dim }}>9/9 online</span>
      </HInset>

      {/* device cards */}
      {devices.map(dev => (
        <HCell key={dev.id} T={T} style={{ padding:'12px 16px' }}>
          <div style={{ display:'flex', alignItems:'flex-start', gap:10, marginBottom:9 }}>
            <Dot ok={dev.ok}/>
            <div style={{ flex:1 }}>
              <div style={{ fontSize:13, fontWeight:700, color:T.text, lineHeight:1.2 }}>{dev.name}</div>
              <div style={{ fontFamily:_MONO, fontSize:9, color:T.faint, marginTop:2, letterSpacing:'0.04em' }}>{dev.sub}</div>
            </div>
          </div>
          <div style={{ display:'grid', gridTemplateColumns:'repeat(4,1fr)', gap:0, background:T.inset, borderRadius:7, border:`1px solid ${T.border}`, overflow:'hidden' }}>
            {dev.stats.map(([k,v], i) => (
              <div key={k} style={{ padding:'8px 10px', borderRight: i<3 ? `1px solid ${T.border}` : 'none', display:'flex', flexDirection:'column', gap:3 }}>
                <span style={lbl({ fontSize:7.5 })}>{k}</span>
                <span style={val(12)}>{v}</span>
              </div>
            ))}
          </div>
        </HCell>
      ))}

      {/* firmware row */}
      <HCell T={T} style={{ padding:'12px 16px' }}>
        <div style={lbl({ color:T.accent, marginBottom:9 })}>FIRMWARE</div>
        <div style={{ display:'flex', flexDirection:'column', gap:6 }}>
          {[['Mistral Pi App','v0.8.3 (dev)'],['ELRS','3.3.1'],['Bootloader','v1.2'],['OS','Raspberry Pi OS 12']].map(([k,v]) => (
            <div key={k} style={{ display:'flex', justifyContent:'space-between', alignItems:'baseline' }}>
              <span style={{ fontSize:12, color:T.text }}>{k}</span>
              <span style={val(11, T.dim)}>{v}</span>
            </div>
          ))}
        </div>
      </HCell>
    </div>
  );
}
Object.assign(window, { Dev2 });
