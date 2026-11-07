(function () {
  var NS = 'http://www.w3.org/2000/svg';
  var root = document.documentElement;
  document.getElementById('themeBtn').addEventListener('click', function () {
    var dark = root.getAttribute('data-theme') === 'dark' ||
      (!root.hasAttribute('data-theme') && window.matchMedia('(prefers-color-scheme: dark)').matches);
    root.setAttribute('data-theme', dark ? 'light' : 'dark');
  });

  function mk(parent, tag, attrs, text) {
    var e = document.createElementNS(NS, tag);
    for (var k in attrs) e.setAttribute(k, attrs[k]);
    if (text !== undefined) e.textContent = text;
    parent.appendChild(e);
    return e;
  }
  function fmt(v, d) { return v.toLocaleString('en-CA', { minimumFractionDigits: d, maximumFractionDigits: d }); }

  // ---------------- Waveforms ----------------
  // Digital trace from a list of [x, level] change points
  function trace(g, y, pts, xEnd, h) {
    var d = '', lv = pts[0][1];
    d += 'M' + pts[0][0] + ',' + (lv ? y : y + h);
    for (var i = 1; i < pts.length; i++) {
      d += ' H' + pts[i][0] + ' V' + (pts[i][1] ? y : y + h);
    }
    d += ' H' + xEnd;
    mk(g, 'path', { d: d, 'class': 'wf' });
  }
  // Bus: segments [x0, x1, label] drawn as hexagons, gaps idle
  function bus(g, y, segs, h, cls) {
    var s = 4;
    segs.forEach(function (sg) {
      var a = sg[0], b = sg[1];
      mk(g, 'path', { 'class': cls, d: 'M' + a + ',' + (y + h / 2) + ' L' + (a + s) + ',' + y + ' H' + (b - s) +
        ' L' + b + ',' + (y + h / 2) + ' L' + (b - s) + ',' + (y + h) + ' H' + (a + s) + ' Z' });
      mk(g, 'text', { x: (a + b) / 2, y: y + h / 2 + 4, 'font-size': 11, 'text-anchor': 'middle' }, sg[2]);
    });
  }
  function label(g, y, h, t) { mk(g, 'text', { x: 10, y: y + h / 2 + 4, 'font-size': 12, 'class': 't2' }, t); }

  (function wfIn() {
    var g = document.getElementById('wfIn'), x0 = 120, p = 60, n = 13, h = 18, end = x0 + n * p;
    var clk = [[x0 - 20, 1]];
    for (var i = 0; i < n; i++) { clk.push([x0 + i * p, 0]); clk.push([x0 + i * p + p / 2, 1]); }
    label(g, 20, h, 'SCLK_IN'); trace(g, 20, clk, end, h);
    label(g, 60, h, 'CS_IN'); trace(g, 60, [[x0 - 20, 1], [x0, 0], [x0 + 8 * p, 1]], end, h);
    var su = [], sr = [];
    for (var b = 0; b < 8; b++) {
      su.push([x0 + b * p, x0 + (b + 1) * p, 'u' + (7 - b)]);
      sr.push([x0 + b * p, x0 + (b + 1) * p, 'r' + (7 - b)]);
    }
    label(g, 100, h, 'MOSI_u_in'); bus(g, 100, su, h, 'wf-bus2');
    label(g, 140, h, 'MOSI_r_in'); bus(g, 140, sr, h, 'wf-bus2');
    for (var j = 0; j < 8; j++) {
      mk(g, 'line', { x1: x0 + j * p + p / 2, x2: x0 + j * p + p / 2, y1: 14, y2: 164, 'class': 'xhair', 'stroke-dasharray': '2 3' });
    }
    mk(g, 'text', { x: x0 + 7.5 * p, y: 180, 'font-size': 11, 'text-anchor': 'start', 'class': 't2' }, '↑ 8th rising edge: words to holding registers, d_tog flips');
    mk(g, 'text', { x: x0 + 10.5 * p, y: 108, 'font-size': 11, 'text-anchor': 'middle', 'class': 'tm' }, '5 periods, CS_IN high');
    mk(g, 'text', { x: x0, y: 196, 'font-size': 11, 'class': 'tm' }, 'dotted: sampling edges · data changes on falling edges');
  })();

  (function wfOut() {
    var g = document.getElementById('wfOut'), x0 = 130, q = 48, h = 18, end = x0 + 17 * q;
    var clk = [[x0 - 30, 0]];
    for (var c = 0; c < 16; c++) clk.push([x0 + c * q, c % 2]);
    clk.push([x0 + 16 * q, 0]);
    label(g, 16, h, 'SCLK_OUT'); trace(g, 16, clk, end, h);
    label(g, 52, h, 'FS_OUT'); trace(g, 52, [[x0 - 30, 0], [x0, 1], [x0 + 2 * q, 0]], end, h);
    var sy = [], se = [];
    for (var b = 0; b < 8; b++) {
      sy.push([x0 + 2 * b * q, x0 + (2 * b + 2) * q, 'y' + (7 - b)]);
      se.push([x0 + 2 * b * q, x0 + (2 * b + 2) * q, 'e' + (7 - b)]);
    }
    label(g, 88, h, 'MOSI_y_out'); bus(g, 88, sy, h, 'wf-bus');
    label(g, 124, h, 'MOSI_e_out'); bus(g, 124, se, h, 'wf-bus');
    for (var k = 0; k < 8; k++) {
      var xs = x0 + (2 * k + 1) * q;
      mk(g, 'line', { x1: xs, x2: xs, y1: 10, y2: 146, 'class': 'xhair', 'stroke-dasharray': '2 3' });
    }
    mk(g, 'text', { x: x0 + 8 * q, y: 164, 'font-size': 11, 'text-anchor': 'middle', 'class': 'tm' },
       'one clk = ' + q + ' px · frame = 16 clk = 400 ns at 40 MHz · dotted: receiver sampling edges');
  })();

  (function stepTl() {
    var g = document.getElementById('stepTl'), x0 = 70, u = 33;
    function X(c) { return x0 + c * u; }
    [0, 5, 10, 15, 20, 25].forEach(function (c) {
      mk(g, 'line', { x1: X(c), x2: X(c), y1: 30, y2: 200, 'class': 'grid' });
      mk(g, 'text', { x: X(c), y: 216, 'font-size': 11, 'text-anchor': 'middle', 'class': 'tm' }, c + (c === 25 ? ' clk' : ''));
    });
    function blk(c0, n, y, fill, txt, txtFill) {
      mk(g, 'rect', { x: X(c0) + 1, y: y, width: n * u - 2, height: 26, rx: 4, fill: fill });
      if (txt) mk(g, 'text', { x: X(c0) + n * u / 2, y: y + 17, 'font-size': 11, 'text-anchor': 'middle', style: 'fill:' + (txtFill || '#fff') }, txt);
    }
    mk(g, 'text', { x: 8, y: 57, 'font-size': 12, 'class': 't2' }, 'core');
    blk(0, 1, 40, 'var(--accent-2)', 'go');
    ['3', '2', '1', '0'].forEach(function (d, i) { blk(1 + 4 * i, 4, 40, 'var(--series-1)', 'digit ' + d + ' · taps 0–3'); });
    blk(17, 1, 40, 'var(--axis)', 'dr', 'var(--text-primary)');
    blk(18, 1, 40, 'var(--text-secondary)', 'FIN');
    blk(19, 4, 40, 'var(--s3)', 'UPD w0..w3');
    mk(g, 'rect', { x: X(23) + 1, y: 40, width: 3 * u - 2, height: 26, rx: 4, fill: 'none', stroke: 'var(--axis)', 'stroke-dasharray': '4 3' });
    mk(g, 'text', { x: X(24.5), y: 57, 'font-size': 11, 'text-anchor': 'middle', 'class': 't2' }, 'spare');
    mk(g, 'text', { x: X(9), y: 84, 'font-size': 11, 'text-anchor': 'middle', 'class': 't2' }, 'MAC: 17 clk (16 issue + drain), Horner order');
    mk(g, 'text', { x: 8, y: 123, 'font-size': 12, 'class': 't2' }, 'output');
    mk(g, 'rect', { x: X(20) + 1, y: 106, width: 6 * u - 2, height: 26, rx: 4, fill: 'var(--wash-1)', stroke: 'var(--series-1)', 'stroke-width': 1.5 });
    mk(g, 'text', { x: X(23), y: 123, 'font-size': 11, 'text-anchor': 'middle' }, 'y/e frame, 16 clk →');
    mk(g, 'text', { x: 8, y: 169, 'font-size': 12, 'class': 't2' }, 'input');
    mk(g, 'rect', { x: X(7.5) + 1, y: 152, width: 16 * u - 2, height: 26, rx: 4, fill: 'var(--wash-2)', stroke: 'var(--accent-2)', 'stroke-width': 1.5 });
    mk(g, 'text', { x: X(15.5), y: 169, 'font-size': 11, 'text-anchor': 'middle' }, 'next u/r frame shifts in: 8 SCLK_IN = 16 clk, then 2–3 clk sync');
    mk(g, 'line', { x1: X(23), x2: X(23), y1: 26, y2: 200, stroke: 'var(--text-primary)', 'stroke-width': 1.5 });
    mk(g, 'text', { x: X(23) - 4, y: 22, 'font-size': 11, 'text-anchor': 'end', 'class': 't2' }, 'step done: earliest next start (23)');
    mk(g, 'line', { x1: X(26), x2: X(26), y1: 26, y2: 200, stroke: 'var(--text-primary)', 'stroke-width': 1.5, 'stroke-dasharray': '5 4' });
    mk(g, 'text', { x: X(26) + 4, y: 22, 'font-size': 11, 'class': 't2' }, 'spec: 26');
  })();

  // ---------------- Line charts ----------------
  function lineChart(o) {
    var svg = document.getElementById(o.svg), card = document.getElementById(o.card), tip = document.getElementById(o.tip);
    var W = 960, H = +svg.getAttribute('viewBox').split(' ')[3];
    var L = 64, R = o.rightPad || 120, T = 14, B = 40;
    var xs = o.x, x0 = o.xMin !== undefined ? o.xMin : xs[0], x1 = o.xMax !== undefined ? o.xMax : xs[xs.length - 1];
    function X(v) { return L + (W - L - R) * (v - x0) / (x1 - x0); }
    function Y(v) { return T + (H - T - B) * (o.yMax - v) / (o.yMax - o.yMin); }
    o.yTicks.forEach(function (t) {
      mk(svg, 'line', { x1: L, x2: W - R, y1: Y(t), y2: Y(t), 'class': 'grid' });
      mk(svg, 'text', { x: L - 8, y: Y(t) + 4, 'font-size': 12, 'text-anchor': 'end', 'class': 'tm' }, o.yFmt(t));
    });
    o.xTicks.forEach(function (t) {
      mk(svg, 'text', { x: X(t), y: H - B + 18, 'font-size': 12, 'text-anchor': 'middle', 'class': 'tm' }, o.xFmt(t));
    });
    mk(svg, 'text', { x: (L + W - R) / 2, y: H - 4, 'font-size': 12, 'text-anchor': 'middle', 'class': 't2' }, o.xLabel);
    mk(svg, 'line', { x1: L, x2: W - R, y1: H - B, y2: H - B, stroke: 'var(--axis)' });
    (o.refs || []).forEach(function (r) {
      mk(svg, 'line', { x1: L, x2: W - R, y1: Y(r.y), y2: Y(r.y), 'class': 'ref', style: r.color ? 'stroke:' + r.color : '' });
      if (r.label) mk(svg, 'text', { x: W - R + 6, y: Y(r.y) + 4, 'font-size': 11, 'class': 't2' }, r.label);
    });
    (o.vmarks || []).forEach(function (vm) {
      mk(svg, 'line', { x1: X(vm.x), x2: X(vm.x), y1: T, y2: H - B, stroke: 'var(--text-primary)', 'stroke-width': 1, 'stroke-dasharray': '3 3' });
      mk(svg, 'text', { x: X(vm.x) + 4, y: T + 10, 'font-size': 11, 'class': 't2' }, vm.label);
    });
    var lg = o.legend && document.getElementById(o.legend);
    o.series.forEach(function (s) {
      var d = '';
      s.v.forEach(function (v, i) { d += (i ? ' L' : 'M') + X(xs[i]).toFixed(1) + ',' + Y(v).toFixed(1); });
      mk(svg, 'path', { d: d, 'class': 'ln', stroke: s.color });
      if (o.direct) {
        var lv = s.v[s.v.length - 1];
        mk(svg, 'text', { x: W - R + 6, y: Y(lv) + 4 + (s.dy || 0), 'font-size': 12 }, s.name);
      }
      if (lg) {
        var li = document.createElement('span'); li.className = 'li';
        var sw = document.createElement('span'); sw.className = 'ln'; sw.style.borderColor = s.color;
        li.appendChild(sw); li.appendChild(document.createTextNode(s.name)); lg.appendChild(li);
      }
    });
    if (lg && o.legendExtra) {
      var li2 = document.createElement('span'); li2.className = 'li';
      var sw2 = document.createElement('span'); sw2.className = 'ln dash'; sw2.style.borderColor = 'var(--text-muted)';
      li2.appendChild(sw2); li2.appendChild(document.createTextNode(o.legendExtra)); lg.appendChild(li2);
    }
    // Hover: crosshair at nearest x, dots on each series, tooltip with all values
    var xh = mk(svg, 'line', { y1: T, y2: H - B, 'class': 'xhair', visibility: 'hidden' });
    var dots = o.series.map(function (s) { return mk(svg, 'circle', { r: 5, fill: s.color, 'class': 'dot', visibility: 'hidden' }); });
    var hit = mk(svg, 'rect', { x: L, y: T, width: W - L - R, height: H - T - B, fill: 'transparent', tabindex: 0,
                               'aria-label': o.aria });
    var cur = -1;
    function show(i) {
      cur = i;
      var xv = X(xs[i]);
      xh.setAttribute('x1', xv); xh.setAttribute('x2', xv); xh.setAttribute('visibility', 'visible');
      tip.innerHTML = '';
      var hd = document.createElement('div'); hd.className = 'tl'; hd.textContent = o.xTip(xs[i]); tip.appendChild(hd);
      o.series.forEach(function (s, k) {
        dots[k].setAttribute('cx', xv); dots[k].setAttribute('cy', Y(s.v[i])); dots[k].setAttribute('visibility', 'visible');
        var r = document.createElement('div');
        var sw = document.createElement('span'); sw.className = 'sw'; sw.style.background = s.color; sw.style.marginRight = '6px';
        var b = document.createElement('strong'); b.textContent = o.yTip(s.v[i]);
        r.appendChild(sw); r.appendChild(document.createTextNode(s.name + ': ')); r.appendChild(b);
        if (o.extraTip) { var ex = o.extraTip(k, i); if (ex) { var e2 = document.createElement('span'); e2.className = 'tl'; e2.textContent = ' ' + ex; r.appendChild(e2); } }
        tip.appendChild(r);
      });
      var cb = card.getBoundingClientRect(), sb = svg.getBoundingClientRect();
      var px = (sb.left - cb.left) + xv / W * sb.width + 14;
      tip.style.left = Math.min(px, cb.width - 250) + 'px';
      tip.style.top = ((sb.top - cb.top) + 10) + 'px';
      tip.style.opacity = 1;
    }
    function hide() { xh.setAttribute('visibility', 'hidden'); dots.forEach(function (d) { d.setAttribute('visibility', 'hidden'); }); tip.style.opacity = 0; cur = -1; }
    function nearest(evt) {
      var sb = svg.getBoundingClientRect(), vx = (evt.clientX - sb.left) / sb.width * W, best = 0;
      xs.forEach(function (v, i) { if (Math.abs(X(v) - vx) < Math.abs(X(xs[best]) - vx)) best = i; });
      return best;
    }
    hit.addEventListener('pointermove', function (e) { show(nearest(e)); });
    hit.addEventListener('pointerleave', hide);
    hit.addEventListener('focus', function () { show(cur < 0 ? xs.length - 1 : cur); });
    hit.addEventListener('blur', hide);
    hit.addEventListener('keydown', function (e) {
      if (e.key === 'ArrowLeft') { show(Math.max(0, cur - 1)); e.preventDefault(); }
      if (e.key === 'ArrowRight') { show(Math.min(xs.length - 1, cur + 1)); e.preventDefault(); }
    });
    // Table view
    var tb = document.getElementById(o.table), t = document.createElement('table'), hr = t.insertRow();
    [o.xLabel].concat(o.series.map(function (s) { return s.name; })).forEach(function (h, j) {
      var th = document.createElement('th'); th.textContent = h; if (j) th.className = 'n'; hr.appendChild(th); });
    xs.forEach(function (xv, i) {
      var r = t.insertRow(), c = r.insertCell(); c.textContent = o.xTip(xv);
      o.series.forEach(function (s) { var d = r.insertCell(); d.className = 'n'; d.textContent = o.yTip(s.v[i]); });
    });
    tb.appendChild(t);
  }

  document.querySelectorAll('button[data-tbl]').forEach(function (b) {
    b.addEventListener('click', function () {
      var tb = document.getElementById(b.getAttribute('data-tbl'));
      tb.hidden = !tb.hidden; b.textContent = tb.hidden ? 'Show table' : 'Hide table';
    });
  });

  // Margin vs frequency: x ascending in MHz
  (function () {
    var m = DATA.margin, idx = m.periods.map(function (p, i) { return i; }).reverse();
    var xs = idx.map(function (i) { return 1000 / m.periods[i]; });
    var cols = ['var(--s1)', 'var(--s2)', 'var(--s3)'];
    var ser = m.series.map(function (s, k) { return { name: s.name.split(' (')[0], v: idx.map(function (i) { return s.v[i]; }), color: cols[k], full: s.name }; });
    var lo = Math.min.apply(null, ser[0].v), hi = Math.max.apply(null, ser[2].v);
    lineChart({ svg: 'marginChart', card: 'marginCard', tip: 'marginTip', table: 'marginTable', legend: 'marginLegend',
      x: xs, series: ser, direct: true, rightPad: 90,
      yMin: Math.floor(lo / 4) * 4, yMax: Math.ceil(hi / 4) * 4,
      yTicks: (function () { var a = []; for (var v = Math.floor(lo / 4) * 4; v <= Math.ceil(hi / 4) * 4; v += 4) a.push(v); return a; })(),
      xTicks: [34, 36, 38, 40, 42, 44, 46, 48, 50].filter(function (v) { return v >= xs[0] - 0.01 && v <= xs[xs.length - 1] + 0.01; }),
      xFmt: function (v) { return v; }, yFmt: function (v) { return (v > 0 ? '+' : '') + v; },
      xLabel: 'clk frequency (MHz)', refs: [{ y: 0, label: '0 ns' }], vmarks: [{ x: 40, label: '40 MHz spec' }],
      xTip: function (v) { return fmt(v, 2) + ' MHz (' + fmt(1000 / v, 1) + ' ns)'; },
      yTip: function (v) { return (v >= 0 ? '+' : '') + fmt(v, 2) + ' ns'; },
      aria: 'Worst setup slack by corner family against clk frequency. Use left and right arrow keys to step through points.' });
  })();

  // Weights vs sample
  (function () {
    var s = DATA.sysid, xs = s.w.map(function (r) { return r[0]; });
    var cols = ['var(--s1)', 'var(--s2)', 'var(--s3)', 'var(--s4)'];
    var ser = [0, 1, 2, 3].map(function (k) { return { name: 'w[' + k + ']', v: s.w.map(function (r) { return r[k + 1]; }), color: cols[k] }; });
    lineChart({ svg: 'wChart', card: 'wCard', tip: 'wTip', table: 'wTable', legend: 'wLegend', legendExtra: 'true plant tap h[k]',
      x: xs, series: ser, direct: true, rightPad: 70,
      yMin: -0.4, yMax: 0.5, yTicks: [-0.4, -0.2, 0, 0.2, 0.4],
      xTicks: [0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000],
      xFmt: function (v) { return v.toLocaleString('en-CA'); }, yFmt: function (v) { return fmt(v, 1); },
      xLabel: 'sample', vmarks: [{ x: 1500, label: 'μ → 2⁻¹⁰' }],
      refs: s.h.map(function (h, k) { return { y: h, color: cols[k] }; }),
      xTip: function (v) { return 'after sample ' + v.toLocaleString('en-CA'); },
      yTip: function (v) { return (v >= 0 ? '+' : '') + fmt(v, 4); },
      extraTip: function (k, i) { var d = Math.round((ser[k].v[i] - s.h[k]) * 64); return '(h ' + (s.h[k] >= 0 ? '+' : '') + fmt(s.h[k], 4) + ', ' + (d >= 0 ? '+' : '') + d + ' LSB)'; },
      aria: 'Weights read back from the chip against sample number. Use left and right arrow keys to step through readouts.' });
  })();

  // Error power vs sample
  (function () {
    var s = DATA.sysid, xs = s.eblk.map(function (v, i) { return (i + 1) * s.B; });
    lineChart({ svg: 'eChart', card: 'eCard', tip: 'eTip', table: 'eTable',
      x: xs, series: [{ name: 'e²', v: s.eblk, color: 'var(--s1)' }], direct: false, rightPad: 110,
      xMin: 0, yMin: -50, yMax: -10, yTicks: [-50, -40, -30, -20, -10],
      xTicks: [0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000],
      xFmt: function (v) { return v.toLocaleString('en-CA'); }, yFmt: function (v) { return v + ' dB'; },
      xLabel: 'sample', refs: [{ y: s.rpow, label: 'r power ' + fmt(s.rpow, 1) + ' dB' }],
      vmarks: [{ x: 1500, label: 'μ → 2⁻¹⁰' }],
      xTip: function (v) { return 'samples ' + (v - s.B + 1).toLocaleString('en-CA') + '–' + v.toLocaleString('en-CA'); },
      yTip: function (v) { return fmt(v, 1) + ' dB'; },
      aria: 'Error power from the chip in 50-sample blocks against sample number. Use left and right arrow keys to step through blocks.' });
  })();
})();
