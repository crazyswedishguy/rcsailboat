// tweaks-panel.jsx — persistent settings panel

function useTweaks(defaults) {
  const KEY = 'rcsb-tweaks';
  const stored = (() => {
    try { return JSON.parse(localStorage.getItem(KEY) || '{}'); }
    catch { return {}; }
  })();
  const [tweaks, setTweaks] = React.useState({ ...defaults, ...stored });
  function setTweak(id, value) {
    setTweaks(prev => {
      const next = { ...prev, [id]: value };
      localStorage.setItem(KEY, JSON.stringify(next));
      return next;
    });
  }
  return [tweaks, setTweak];
}

function TweaksPanel({ children }) {
  const [open, setOpen] = React.useState(false);
  return (
    <>
      <button
        onClick={() => setOpen(o => !o)}
        title="Settings"
        style={{
          position: 'fixed', bottom: 16, right: 16, zIndex: 1000,
          width: 36, height: 36, borderRadius: '50%',
          background: '#1a2a40', border: '1px solid #253650',
          color: '#5a7a96', fontSize: 16, cursor: 'pointer',
          display: 'flex', alignItems: 'center', justifyContent: 'center',
        }}
      >⚙</button>
      {open && (
        <div style={{
          position: 'fixed', bottom: 60, right: 16, zIndex: 1000,
          background: '#121e30', border: '1px solid #253650',
          borderRadius: 12, padding: '12px 16px', minWidth: 200,
          boxShadow: '0 4px 24px rgba(0,0,0,0.5)',
        }}>
          {children}
        </div>
      )}
    </>
  );
}

function TweakSection({ title, children }) {
  return (
    <div style={{ marginBottom: 10 }}>
      <div style={{
        fontSize: 9, fontWeight: 700, letterSpacing: '0.09em',
        textTransform: 'uppercase', color: '#5a7a96', marginBottom: 6,
      }}>{title}</div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>{children}</div>
    </div>
  );
}

function TweakRadio({ label, options, value, onChange }) {
  return (
    <div>
      <div style={{ fontSize: 10, color: '#5a7a96', marginBottom: 3 }}>{label}</div>
      <div style={{ display: 'flex', gap: 4 }}>
        {options.map(opt => (
          <button
            key={opt}
            onClick={() => onChange(opt)}
            style={{
              flex: 1, padding: '4px 6px', borderRadius: 6, cursor: 'pointer',
              fontSize: 10, fontWeight: 700, fontFamily: 'inherit',
              border: `1px solid ${value === opt ? '#3ecfcf' : '#253650'}`,
              background: value === opt ? 'rgba(62,207,207,0.15)' : '#1a2a40',
              color: value === opt ? '#3ecfcf' : '#5a7a96',
            }}
          >{opt}</button>
        ))}
      </div>
    </div>
  );
}
