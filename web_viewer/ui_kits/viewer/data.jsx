// Synthetic sample data so the viewer looks alive in the kit
function makeSyntheticData() {
  const genSeries = (id, label, n, phase, freqHz, noise, baseV, baseA) => {
    const pts = [];
    const periodUs = 1000; // 1 kHz
    for (let i = 0; i < n; i++) {
      const tUs = i * periodUs + phase;
      const t = tUs / 1e6;
      const v = baseV + 0.08 * Math.sin(2 * Math.PI * freqHz * t + phase * 1e-6) + (Math.random() - 0.5) * noise;
      const a = baseA + 0.18 * Math.sin(2 * Math.PI * freqHz * t * 1.3 + 0.6) + (Math.random() - 0.5) * noise * 0.2;
      const p = v * a;
      const temp = 34.5 + 0.6 * Math.sin(t * 0.4) + (Math.random() - 0.5) * 0.1;
      const vshunt = a * 0.01;
      const charge = a * t;
      const energy = p * t;
      pts.push({ sourceId: id, sourceLabel: label, seq: i, timeUs: tUs, voltage: v, current: a, power: p, temp, vshunt, charge, energy });
    }
    return { id, label, points: pts };
  };
  return {
    meta: { schema_version: "1.0", protocol_version: "1", config: { stream_period_us: 1000 } },
    series: [
      genSeries("pico",        "pico · powermonitor_20260218_005711.json", 800, 0,     6,  0.04, 12.04, 0.52),
      genSeries("onboard_cpp", "onboard_cpp · ina228_demo.json",           800, 1500,  4,  0.03, 11.98, 0.48),
    ],
  };
}

Object.assign(window, { makeSyntheticData });
