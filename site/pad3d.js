"use strict";
// Original site code, MIT (see LICENSE.md). The live 3D controller on the controls card: the same model and
// look as the in-VR cards and WuWa VR menu (mod/uevr/src/utility/WuWaPad3D.hpp, exported to media/pad3d.json
// by dev/imgui-preview --export-pad3d). Gold wire and soft light points, nearer parts brighter, turning slowly;
// pointing at a row presses its button, as holding L3 and pressing it does in the headset.
window.WuWaPad3D = (() => {
  const BIT = {A: 1, B: 2, X: 4, Y: 8, LB: 16, RB: 32, LT: 64, RT: 128, L3: 256, R3: 512, View: 1024, Menu: 2048,
    Up: 4096, Down: 8192, Left: 16384, Right: 32768};
  const DPAD = BIT.Up | BIT.Down | BIT.Left | BIT.Right;
  const SHAPE = {disc: 0, stick: 1, rect: 2, block: 3, cross: 4};
  const LOOK = {line: [214, 175, 91], deep: [118, 152, 214], lit: [255, 220, 140], text: [249, 232, 180], dark: [26, 18, 6]};
  const GLYPHS = new Set(["triangle", "circle", "cross", "square"]);
  const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]], mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
  const rgba = (c, a) => `rgba(${c[0] | 0},${c[1] | 0},${c[2] | 0},${Math.max(0, Math.min(1, a)).toFixed(3)})`;
  const mix = (a, b, t, alpha) => { t = Math.max(0, Math.min(1, t)); return rgba([a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t], alpha); };
  const hex = h => [parseInt(h.slice(1, 3), 16), parseInt(h.slice(3, 5), 16), parseInt(h.slice(5, 7), 16)];
  // On VR controllers View is the left grip + Menu, and there is no D-pad.
  const onModel = (mask, device) => device !== "vr" ? mask : ((mask & BIT.View ? (mask & ~BIT.View) | BIT.LB | BIT.Menu : mask) & ~DPAD);
  // What a card row presses, and which button its leader runs to.
  const pressOf = id => id === "YX" ? BIT.Y | BIT.X : BIT[id] || 0;
  const leaderOf = (id, device) => id === "YX" ? BIT.Y : device === "vr" && id === "View" ? BIT.Menu : device === "vr" && id === "Down" ? 0 : BIT[id] || 0;

  function projector(model, box, yaw, pitch, sway, fill) {
    const p = {cx: 0, cy: 0, scale: 1, distance: 3.4, cyw: 1, syw: 0, cp: 1, sp: 0};
    p.turn = (a, b) => { p.cyw = Math.cos(a); p.syw = Math.sin(a); p.cp = Math.cos(b); p.sp = Math.sin(b); };
    p.view = q => { const x = q[0] * p.cyw + q[2] * p.syw, z1 = -q[0] * p.syw + q[2] * p.cyw; return [x, q[1] * p.cp - z1 * p.sp, q[1] * p.sp + z1 * p.cp]; };
    p.screen = v => { const f = p.distance / (p.distance - v[2]); return [p.cx + v[0] * f * p.scale, p.cy - v[1] * f * p.scale]; };
    p.at = q => p.screen(p.view(q));
    // Fit the model, turned either way by `sway`, into the box (as wuwa_pad3d::fit_model).
    let lx = Infinity, ly = Infinity, hx = -Infinity, hy = -Infinity;
    for (const sample of [-sway, 0, sway]) {
      p.turn(sample, model.pitch);
      const grow = (q, r) => { const s = p.at(q); lx = Math.min(lx, s[0] - r); hx = Math.max(hx, s[0] + r); ly = Math.min(ly, s[1] - r); hy = Math.max(hy, s[1] + r); };
      for (const q of model.points) grow(q, 0);
      for (const part of model.parts) grow(part.c, Math.max(part.ru, part.rv));
    }
    p.scale = Math.min(box.w * fill / Math.max(hx - lx, 0.01), box.h * fill / Math.max(hy - ly, 0.01));
    p.cx = box.x + box.w / 2 - (lx + hx) / 2 * p.scale;
    p.cy = box.y + box.h / 2 - (ly + hy) / 2 * p.scale;
    p.turn(yaw, pitch);
    return p;
  }

  function glyph(ctx, c, r, name, color, thick) {
    ctx.strokeStyle = color; ctx.lineWidth = thick; ctx.beginPath();
    if (name === "triangle") { ctx.moveTo(c[0], c[1] - r); ctx.lineTo(c[0] + r * 0.87, c[1] + r * 0.5); ctx.lineTo(c[0] - r * 0.87, c[1] + r * 0.5); ctx.closePath(); }
    else if (name === "circle") ctx.arc(c[0], c[1], r * 0.8, 0, Math.PI * 2);
    else if (name === "cross") { ctx.moveTo(c[0] - r * 0.7, c[1] - r * 0.7); ctx.lineTo(c[0] + r * 0.7, c[1] + r * 0.7); ctx.moveTo(c[0] + r * 0.7, c[1] - r * 0.7); ctx.lineTo(c[0] - r * 0.7, c[1] + r * 0.7); }
    else if (name === "square") ctx.rect(c[0] - r * 0.65, c[1] - r * 0.65, r * 1.3, r * 1.3);
    ctx.stroke();
  }
  const poly = (ctx, pts) => { ctx.beginPath(); pts.forEach((q, i) => i ? ctx.lineTo(q[0], q[1]) : ctx.moveTo(q[0], q[1])); ctx.closePath(); };

  // Draws `model` showing `held` (and a ripple from `ripple`, 0..1) in box; returns where each control is.
  function draw(ctx, model, device, box, state) {
    const {time, held = 0, ripple = -1, rippleMask = 0, still = false, sway = 0.16, swaySeconds = 12, fill = 0.94} = state;
    const yaw = still ? 0 : sway * Math.sin(time * Math.PI * 2 / swaySeconds);
    const pitch = model.pitch + (still ? 0 : 0.04 * Math.sin(time * Math.PI * 2 / (swaySeconds * 1.7)));
    const pr = projector(model, box, yaw, pitch, still ? 0 : sway, fill);
    const viewed = model.points.map(pr.view);
    let zlo = Infinity, zhi = -Infinity;
    for (const v of viewed) { zlo = Math.min(zlo, v[2]); zhi = Math.max(zhi, v[2]); }
    const zspan = Math.max(zhi - zlo, 0.01), closeness = z => (z - zlo) / zspan;

    // The light field: a halo, a glow and a core per point, twinkling, a band of light sweeping across.
    const dot = Math.max(0.8, pr.scale * 0.0075), sweep = still ? -9 : ((time / 5) % 1) * 3 - 1.5;
    model.splats.forEach((s, i) => {
      const v = pr.view(s), close = closeness(v[2]), size = s[3];
      const twinkle = still ? 1 : 0.78 + 0.22 * Math.sin(time * 1.7 + i * 2.39);
      const bright = twinkle * (1 + 0.9 * Math.exp(-(v[0] - sweep) * (v[0] - sweep) / 0.02));
      const q = pr.screen(v);
      for (const [r, a, to] of [[3, 0.045, LOOK.line], [1.6, 0.12, LOOK.line], [0.7, (0.3 + 0.5 * close), LOOK.lit]]) {
        ctx.fillStyle = mix(LOOK.deep, to, close, a * bright);
        ctx.beginPath(); ctx.arc(q[0], q[1], dot * r * size, 0, Math.PI * 2); ctx.fill();
      }
    });
    const thick = Math.max(1, pr.scale * 0.004);
    ctx.lineWidth = thick;
    for (let i = 0; i < model.edges.length; i += 2) {
      const a = viewed[model.edges[i]], b = viewed[model.edges[i + 1]], close = closeness((a[2] + b[2]) / 2);
      const p0 = pr.screen(a), p1 = pr.screen(b);
      ctx.strokeStyle = mix(LOOK.deep, LOOK.line, close, 0.18 + 0.5 * close);
      ctx.beginPath(); ctx.moveTo(p0[0], p0[1]); ctx.lineTo(p1[0], p1[1]); ctx.stroke();
    }

    // Controls, the far ones first.
    const anchors = [], mask = onModel(held, device), rippling = onModel(rippleMask, device);
    const parts = [...model.parts].sort((a, b) => pr.view(a.c)[2] - pr.view(b.c)[2]);
    for (const part of parts) {
      const lit = (mask & part.control) !== 0, sink = lit ? 1 : 0;
      const close = closeness(pr.view(part.c)[2]), facing = pr.view(part.n)[2], shown = facing > 0 ? 1 : 0.45;
      const top = add(part.c, mul(part.n, part.depth * (1 - 0.8 * sink)));
      const edge = lit ? rgba(LOOK.lit, 1) : mix(LOOK.deep, LOOK.line, close, (0.3 + 0.6 * close) * shown);
      const fillColor = lit ? rgba(LOOK.lit, 0.95) : `rgba(14,12,9,${(0.82 * shown).toFixed(3)})`;
      const lineW = lit ? thick * 1.8 : thick;
      const ring = (c, ru, rv, n) => Array.from({length: n}, (_, i) => { const a = i * Math.PI * 2 / n; return pr.at(add(add(c, mul(part.u, Math.cos(a) * ru)), mul(part.v, Math.sin(a) * rv))); });
      const quad = (c, ru, rv) => [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([a, b]) => pr.at(add(add(c, mul(part.u, a * ru)), mul(part.v, b * rv))));
      const shape = (pts, f, s, w) => { poly(ctx, pts); if (f) { ctx.fillStyle = f; ctx.fill(); } if (s) { ctx.strokeStyle = s; ctx.lineWidth = w; ctx.stroke(); } };
      const longPart = part.shape === SHAPE.rect || part.shape === SHAPE.block;
      const rPx = Math.max(part.ru, part.rv) * pr.scale, nearPx = longPart ? Math.min(part.ru, part.rv) * pr.scale : rPx;
      const labelAt = part.shape === SHAPE.stick ? pr.at(add(part.c, mul(part.n, 0.07 - 0.035 * sink))) : pr.at(top);
      if (lit) for (const [r, a] of [[1.9, 0.06], [1.4, 0.09]]) { ctx.fillStyle = rgba(LOOK.lit, a); ctx.beginPath(); ctx.arc(labelAt[0], labelAt[1], nearPx * r, 0, Math.PI * 2); ctx.fill(); }
      if (part.shape === SHAPE.stick) {
        shape(ring(part.c, part.ru * 1.45, part.rv * 1.45, 22), null, mix(LOOK.deep, LOOK.line, close, (0.3 + 0.4 * close) * shown), thick);
        const cap = add(part.c, mul(part.n, 0.07 - 0.035 * sink));
        const capRing = ring(cap, part.ru, part.rv, 20), stem = ring(add(part.c, mul(part.n, 0.01)), part.ru * 0.7, part.rv * 0.7, 20);
        ctx.strokeStyle = edge; ctx.lineWidth = thick; ctx.beginPath();
        for (let i = 0; i < 20; i += 5) { ctx.moveTo(capRing[i][0], capRing[i][1]); ctx.lineTo(stem[i][0], stem[i][1]); }
        ctx.stroke();
        shape(capRing, fillColor, edge, lineW);
        shape(ring(cap, part.ru * 0.62, part.rv * 0.62, 16), null, lit ? rgba(LOOK.dark, 0.5) : edge.replace(/[\d.]+\)$/, a => (parseFloat(a) * 0.5).toFixed(3) + ")"), thick);
      } else if (part.shape === SHAPE.disc) {
        shape(ring(top, part.ru, part.rv, 18), fillColor, edge, lineW);
      } else if (part.shape === SHAPE.rect) {
        shape(quad(top, part.ru, part.rv), fillColor, edge, lineW);
      } else if (part.shape === SHAPE.block) {
        const cap = quad(top, part.ru, part.rv), base = quad(part.c, part.ru * 0.96, part.rv * 0.96);
        poly(ctx, cap); ctx.fillStyle = fillColor; ctx.fill();
        ctx.strokeStyle = edge; ctx.lineWidth = thick; ctx.beginPath();
        for (let i = 0; i < 4; i++) { ctx.moveTo(cap[i][0], cap[i][1]); ctx.lineTo(base[i][0], base[i][1]); }
        ctx.stroke();
        shape(base, null, edge.replace(/[\d.]+\)$/, a => (parseFloat(a) * 0.6).toFixed(3) + ")"), thick);
        shape(cap, null, edge, lineW);
      } else if (part.shape === SHAPE.cross) {
        for (const [du, dv, bit] of [[0, 1, BIT.Up], [0, -1, BIT.Down], [1, 0, BIT.Right], [-1, 0, BIT.Left]]) {
          const armLit = (mask & bit) !== 0;
          const centre = add(add(top, mul(part.u, du * part.ru * 1.25)), mul(part.v, dv * part.rv * 1.25));
          shape(quad(centre, part.ru * 0.6, part.rv * 0.6), armLit ? rgba(LOOK.lit, 0.95) : `rgba(14,12,9,${(0.82 * shown).toFixed(3)})`, armLit ? rgba(LOOK.lit, 1) : edge, armLit ? lineW : thick);
        }
      }
      if (ripple >= 0 && (rippling & part.control)) {
        ctx.strokeStyle = rgba(LOOK.lit, 0.85 * (1 - ripple)); ctx.lineWidth = thick * (2 - ripple);
        ctx.beginPath(); ctx.arc(labelAt[0], labelAt[1], nearPx * (1.15 + 1.5 * ripple), 0, Math.PI * 2); ctx.stroke();
      }
      anchors.push({control: part.control, at: labelAt, radius: longPart ? nearPx * 0.45 : nearPx});

      // The name on it: always when lit, otherwise when there is room and it faces the viewer.
      const text = part.label;
      if (!text) continue;
      const px = Math.max(6, Math.min(44, Math.min(part.ru, part.rv) * pr.scale * 1.15));
      if (!lit && (px < 10 || facing < 0.15)) continue;
      const color = lit ? rgba(LOOK.dark, 1) : part.tint ? rgba(hex(part.tint), shown) : rgba(LOOK.text, shown);
      if (GLYPHS.has(text)) { glyph(ctx, labelAt, rPx * 0.55, text, color, Math.max(1, rPx * 0.12)); continue; }
      if (!lit && text.length > 4 && px < 16) continue;
      let size = px;
      ctx.font = `600 ${size}px Kanit, "Segoe UI", system-ui, sans-serif`;
      const room = longPart ? part.ru * 2 * pr.scale * 0.86 : rPx * 1.6, w = ctx.measureText(text).width;
      if (w > room && w > 0) size *= room / w;
      if (size < 7) continue;
      ctx.font = `600 ${size}px Kanit, "Segoe UI", system-ui, sans-serif`;
      ctx.fillStyle = color; ctx.textAlign = "center"; ctx.textBaseline = "middle";
      ctx.fillText(text, labelAt[0], labelAt[1] + size * 0.04);
    }
    return anchors;
  }

  // The card: a canvas over the whole diagram (the model where the drawing was, leaders from each row to its
  // button underneath), the old SVG kept for layout, print and no-script readers.
  function attach(card, models, getDevice) {
    const diagram = card.querySelector(".pad-diagram"), drawing = diagram?.querySelector(".pad-svg");
    if (!diagram || !drawing) return null;
    const canvas = document.createElement("canvas");
    canvas.className = "pad-3d";
    canvas.setAttribute("aria-hidden", "true");
    diagram.append(canvas);
    card.classList.add("is-3d");
    const ctx = canvas.getContext("2d");
    const still = window.matchMedia?.("(prefers-reduced-motion: reduce)");
    const rows = [...diagram.querySelectorAll(".pad-list li[data-btn]")];
    const demo = ["LT", "RT", "LB", "RB", "View", "Menu", "YX", "B", "A", "R3"];
    let hot = null, hotAt = 0, demoIndex = 0, demoAt = 0, visible = true, frame = 0, last = 0;
    const press = (id, at) => { hot = id; hotAt = at; last = 0; schedule(); };
    rows.forEach(li => {
      li.addEventListener("mouseenter", () => press(li.dataset.btn, performance.now() / 1000));
      li.addEventListener("mouseleave", () => { if (hot === li.dataset.btn) press(null, 0); });
      li.addEventListener("click", () => press(hot === li.dataset.btn ? null : li.dataset.btn, performance.now() / 1000));
    });
    const render = now => {
      frame = 0;
      const t = now / 1000, reduce = !!still?.matches;
      if (!reduce && t - last < 1 / 40) { schedule(); return; }
      last = t;
      const rect = diagram.getBoundingClientRect(), padRect = drawing.getBoundingClientRect(), dpr = Math.min(window.devicePixelRatio || 1, 2);
      const w = Math.round(rect.width * dpr), h = Math.round(rect.height * dpr);
      if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, rect.width, rect.height);
      const device = getDevice(), model = models[device === "ps" ? "playstation" : device] || models.xbox;
      // Nothing pointed at: press each row in turn, as a player trying the card would.
      let id = hot, since = hotAt, demoing = false;
      if (!id && !reduce) {
        if (t - demoAt > 1.8) { demoAt = t; demoIndex = (demoIndex + 1) % demo.length; }
        id = demo[demoIndex]; since = demoAt; demoing = true;
        if (device === "vr" && id === "Down") id = null;
      }
      const pressed = id ? pressOf(id) : 0, k = (t - since) / 0.6;
      const box = {x: padRect.left - rect.left, y: padRect.top - rect.top, w: padRect.width, h: padRect.height};
      const anchors = draw(ctx, model, device, box, {time: t, still: reduce, held: BIT.L3 | pressed, ripple: id && k < 1 ? k : -1, rippleMask: pressed});
      // Leaders behind what is drawn, so the buttons sit over them; only while the lists flank the drawing.
      const flanked = diagram.querySelector(".pad-left").getBoundingClientRect().right <= padRect.left + 1;
      ctx.globalCompositeOperation = "destination-over";
      if (flanked) {
        for (const li of rows) {
          const bits = leaderOf(li.dataset.btn, device), anchor = anchors.find(a => a.control & bits);
          if (!anchor || li.offsetParent === null) continue;
          const r = li.getBoundingClientRect(), left = !!li.closest(".pad-left"), y = r.top + r.height / 2 - rect.top;
          const x = (left ? r.right : r.left) - rect.left, bend = x + (left ? 14 : -14);
          const dx = anchor.at[0] - bend, dy = anchor.at[1] - y, length = Math.hypot(dx, dy) || 1, back = (anchor.radius * 1.15 + 3) / length;
          const end = [anchor.at[0] - dx * back, anchor.at[1] - dy * back], on = li.dataset.btn === id;
          ctx.strokeStyle = on ? "rgba(249,232,180,.9)" : "rgba(214,178,98,.45)"; ctx.lineWidth = on ? 2 : 1.2;
          ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(bend, y); ctx.lineTo(end[0], end[1]); ctx.stroke();
          ctx.fillStyle = on ? "#f9e8b4" : "#d6af5b"; ctx.beginPath(); ctx.arc(end[0], end[1], on ? 4 : 3.2, 0, Math.PI * 2); ctx.fill();
        }
      }
      ctx.globalCompositeOperation = "source-over";
      rows.forEach(li => li.classList.toggle("is-demo", demoing && li.dataset.btn === id));
      if (!reduce || (id && k < 1)) schedule();
    };
    const schedule = () => { if (visible && !frame) frame = requestAnimationFrame(render); };
    if ("IntersectionObserver" in window) new IntersectionObserver(entries => { visible = entries.some(e => e.isIntersecting); schedule(); }).observe(card);
    if (window.ResizeObserver) new ResizeObserver(() => schedule()).observe(diagram);
    still?.addEventListener?.("change", () => schedule());
    schedule();
    return {redraw: () => { last = 0; schedule(); }};
  }

  return {attach, draw, BIT};
})();
