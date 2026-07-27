// Tutorial player: builds the 3D keyboard, renders the fake-notepad IME
// screen, and plays scripted key/​screen animations from TUTORIALS.

const $ = s => document.querySelector(s);
const sleep = ms => new Promise(r => setTimeout(r, ms));

/* ---------- keyboard ---------- */

const keyEls = {};

function buildKeyboard() {
  const body = $('#kbBody');
  // slab thickness faces
  for (const cls of ['kb-under', 'kb-front', 'kb-back', 'kb-left', 'kb-right']) {
    const d = document.createElement('div');
    d.className = cls;
    body.appendChild(d);
  }
  for (const row of KEY_ROWS) {
    const rowEl = document.createElement('div');
    rowEl.className = 'kb-row';
    for (const [id, label, size] of row) {
      const key = document.createElement('div');
      key.className = 'key' + (size > 1 ? ' wide' : '');
      key.style.width = `calc(var(--u) * ${size} + var(--kgap) * ${size - 1})`;
      key.dataset.key = id;
      const side = document.createElement('div');
      side.className = 'side';
      key.appendChild(side);
      const cap = document.createElement('div');
      cap.className = 'cap';
      cap.innerHTML = `<span class="main">${label}</span>`;
      const low = id.toLowerCase();
      if (INITIALS[low] && id.length === 1) cap.innerHTML += `<span class="sub-i">${INITIALS[low]}</span>`;
      if (FINALS[low] && id.length === 1) cap.innerHTML += `<span class="sub-f">${FINALS[low]}</span>`;
      if (CONTROLS[id]) cap.innerHTML += `<span class="sub-c">${CONTROLS[id]}</span>`;
      key.appendChild(cap);
      rowEl.appendChild(key);
      keyEls[id] = key;
    }
    body.appendChild(rowEl);
  }
}

function pressKey(id, dur = 255) {
  const el = keyEls[id];
  if (!el) return;
  el.classList.add('pressed');
  setTimeout(() => el.classList.remove('pressed'), dur);
}

/* ---------- keyboard rotation (clamped: never shows the back) ---------- */

const rot = { x: 26, y: 0 };
function applyRot() {
  $('#kbTilt').style.transform = `rotateX(${rot.x}deg) rotateY(${rot.y}deg)`;
}
function setupDrag() {
  const stage = $('#kbStage');
  let dragging = false, px = 0, py = 0;
  stage.addEventListener('pointerdown', e => {
    dragging = true; px = e.clientX; py = e.clientY;
    stage.classList.add('dragging');
    stage.setPointerCapture(e.pointerId);
  });
  stage.addEventListener('pointermove', e => {
    if (!dragging) return;
    rot.y = Math.max(-32, Math.min(32, rot.y + (e.clientX - px) * 0.25));
    rot.x = Math.max(10, Math.min(52, rot.x - (e.clientY - py) * 0.25));
    px = e.clientX; py = e.clientY;
    applyRot();
  });
  const end = () => { dragging = false; stage.classList.remove('dragging'); };
  stage.addEventListener('pointerup', end);
  stage.addEventListener('pointercancel', end);
}

/* ---------- screen rendering ---------- */

const EMPTY = { text: '', comp: [], anchor: null, cur: null, menu: null, mode: 'zh' };

