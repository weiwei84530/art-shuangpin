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
      let html = `<span class="main">${label}</span>`;
      const low = id.toLowerCase();
      if (INITIALS[low] && id.length === 1) {
        // The recited vowel goes right under the initial: it is what the
        // key means when it is a syllable all by itself.
        const vowel = SINGLE_VOWELS[low] ? `<i>${SINGLE_VOWELS[low]}</i>` : '';
        html += `<span class="sub-i">${INITIALS[low]}${vowel}</span>`;
      }
      if (FINALS[low] && id.length === 1) {
        html += `<span class="sub-f">` +
                FINALS[low].map(f => `<i>${f}</i>`).join('') + `</span>`;
      }
      if (CONTROLS[id]) html += `<span class="sub-c">${CONTROLS[id]}</span>`;
      cap.innerHTML = html;
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

// The press animation is over in a quarter of a second, which is too fast to
// find a key you do not already know. The keys a tutorial step pressed stay
// lit until the next step, so the caption always has something to point at.
function litKey(id) {
  if (keyEls[id]) keyEls[id].classList.add('lit');
}

function clearLit() {
  document.querySelectorAll('.key.lit').forEach(el => el.classList.remove('lit'));
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

// A shallow tilt on purpose: the annotations are 10px bopomofo, and tilting
// the board foreshortens them VERTICALLY -- which is exactly what flattens
// ㄧ into a dash and blurs ㄛ against ㄜ. Drag it steeper if you want the
// look; the drill needs the reading.
const rot = { x: 18, y: 0 };
function applyRot() {
  $('#kbTilt').style.transform = `rotateX(${rot.x}deg) rotateY(${rot.y}deg)`;
}
// Scales the board down to whatever room is left under the panels. Media
// queries cannot do this: the drill bar is taller than the caption bar, and
// tilting the board changes how tall it draws, so the space is only known
// at run time. Without it the keyboard simply overlaps the panel above.
function fitKeyboard() {
  const stage = $('#kbStage'), body = $('#kbBody'), hint = $('.kb-hint');
  body.style.zoom = '1';
  const room = stage.clientHeight - hint.offsetHeight - 16;
  const box = body.getBoundingClientRect();   // the tilted, on-screen size
  if (box.height <= 0 || room <= 0) return;
  const z = Math.min(1, room / box.height, (stage.clientWidth - 16) / box.width);
  body.style.zoom = z.toFixed(3);
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
  const end = () => {
    dragging = false;
    stage.classList.remove('dragging');
    fitKeyboard();  // a flatter board draws taller and may no longer fit
  };
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
    // place under the anchor character -- .ch only, since the caret is a
    // sibling span and would shift every index past it by one
    const spans = compEl.querySelectorAll('.ch');
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

  // A drill fills more lines than the window has, so follow the caret the
  // way an editor would.
  const body = $('#npBody');
  body.scrollTop = body.scrollHeight;
}

/* ---------- tutorial player ---------- */

const player = {
  // `pending` is the step being animated; it runs ahead of `si`, which only
  // catches up once the keys have finished playing.
  ti: 0, si: -1, pending: -1, states: [], playing: false, gen: 0,

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
    this.pending = -1;
    this.playing = false;
    clearLit();
    renderScreen(EMPTY);
    setActiveNavItem($(`#nav .nav-item[data-lesson="${i}"]`));
    $('#capStep').textContent = t.title;
    $('#capText').innerHTML = '';
    this.updateBtn();
    // Show the first step at once -- a blank screen reads as broken -- but
    // go no further until the reader asks for it.
    const gen = this.gen;
    this.runStep(0, true, gen).then(ok => { if (ok) this.armNext(); });
  },

  async stepTo(k, animate, gen) {
    const t = TUTORIALS[this.ti];
    if (k < 0 || k >= t.steps.length) return;
    const step = t.steps[k];
    this.pending = k;
    clearLit();
    if (animate) {
      for (const key of step.keys) {
        if (gen !== this.gen) return;
        pressKey(key);
        litKey(key);
        await sleep(450);
      }
    } else {
      for (const key of step.keys) litKey(key);
    }
    if (gen !== undefined && gen !== this.gen) return;
    this.si = k;
    renderScreen(this.states[k]);
    $('#capStep').textContent = `${t.title}　·　步驟 ${k + 1} / ${t.steps.length}`;
    $('#capText').innerHTML = step.cap;
    if (k === t.steps.length - 1) progress.mark(t.id);
  },

  // How long the caption needs to be read. Both modes run this same clock
  // and differ only in what happens when it runs out: autoplay moves on,
  // manual lights the button that would have moved on.
  dwell(step) { return Math.min(8000, 1600 + step.cap.length * 55); },

  // One step, start to finish: play the keys, show the caption, then hold
  // for the reading time. False means something interrupted it.
  async runStep(k, animate, gen) {
    this.disarmNext();
    await this.stepTo(k, animate, gen);
    if (gen !== this.gen) return false;
    await sleep(this.dwell(TUTORIALS[this.ti].steps[k]));
    return gen === this.gen;
  },

  // The reading time is up and nothing is going to happen by itself, so the
  // button that carries on starts asking to be pressed.
  armNext() {
    const t = TUTORIALS[this.ti];
    const last = this.si >= t.steps.length - 1;
    const more = !last || this.ti < TUTORIALS.length - 1;
    const btn = $('#btnNext');
    btn.classList.toggle('blink', more);
    btn.title = last ? '下一課' : '下一步';
  },

  disarmNext() { $('#btnNext').classList.remove('blink'); },

  async play() {
    const t = TUTORIALS[this.ti];
    if (this.si >= t.steps.length - 1) { this.load(this.ti); return; }
    const gen = ++this.gen;
    this.playing = true;
    this.updateBtn();
    while (this.si < t.steps.length - 1) {
      if (!await this.runStep(this.si + 1, true, gen)) return;
    }
    this.playing = false;
    this.updateBtn();
    this.armNext();
  },

  pause() { this.gen++; this.playing = false; this.updateBtn(); this.disarmNext(); },

  // Pausing by hand is not "the reading time ran out", but the reader has
  // just said they want the controls, so show them straight away.
  toggle() {
    if (this.playing) { this.pause(); this.armNext(); } else { this.play(); }
  },

  async next() {
    const t = TUTORIALS[this.ti];
    // Clicked again while the keys are still playing: that means "skip the
    // animation", not "start the step over". Aborting the in-flight step
    // and re-running it from the top would sit there making no progress
    // for anyone who clicks faster than 450ms a key.
    if (this.pending > this.si) {
      const k = this.pending;
      this.pause();
      await this.stepTo(k, false, this.gen);
      this.armNext();
      return;
    }
    this.pause();
    // Past the last step the sensible next move is the next lesson.
    if (this.si >= t.steps.length - 1) {
      if (this.ti < TUTORIALS.length - 1) this.load(this.ti + 1);
      return;
    }
    const gen = this.gen;
    if (await this.runStep(this.si + 1, true, gen)) this.armNext();
  },

  async prev() {
    this.pause();
    const gen = this.gen;
    if (await this.runStep(Math.max(0, this.si - 1), false, gen)) this.armNext();
  },

  restart() { this.load(this.ti); },

  updateBtn() { $('#btnPlay').textContent = this.playing ? '⏸' : '▶'; }
};

