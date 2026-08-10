// Audits web/tutorials.js against the real engine.
//
//   node scripts/check-tutorials.mjs [--repl build/Release/repl.exe]
//                                    [--data out/data.txt]
//
// The lessons are hand-written (unlike the drills, which are generated), so
// nothing stops them drifting away from the IME they claim to teach. Two
// checks close that gap:
//
//   A. screens -- replay each lesson's keystrokes through the real
//      mspy::Composer and compare what the engine shows with what the
//      lesson says it shows: committed text, composition text, which part
//      is still bopomofo, the selection anchor, the caret, the candidate
//      list and its page counter.
//
//   B. shortest keys -- a lesson can be perfectly correct and still teach
//      the slow way round (的 as d+e+Space long after d+Space started
//      working). For every syllable a lesson types, compare the keys it
//      spends with the fewest keys that spell the same reading.
//
// A step may carry `alt: true` to opt out of B (it is deliberately showing
// a longer alternative), or `audit: false` to say the engine cannot model
// it at all -- the replay of that lesson stops there, and every step before
// it is still checked.

import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { argv, exit } from 'node:process';

const args = argv.slice(2);
const optionAfter = (name, fallback) => {
  const i = args.indexOf(name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : fallback;
};
const REPL = optionAfter('--repl', 'build/Release/repl.exe');
const DATA = optionAfter('--data', 'out/data.txt');

/* ------------------------------------------------------------ the lessons */

// tutorials.js is plain top-level `const` declarations with no imports, so
// evaluating it as a function body and asking for the bindings back is both
// exact and cheaper than parsing JavaScript object literals.
function loadTutorials(path) {
  const source = readFileSync(path, 'utf8');
  return Function(`${source}\nreturn TUTORIALS;`)();
}

/* --------------------------------------------------------------- the keys */

// repl.exe reads one character per keystroke and spells the keys that are
// not characters with stand-ins of its own.
const REPL_KEY = {
  Space: ' ',
  Enter: '\n',
  Backspace: '<',
  Tab: '<', // Tab is Backspace while composing (docs/spec.md §6)
  Esc: '!',
  ShiftL: '#',
  ShiftR: '#'
};

function replKey(id) {
  if (id in REPL_KEY) return REPL_KEY[id];
  if ([...id].length === 1) return id;
  return null; // a key the composer has no notion of
}

/* ------------------------------------------------------------ the engine */

function replay(keys) {
  const out = execFileSync(REPL, ['--data', DATA, '--keys', keys, '--json'], {
    encoding: 'utf8',
    maxBuffer: 64 * 1024 * 1024
  });
  return out
    .split('\n')
    .filter((line) => line.trim() !== '')
    .map((line) => JSON.parse(line));
}

function shortestKeys(readings) {
  if (readings.length === 0) return new Map();
  const out = execFileSync(REPL, ['--data', DATA, '--shortest', ...readings], {
    encoding: 'utf8',
    maxBuffer: 8 * 1024 * 1024
  });
  const map = new Map();
  for (const line of out.split('\n')) {
    const [reading, keys] = line.trim().split(' ');
    if (reading && keys) map.set(reading, keys === '-' ? null : keys);
  }
  return map;
}

/* -------------------------------------------------------- the comparison */

const EMPTY = { text: '', comp: [], anchor: null, cur: null, menu: null, mode: 'zh' };

// What the lesson claims the screen looks like, reduced to the facts the
// engine can confirm. The composition is compared as text rather than as
// spans: a lesson groups "OK" into one span and the engine has no opinion
// about that, but neither may disagree about the characters or about which
// of them are still bopomofo.
function lessonView(screen) {
  const comp = screen.comp || [];
  const text = (entry) => entry[0];
  const at = (i) => (i != null && comp[i] ? comp[i][0] : '');
  const anchorIndex = screen.menu ? screen.menu.anchor : screen.anchor;
  return {
    committed: screen.text || '',
    composition: comp.map(text).join(''),
    pending: comp.filter((e) => e[1] === 'p').map(text).join(''),
    anchor: at(anchorIndex),
    caretPrefix:
      screen.cur == null
        ? comp.map(text).join('')
        : comp.slice(0, screen.cur).map(text).join(''),
    menu: screen.menu
      ? { page: screen.menu.page, items: screen.menu.items.join(' ') }
      : null
  };
}

function engineView(record, committed) {
  const { before, unconfirmed, highlighted, after } = record;
  return {
    committed,
    composition: before + unconfirmed + highlighted + after,
    pending: unconfirmed,
    anchor: highlighted,
    caretPrefix: before + unconfirmed,
    menu: record.menu
      ? { page: record.menu.page, items: record.menu.items.join(' ') }
      : null
  };
}

function describe(value) {
  if (value === null) return '(無選單)';
  if (typeof value === 'object') return `${value.page} [${value.items}]`;
  return value === '' ? '(空)' : value;
}

const FIELD_LABEL = {
  committed: '已上屏',
  composition: '組字串',
  pending: '未定案注音',
  anchor: '反白字',
  caretPrefix: '游標左邊',
  menu: '候選選單'
};

function diff(lesson, engine) {
  const problems = [];
  for (const field of Object.keys(FIELD_LABEL)) {
    const a = lesson[field];
    const b = engine[field];
    const same =
      a === null && b === null
        ? true
        : a === null || b === null
        ? false
        : typeof a === 'object'
        ? a.page === b.page && a.items === b.items
        : a === b;
    if (!same) {
      problems.push(
        `${FIELD_LABEL[field]}：課文 ${describe(a)}　實際 ${describe(b)}`
      );
    }
  }
  return problems;
}

/* ------------------------------------------------- check B: shortest keys */

// Every run of keys that builds one syllable, taken straight off the
// keystroke stream: while a syllable is being typed the engine shows its
// bopomofo in `unconfirmed`, and the key that empties it is the one that
// settled it.
function syllableRuns(records, keyIds) {
  const runs = [];
  let keys = [];
  let reading = '';
  let english = false;
  records.forEach((record, i) => {
    const id = keyIds[i];
    if (id === 'ShiftL' || id === 'ShiftR') {
      english = !english;
      keys = [];
      reading = '';
      return;
    }
    if (english) return;
    if (record.unconfirmed !== '') {
      // A syllable with no tone digit is settled by the next syllable's
      // first key, so the bopomofo never passes through empty -- it just
      // stops being an extension of what was there (ㄢ then ㄑ).
      if (reading !== '' && !record.unconfirmed.startsWith(reading)) {
        runs.push({ keys: [...keys], reading, stepEnd: i });
        keys = [];
      }
      keys.push(id);
      reading = record.unconfirmed;
      return;
    }
    if (keys.length > 0) {
      runs.push({ keys: [...keys], reading, stepEnd: i });
      keys = [];
      reading = '';
    }
  });
  return runs;
}

/* ------------------------------------------------------------------ main */

const tutorials = loadTutorials('web/tutorials.js');
const findings = [];
let stepsChecked = 0;
let lessonsTruncated = 0;

// One pass to collect readings, so the shortest-key oracle is asked once.
const plans = [];
for (const lesson of tutorials) {
  const keyIds = [];
  const bounds = [];
  let stopped = -1;
  lesson.steps.forEach((step, si) => {
    if (step.audit === false && stopped < 0) stopped = si;
    if (stopped >= 0) return;
    for (const id of step.keys || []) {
      const k = replKey(id);
      if (k === null) {
        findings.push(`${lesson.id} 步驟 ${si + 1}：無法對應的按鍵 ${id}`);
        continue;
      }
      keyIds.push(id);
    }
    bounds.push({ si, index: keyIds.length - 1 });
  });
  const keys = keyIds.map(replKey).join('');
  const records = keyIds.length > 0 ? replay(keys) : [];
  plans.push({ lesson, keyIds, bounds, records, stopped });
  if (stopped >= 0) lessonsTruncated++;
}

const allReadings = new Set();
for (const plan of plans) {
  for (const run of syllableRuns(plan.records, plan.keyIds)) {
    if (run.keys.length > 1) allReadings.add(run.reading);
  }
}
const shortest = shortestKeys([...allReadings]);

for (const plan of plans) {
  const { lesson, bounds, records } = plan;

  // Check A -- the screens.
  let state = { ...EMPTY };
  let committed = '';
  let cursor = -1;
  for (const { si, index } of bounds) {
    const step = lesson.steps[si];
    state = { ...state, ...(step.screen || {}) };
    while (cursor < index) {
      cursor++;
      committed += records[cursor].commit;
    }
    const record = cursor >= 0 ? records[cursor] : null;
    const engine = record
      ? engineView(record, committed)
      : engineView(
          { before: '', unconfirmed: '', highlighted: '', after: '', menu: null },
          ''
        );
    const problems = diff(lessonView(state), engine);
    if (problems.length > 0) {
      findings.push(
        `${lesson.id} 步驟 ${si + 1}（按鍵 ${
          (step.keys || []).join(' ') || '無'
        }）\n    ` + problems.join('\n    ')
      );
    }
    // The 中/英 badge is the lesson's own claim; the engine's truth is how
    // many bare Shift taps have been replayed.
    const taps = plan.keyIds
      .slice(0, cursor + 1)
      .filter((id) => id === 'ShiftL' || id === 'ShiftR').length;
    const engineMode = taps % 2 === 0 ? 'zh' : 'en';
    if ((state.mode || 'zh') !== engineMode) {
      findings.push(
        `${lesson.id} 步驟 ${si + 1}：中英模式　課文 ${
          state.mode || 'zh'
        }　實際 ${engineMode}`
      );
    }
    stepsChecked++;
  }

  // Check B -- the fewest keys.
  const stepOf = (index) => {
    for (const { si, index: end } of bounds) if (index <= end) return si;
    return bounds.length > 0 ? bounds[bounds.length - 1].si : 0;
  };
  for (const run of syllableRuns(records, plan.keyIds)) {
    if (run.keys.length <= 1) continue;
    const best = shortest.get(run.reading);
    if (!best || best.length >= run.keys.length) continue;
    const si = stepOf(run.stepEnd);
    if (lesson.steps[si] && lesson.steps[si].alt === true) continue;
    findings.push(
      `${lesson.id} 步驟 ${si + 1}：${run.reading} 用了 ${
        run.keys.length
      } 鍵（${run.keys.join('')}），最省是 ${best.length} 鍵（${best}）`
    );
  }
}

console.log(
  `檢查 ${tutorials.length} 課、${stepsChecked} 個步驟` +
    (lessonsTruncated > 0 ? `（${lessonsTruncated} 課標了 audit:false 提早結束）` : '')
);

if (findings.length === 0) {
  console.log('教學課程與引擎一致，且每一步都是目前最省的打法。');
  exit(0);
}

console.log(`\n發現 ${findings.length} 處不一致：\n`);
for (const finding of findings) console.log(`  - ${finding}`);
console.log(
  '\n修法：改課文或改字幕讓它符合實際行為；刻意示範較長的打法就在該步驟加 alt: true，' +
    '引擎模擬不到的步驟加 audit: false。'
);
exit(2);