function renderScreen(st) {
  $('#committed').textContent = st.text;
  const compEl = $('#composition');
  compEl.innerHTML = '';
  // blinking caret sits at the IME cursor (st.cur index; null = at the end)
  const caret = document.createElement('span');
  caret.className = 'caret';
  st.comp.forEach(([ch, kind], i) => {
    if (st.cur === i) compEl.appendChild(caret);
    const s = document.createElement('span');
    s.className = 'ch ' + (kind === 's' ? 'settled' : 'pending');
    if (st.anchor === i && !st.menu) s.classList.add('anchor');
    if (st.menu && st.menu.anchor === i) s.classList.add('anchor');
    s.textContent = ch;
    compEl.appendChild(s);
  });
  if (st.cur == null || st.cur >= st.comp.length) compEl.appendChild(caret);
  const badge = $('#modeBadge');
  badge.textContent = st.mode === 'en' ? '英' : '中';
  badge.className = 'mode-badge ' + st.mode;
  $('#modeHint').textContent = st.mode === 'en' ? '英文模式（按鍵放行）' : '中文模式';

  const card = $('#candCard');
  if (st.menu) {
    card.innerHTML =
      st.menu.items.map((t, i) =>
        `<div class="cand-row${st.menu.sel === i ? ' sel' : ''}"><span class="cn">${i + 1}</span>${t}</div>`
      ).join('') +
      `<div class="cand-pager">${st.menu.page}</div>`;
    card.hidden = false;
    // place under the anchor character
    const spans = compEl.children;
    const target = spans[Math.min(st.menu.anchor, spans.length - 1)];
    if (target) {
      const bodyRect = $('#npBody').getBoundingClientRect();
      const r = target.getBoundingClientRect();
      let left = r.left - bodyRect.left;
      left = Math.min(left, bodyRect.width - card.offsetWidth - 10);
      card.style.left = left + 'px';
      // below the anchor char; flip above only when it actually fits there
      // (mirrors the real IME's CalcFitPointAroundTextExtent behavior)
      let top = r.bottom - bodyRect.top + 4;
      const above = r.top - bodyRect.top - card.offsetHeight - 4;
      if (top + card.offsetHeight > bodyRect.height - 4 && above >= 4) {
        top = above;
      }
      card.style.top = top + 'px';
    }
  } else {
    card.hidden = true;
  }
}

/* ---------- tutorial player ---------- */

const player = {
  ti: 0, si: -1, states: [], playing: false, gen: 0,

  load(i) {
    this.ti = i;
    this.gen++;
    const t = TUTORIALS[i];
    // fold each step's partial screen over the previous full state
    this.states = [];
    let st = { ...EMPTY };
    for (const step of t.steps) {
      st = { ...st, ...(step.screen || {}) };
      this.states.push(st);
    }
    this.si = -1;
    renderScreen(EMPTY);
    document.querySelectorAll('#nav button').forEach((b, k) =>
      b.classList.toggle('active', k === i));
    $('#capStep').textContent = t.title;
    $('#capText').innerHTML = '準備播放…';
    this.play();
  },

  async stepTo(k, animate, gen) {
    const t = TUTORIALS[this.ti];
    if (k < 0 || k >= t.steps.length) return;
    const step = t.steps[k];
    if (animate) {
      for (const key of step.keys) {
        if (gen !== this.gen) return;
        pressKey(key);
        await sleep(450);
      }
    }
    if (gen !== undefined && gen !== this.gen) return;
    this.si = k;
    renderScreen(this.states[k]);
    $('#capStep').textContent = `${t.title}　·　步驟 ${k + 1} / ${t.steps.length}`;
    $('#capText').innerHTML = step.cap;
  },

  async play() {
    const t = TUTORIALS[this.ti];
    if (this.si >= t.steps.length - 1) { this.load(this.ti); return; }
    const gen = ++this.gen;
    this.playing = true;
    this.updateBtn();
    while (this.si < t.steps.length - 1) {
      await this.stepTo(this.si + 1, true, gen);
      if (gen !== this.gen) return;
      const cap = t.steps[this.si].cap;
      await sleep(Math.min(8000, 1600 + cap.length * 55));
      if (gen !== this.gen) return;
    }
    if (gen === this.gen) { this.playing = false; this.updateBtn(); }
  },

  pause() { this.gen++; this.playing = false; this.updateBtn(); },

  toggle() { this.playing ? this.pause() : this.play(); },

  next() { this.pause(); this.stepTo(this.si + 1, true, this.gen); },

  prev() { this.pause(); this.stepTo(Math.max(0, this.si - 1), false); },

  restart() { this.load(this.ti); },

  updateBtn() { $('#btnPlay').textContent = this.playing ? '⏸' : '▶'; }
};

/* ---------- boot ---------- */

function buildNav() {
  const nav = $('#nav');
  TUTORIALS.forEach((t, i) => {
    const b = document.createElement('button');
    b.innerHTML = `<span class="no">${i === 0 ? '☆' : i}</span>${t.title}`;
    b.addEventListener('click', () => player.load(i));
    nav.appendChild(b);
  });
}

$('#btnPrev').addEventListener('click', () => player.prev());
$('#btnPlay').addEventListener('click', () => player.toggle());
$('#btnNext').addEventListener('click', () => player.next());
$('#btnRestart').addEventListener('click', () => player.restart());

buildKeyboard();
applyRot();
setupDrag();
buildNav();
player.load(0);