/* ---------- progress ---------- */

// Which lessons the reader has reached the end of. Keyed by lesson id, not
// by position, so reordering the curriculum later does not scramble it.
const progress = {
  KEY: 'tutorialDone',
  done: new Set(),

  load() {
    try {
      this.done = new Set(JSON.parse(localStorage.getItem(this.KEY) || '[]'));
    } catch { this.done = new Set(); }
  },

  save() {
    try { localStorage.setItem(this.KEY, JSON.stringify([...this.done])); } catch {}
  },

  mark(id) {
    if (this.done.has(id)) return;
    this.done.add(id);
    this.save();
    this.render();
  },

  reset() { this.done.clear(); this.save(); this.render(); },

  render() {
    const total = TUTORIALS.length;
    const n = TUTORIALS.filter(t => this.done.has(t.id)).length;
    $('#pgCount').textContent = `${n} / ${total}`;
    $('#pgFill').style.width = total ? `${(n / total) * 100}%` : '0';
    $('#progress').classList.toggle('all-done', total > 0 && n === total);
    document.querySelectorAll('#nav .nav-item[data-lesson]').forEach(b => {
      const i = +b.dataset.lesson;
      const hit = this.done.has(TUTORIALS[i].id);
      b.classList.toggle('done', hit);
      b.querySelector('.no').textContent = hit ? '✓' : String(i + 1);
    });
  }
};

