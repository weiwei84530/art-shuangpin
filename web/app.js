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

// The physical keycap a printable character lives on, and whether Shift is
// needed to reach it. The drill highlights the cap, not the character.
const SHIFTED = {
  '!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '^': '6', '&': '7',
  '*': '8', '(': '9', ')': '0', '_': '-', '+': '=', '{': '[', '}': ']',
  ':': ';', '"': "'", '<': ',', '>': '.', '?': '/', '~': '`'
};

function capFor(key) {
  if (key === 'Space' || key === 'Enter' || key === 'Backspace' || key === 'Tab') {
    return { id: key, shift: false };
  }
  if (SHIFTED[key]) return { id: SHIFTED[key], shift: true };
  return { id: key.toLowerCase(), shift: false };
}

function clearHints() {
  for (const el of Object.values(keyEls)) el.classList.remove('hint');
}

function showHint(key) {
  clearHints();
  const { id, shift } = capFor(key);
  if (keyEls[id]) keyEls[id].classList.add('hint');
  if (shift) {
    keyEls['ShiftL'].classList.add('hint');
    keyEls['ShiftR'].classList.add('hint');
  }
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

/* ---------- typing drill ---------- */

// Plays a lesson from DRILLS: the article on top, the simulated IME screen
// in the notepad, and the next expected key lit up on the keyboard. Wrong
// keys do nothing at all -- no warning, no penalty, the drill simply does
// not move (2026-08-09).

const CP = s => Array.from(s);

// A friendly name for the key being asked for.
const KEY_LABEL = { Space: '空白', Enter: 'Enter' };
const TONE_NOTE = {
  '1': '一聲', '2': '二聲', '3': '三聲', '4': '四聲', '5': '輕聲',
  '0': '一聲（右手）', '9': '二聲（右手）', '8': '三聲（右手）',
  '7': '四聲（右手）', '6': '輕聲（右手）'
};

const drill = {
  di: -1, si: 0, chars: [],

  get lesson() { return DRILLS[this.di]; },

  load(i) {
    this.di = i;
    this.si = 0;
    this.chars = CP(DRILLS[i].text);
    player.pause();
    $('#captionBar').hidden = true;
    $('#drillBar').hidden = false;
    $('#drillTitle').textContent = DRILLS[i].title;
    $('#drillIntro').textContent = DRILLS[i].intro;
    this.render();
  },

  leave() {
    this.di = -1;
    clearHints();
    $('#drillBar').hidden = true;
    $('#captionBar').hidden = false;
  },

  restart() { if (this.di >= 0) this.load(this.di); },

  // The screen as of the last completed keystroke.
  screenAt(index) {
    if (index < 0) return { ...EMPTY };
    const step = this.lesson.steps[index];
    return {
      text: step.t,
      comp: step.c,
      anchor: step.a >= 0 ? step.a : null,
      cur: step.r,
      menu: step.m ? { anchor: step.a >= 0 ? step.a : step.c.length - 1,
                       items: step.m.items, page: step.m.page, sel: null } : null,
      mode: 'zh'
    };
  },

  render() {
    const steps = this.lesson.steps;
    renderScreen(this.screenAt(this.si - 1));

    const done = this.si > 0 ? steps[this.si - 1].d : 0;
    const text = $('#drillText');
    text.innerHTML = '';
    this.chars.forEach((ch, i) => {
      const span = document.createElement('span');
      if (i < done) span.className = 'done';
      else if (i === done) span.className = 'now';
      span.textContent = ch;
      text.appendChild(span);
    });

    $('#drillFill').style.width = (100 * this.si / steps.length) + '%';
    $('#drillCount').textContent = `${this.si} / ${steps.length}`;

    const hint = $('#drillNext');
    if (this.si >= steps.length) {
      clearHints();
      hint.innerHTML = '<span class="cheer">完成了！</span>　按 ↻ 再練一次，或從左邊挑下一課。';
      return;
    }
    const key = steps[this.si].k;
    showHint(key);
    const label = KEY_LABEL[key] || key.toUpperCase();
    // What the key is FOR, read off the screen that is currently up: the
    // menu is open on the step before the digit that picks from it.
    const menuOpen = this.si > 0 && !!steps[this.si - 1].m;
    let note = '';
    if (menuOpen) {
      note = key === '8' ? '　（翻到下一頁）' : '　（在候選單裡選這一個）';
    } else if (TONE_NOTE[key] && this.si > 0 && /^[a-z;]$/.test(steps[this.si - 1].k)) {
      note = `　（${TONE_NOTE[key]}）`;
    } else if (key === 'Space') {
      note = '　（單鍵音節要用空白或聲調收尾）';
    } else if (key === 'Enter') {
      note = '　（整段上屏）';
    } else if (key === '8') {
      note = '　（開候選單）';
    } else if (key === '9' || key === '0') {
      note = key === '9' ? '　（游標往左）' : '　（游標往右）';
    }
    hint.innerHTML = `下一鍵：<kbd>${label}</kbd>${note}`;
  },

  // Returns true when the event was the key the drill is waiting for.
  handle(event) {
    if (this.di < 0) return false;
    const steps = this.lesson.steps;
    if (this.si >= steps.length) return false;
    const want = steps[this.si].k;
    const got = event.key === ' ' ? 'Space' : event.key;
    if (got !== want) return false;
    pressKey(capFor(want).id);
    this.si++;
    this.render();
    return true;
  }
};

/* ---------- boot ---------- */

function buildNav() {
  const nav = $('#nav');
  const heading = label => {
    const d = document.createElement('div');
    d.className = 'nav-group';
    d.textContent = label;
    nav.appendChild(d);
  };

  heading('教學');
  TUTORIALS.forEach((t, i) => {
    const b = document.createElement('button');
    b.dataset.kind = 'tutorial';
    b.innerHTML = `<span class="no">${i === 0 ? '☆' : i}</span>${t.title}`;
    b.addEventListener('click', () => { drill.leave(); player.load(i); });
    nav.appendChild(b);
  });

  heading('看打練習');
  DRILLS.forEach((d, i) => {
    const b = document.createElement('button');
    b.dataset.kind = 'drill';
    b.innerHTML = `<span class="no">⌨</span>${d.title}`;
    b.addEventListener('click', () => {
      document.querySelectorAll('#nav button').forEach(x => x.classList.remove('active'));
      b.classList.add('active');
      drill.load(i);
    });
    nav.appendChild(b);
  });
}

$('#btnPrev').addEventListener('click', () => player.prev());
$('#btnPlay').addEventListener('click', () => player.toggle());
$('#btnNext').addEventListener('click', () => player.next());
$('#btnRestart').addEventListener('click', () => player.restart());
$('#btnDrillRestart').addEventListener('click', () => drill.restart());

// The drill reads the real keyboard, so it has to stop the browser acting
// on Space, Enter and the like -- but only for the key it actually wanted.
window.addEventListener('keydown', e => {
  if (e.ctrlKey || e.altKey || e.metaKey) return;
  if (drill.handle(e)) e.preventDefault();
});

buildKeyboard();
applyRot();
setupDrag();
buildNav();
player.load(0);