/* ---------- sound ---------- */

// The wrong-key chime. Synthesised rather than served as an audio file: the
// site is plain static assets on GitHub Pages and this keeps it that way.
// The context is built on the first chime, by which point the learner has
// pressed a key -- the gesture browsers demand before a page may make noise.

// Shaped after the Windows 11 default beep, which is what the Microsoft IME
// plays when it rejects a key. Measured off Windows Background.wav: a soft
// F3 + C4 chime, [frequency, level, ring] per partial. The real one takes
// 1.3 s to fade -- far too long to sit between two keystrokes -- so this
// keeps its shape and cuts the tail.
const CHIME = [
  [174.6, 0.050, 0.42],  // F3, the fundamental
  [261.6, 0.034, 0.45],  // C4, a fifth above it
  [349.2, 0.013, 0.18],  // F4
  [523.3, 0.030, 0.40]   // C5 -- the partial that makes it read as bright
];

const store = {
  get(key, fallback) {
    try { return localStorage.getItem(key) ?? fallback; } catch (e) { return fallback; }
  },
  set(key, value) {
    try { localStorage.setItem(key, value); } catch (e) { /* private mode */ }
  }
};

const sound = {
  on: store.get('drillSound', 'on') !== 'off',
  ctx: null,

  toggle() {
    this.on = !this.on;
    store.set('drillSound', this.on ? 'on' : 'off');
    this.updateBtn();
    if (this.on) this.ding();  // so the learner hears what they turned on
  },

  updateBtn() {
    const b = $('#btnDrillSound');
    b.textContent = this.on ? '🔊' : '🔇';
    b.title = this.on ? '按錯有提示聲（點一下關掉）' : '提示聲已關閉（點一下打開）';
    b.classList.toggle('off', !this.on);
  },

  ding() {
    if (!this.on) return;
    const Ctx = window.AudioContext || window.webkitAudioContext;
    if (!Ctx) return;
    if (!this.ctx) this.ctx = new Ctx();
    if (this.ctx.state === 'suspended') this.ctx.resume();

    const t = this.ctx.currentTime;
    for (const [freq, level, ring] of CHIME) {
      const osc = this.ctx.createOscillator();
      const gain = this.ctx.createGain();
      osc.type = 'sine';
      osc.frequency.value = freq;
      // Ramps, not steps -- a square edge on the gain clicks audibly. The
      // attack is quick where the original swells over 60 ms: at typing
      // speed the answer has to land on the key, not after it.
      gain.gain.setValueAtTime(0.0001, t);
      gain.gain.exponentialRampToValueAtTime(level, t + 0.02);
      gain.gain.exponentialRampToValueAtTime(0.0001, t + ring);
      osc.connect(gain).connect(this.ctx.destination);
      osc.start(t);
      osc.stop(t + ring + 0.02);
    }
  }
};

/* ---------- typing drill ---------- */

// Plays a lesson from DRILLS: the article on top, the simulated IME screen
// in the notepad, and the next expected key lit up on the keyboard. A wrong
// key never moves the drill on and is never marked on screen; the only
// answer it gets is a short chime (2026-08-10).

const CP = s => Array.from(s);

// A friendly name for the key being asked for.
const KEY_LABEL = { Space: '空白', Enter: 'Enter' };
// Only 2, 3 and 4 ever appear: no digit at all already means tone 1 or
// neutral, so the drill never asks for those two.
const TONE_NOTE = {
  '2': '二聲', '3': '三聲', '4': '四聲',
  '9': '二聲（右手）', '8': '三聲（右手）', '7': '四聲（右手）'
};

const drill = {
  di: -1, si: 0, chars: [],
  // Indices of the characters the learner fumbled: a wrong key is charged
  // to whichever character was being typed at the time. Reset per attempt.
  slips: new Set(),

  get lesson() { return DRILLS[this.di]; },

  // How many characters of the lesson are finished -- the article panel
  // follows the keystrokes, so this is what a wrong key is charged to.
  get done() { return this.si > 0 ? this.lesson.steps[this.si - 1].d : 0; },

  load(i) {
    this.di = i;
    this.si = 0;
    this.slips = new Set();
    this.chars = CP(DRILLS[i].text);
    // Whatever button started the drill keeps keyboard focus, and Space or
    // Enter would then press it again instead of reaching the drill.
    if (document.activeElement) document.activeElement.blur();
    player.pause();
    clearLit();
    $('#captionBar').hidden = true;
    $('#drillBar').hidden = false;
    // The drill needs its room lower down: the notepad is only there to
    // show what the IME is doing, while the article and the keyboard are
    // what the learner actually reads.
    document.body.classList.add('drilling');
    $('#drillTitle').textContent = DRILLS[i].title;
    $('#drillIntro').textContent = DRILLS[i].intro;
    this.render();
    fitKeyboard();  // the drill bar is taller than the caption bar
  },

  leave() {
    this.di = -1;
    clearHints();
    $('#drillBar').hidden = true;
    $('#captionBar').hidden = false;
    document.body.classList.remove('drilling');
    fitKeyboard();
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

    const done = this.done;
    const text = $('#drillText');
    text.innerHTML = '';
    this.chars.forEach((ch, i) => {
      const span = document.createElement('span');
      // A character typed cleanly turns green; one that took a wrong key on
      // the way stays marked, so the mistakes are still visible at the end.
      if (i < done) span.className = this.slips.has(i) ? 'done slip' : 'done';
      else if (i === done) span.className = 'now';
      // The line break is a character to type like any other (Enter commits,
      // Enter breaks), so it gets a mark of its own rather than vanishing.
      if (ch === '\n') {
        span.classList.add('nl');
        span.textContent = '↵';
        text.appendChild(span);
        text.appendChild(document.createElement('br'));
        return;
      }
      span.textContent = ch;
      text.appendChild(span);
    });
    const now = text.querySelector('.now');
    if (now) now.scrollIntoView({ block: 'nearest' });

    $('#drillFill').style.width = (100 * this.si / steps.length) + '%';
    $('#drillCount').textContent = `${this.si} / ${steps.length}`;

    const hint = $('#drillNext');
    if (this.si >= steps.length) {
      clearHints();
      const slips = this.slips.size;
      hint.innerHTML = slips === 0
        ? '<span class="cheer">完成了，全對！</span>　按 ↻ 再練一次，或從左邊挑下一課。'
        : `<span class="cheer">完成了！</span>　其中 <span class="slip-count">${slips}</span>` +
          ` 個字打錯過（共 ${this.chars.length} 字），歡迎按 ↻ 再挑戰一次。`;
      return;
    }
    const key = steps[this.si].k;
    showHint(key);
    const label = KEY_LABEL[key] || key.toUpperCase();
    // Every key in a drill is a letter, a tone digit, Space, Enter or
    // punctuation -- the lessons are chosen so nothing ever needs
    // correcting, so there are no cursor or candidate-menu keys to explain.
    let note = '';
    if (TONE_NOTE[key] && this.si > 0 && /^[a-z;]$/.test(steps[this.si - 1].k)) {
      note = `　（${TONE_NOTE[key]}）`;
    } else if (key === 'Space') {
      note = '　（單鍵音節要用空白或聲調收尾）';
    } else if (key === 'Enter') {
      // Two in a row at a full stop: the first commits, the second is the
      // line break. The step's own screen says which one this is.
      note = steps[this.si].t.endsWith('\n') ? '　（換行）' : '　（整段上屏）';
    } else if (key === ',') {
      note = '　（逗號，同一段繼續打）';
    }
    hint.innerHTML = `下一鍵：<kbd>${label}</kbd>${note}`;
  },

  // Whether a key that was not the one wanted counts as a typing mistake.
  // Modifiers, Tab, the arrows and the function keys are not part of the
  // drill at all, so they stay silent; anything the learner could have
  // meant as input -- a letter, a digit, punctuation, Space, Enter or
  // Backspace -- gets the chime.
  isMistake(event) {
    if (this.di < 0 || this.si >= this.lesson.steps.length) return false;
    // A held-down key is one mistake, not thirty chimes a second.
    if (event.repeat) return false;
    const k = event.key;
    return k === ' ' || k === 'Enter' || k === 'Backspace' || CP(k).length === 1;
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

  // A collapsible group. The drills are a long list of exercises rather
  // than reading material, so they start folded away.
  const group = (label, collapsed) => {
    const head = document.createElement('button');
    head.className = 'nav-group' + (collapsed ? ' collapsed' : '');
    head.innerHTML = `<span class="chev">▾</span><span class="gl">${label}</span>` +
                     `<span class="cnt"></span>`;
    const list = document.createElement('div');
    list.className = 'nav-list' + (collapsed ? ' collapsed' : '');
    head.addEventListener('click', () => {
      const now = !list.classList.contains('collapsed');
      list.classList.toggle('collapsed', now);
      head.classList.toggle('collapsed', now);
    });
    nav.appendChild(head);
    nav.appendChild(list);
    return { head, list };
  };

  // The lessons run in stages, and a stage only makes sense once the one
  // before it is done, so each gets its own open group in reading order.
  const stages = [];
  TUTORIALS.forEach((t, i) => {
    let stage = stages.find(s => s.name === t.stage);
    if (!stage) {
      stage = { name: t.stage, items: [] };
      stages.push(stage);
    }
    stage.items.push({ lesson: t, index: i });
  });

  stages.forEach((stage, si) => {
    const g = group(`${si + 1}　${stage.name}`, false);
    stage.items.forEach(({ lesson, index }) => {
      const b = document.createElement('button');
      b.className = 'nav-item';
      b.dataset.lesson = index;
      b.innerHTML = `<span class="no">${index + 1}</span>${lesson.title}`;
      b.addEventListener('click', () => { drill.leave(); player.load(index); });
      g.list.appendChild(b);
    });
    g.head.querySelector('.cnt').textContent = stage.items.length;
  });

  const drills = group('看打練習', true);
  DRILLS.forEach((d, i) => {
    const b = document.createElement('button');
    b.className = 'nav-item';
    b.innerHTML = `<span class="no">⌨</span>${d.title}`;
    b.addEventListener('click', () => {
      setActiveNavItem(b);
      drill.load(i);
    });
    drills.list.appendChild(b);
  });
  drills.head.querySelector('.cnt').textContent = DRILLS.length;
}

function setActiveNavItem(button) {
  document.querySelectorAll('#nav .nav-item').forEach(b =>
    b.classList.toggle('active', b === button));
}

$('#pgReset').addEventListener('click', e => { progress.reset(); e.currentTarget.blur(); });

$('#btnPrev').addEventListener('click', () => player.prev());
$('#btnPlay').addEventListener('click', () => player.toggle());
$('#btnNext').addEventListener('click', () => player.next());
$('#btnRestart').addEventListener('click', () => player.restart());
$('#btnDrillRestart').addEventListener('click', () => drill.restart());
$('#btnDrillSound').addEventListener('click', e => {
  sound.toggle();
  e.currentTarget.blur();  // or Space would toggle it again
});

// The drill reads the real keyboard, so it has to stop the browser acting
// on Space, Enter and the like -- but only for the key it actually wanted.
window.addEventListener('keydown', e => {
  if (e.ctrlKey || e.altKey || e.metaKey) return;
  if (drill.handle(e)) {
    e.preventDefault();
    return;
  }
  if (drill.di < 0) return;
  if (drill.isMistake(e)) {
    sound.ding();
    drill.slips.add(drill.done);  // charged to the character being typed
  }
  if (e.key === ' ' || e.key === 'Enter' || e.key === 'Backspace') {
    // Even when it is the wrong key, these must not scroll the page, go
    // back a page, or activate whatever happens to be focused.
    e.preventDefault();
  }
});

buildKeyboard();
applyRot();
setupDrag();
progress.load();
buildNav();
progress.render();
sound.updateBtn();
fitKeyboard();
window.addEventListener('resize', fitKeyboard);
player.load(0);
