#!/usr/bin/env node
/*
 * DOM-level keyboard test. Runs the page the DEVICE SERVES -- decompressed from
 * lib/CardputerMirror/WebAssets.h -- in jsdom, and asserts the keyboard the
 * browser actually builds.
 *
 * WHY THE SERVED PAGE, NOT web/index.html:
 *   web/index.html contains the literal placeholder /*__KEYMAP__* / and no
 *   keymap. Opened directly it renders a "keymap missing" notice into #kb by
 *   design. Testing the template therefore tests a page that no browser ever
 *   sees; every count assertion reports 0. gen_web_assets.py inlines web/keymap.js
 *   at build time, so the header is the only artefact that matches reality.
 *
 * HARNESS NOTES (both cost a debugging cycle):
 *   - The page's first statements are getContext('2d',{willReadFrequently:true})
 *     followed by an immediate property set. jsdom has no canvas backend, so
 *     getContext returns null and the WHOLE top-level script dies -- keyboard
 *     included. The stub must accept the options argument and return an object.
 *   - Element-counting assertions pass VACUOUSLY on an empty DOM ("no bad caps"
 *     is true when there are no caps). The build gate below refuses to report
 *     any downstream result unless the keyboard actually built.
 *
 * Requires jsdom. Skips with exit 0 and a clear notice if it is not installed,
 * so it never breaks a checkout that has not run npm install.
 */
'use strict';
const fs = require('fs'), path = require('path'), zlib = require('zlib');

let JSDOM, VirtualConsole;
try { ({ JSDOM, VirtualConsole } = require('jsdom')); }
catch (e) {
  try { ({ JSDOM, VirtualConsole } = require('/tmp/kbtest/node_modules/jsdom')); }
  catch (e2) {
    // The /tmp fallback above is a convenience for the machine this test was
    // written on and WILL disappear on reboot. The durable fix is a local
    // install in the repo:  npm install --no-save jsdom
    // Skipping (exit 0) rather than failing is deliberate: a checkout without
    // jsdom should still run the other three suites, and a missing test
    // dependency is not a defect in the firmware.
    console.log('SKIP: jsdom not installed. Run: npm install --no-save jsdom');
    process.exit(0);
  }
}

const root = path.resolve(__dirname, '..');
const hdr = fs.readFileSync(path.join(root, 'lib/CardputerMirror/WebAssets.h'), 'utf8');
const bytes = Buffer.from((hdr.match(/0x[0-9a-fA-F]{2}/g) || []).map(h => parseInt(h, 16)));
const declared = Number((hdr.match(/kIndexHtmlGzLen\s*=\s*(\d+)/) || [])[1]);
if (bytes.length !== declared) {
  console.log(`FAIL: WebAssets.h length mismatch (declared ${declared}, parsed ${bytes.length})`);
  process.exit(1);
}
const html = zlib.gunzipSync(bytes).toString('utf8');

const vc = new VirtualConsole();
const errs = [];
vc.on('jsdomError', e => errs.push(String(e.detail || e).split('\n')[0]));

function stubCtx() {
  const noop = () => {};
  const mk = (w, h) => ({ data: new Uint8ClampedArray(4 * Math.max(1, w | 0) * Math.max(1, h | 0)) });
  return { imageSmoothingEnabled: false, fillStyle: '#000', strokeStyle: '#000', font: '',
    drawImage: noop, clearRect: noop, fillRect: noop, strokeRect: noop, putImageData: noop,
    createImageData: mk, getImageData: (x, y, w, h) => mk(w, h),
    save: noop, restore: noop, translate: noop, scale: noop, setTransform: noop, rotate: noop,
    beginPath: noop, closePath: noop, moveTo: noop, lineTo: noop, stroke: noop, fill: noop,
    arc: noop, rect: noop, clip: noop, fillText: noop, measureText: () => ({ width: 0 }) };
}

const dom = new JSDOM(html, { runScripts: 'dangerously', pretendToBeVisual: true, virtualConsole: vc,
  beforeParse(w) {
    w.HTMLCanvasElement.prototype.getContext = function () { return stubCtx(); };
    w.WebSocket = class { constructor() { this.readyState = 0; } send() {} close() {} addEventListener() {} };
    w.requestAnimationFrame = cb => setTimeout(() => cb(0), 0);
  }});

const doc = dom.window.document;
let fail = 0;
const ok = (c, msg, extra = '') => {
  if (!c) { fail++; console.log('  FAIL  ' + msg + (extra ? '  [' + extra + ']' : '')); }
  else console.log('  ok    ' + msg);
};

setTimeout(() => {
  if (errs.length) { console.log('  script errors:'); errs.slice(0, 3).forEach(e => console.log('    ' + e)); }

  const kb = doc.getElementById('kb');
  ok(!!kb, '#kb exists');
  const keys = kb ? kb.querySelectorAll('.key') : [];

  // GATE -- see harness notes.
  if (!(keys.length > 0)) {
    console.log('  FAIL  top-level script ran and built the keyboard  [0 keys]');
    console.log('\nFAILURES: ' + (fail + 1) + ' (build never ran; downstream assertions skipped)');
    process.exit(1);
  }
  ok(true, 'top-level script ran and built the keyboard');

  const rows = kb.querySelectorAll('.krow');
  ok(rows.length === 4, 'four rows', String(rows.length));
  ok(keys.length === 56, '56 key cells', String(keys.length));
  const per = [...rows].map(r => r.querySelectorAll('.key').length);
  ok(per.every(n => n === 14), 'every row has 14 keys', per.join(','));

  // ADR 0022: a key is TWO parts -- printed case legend + separate rubber dome.
  ok([...keys].every(k => k.querySelector('.lgd')), 'every key has a legend bar');
  ok([...keys].every(k => k.querySelector('.dome')), 'every key has a dome');

  // The redundant-secondary defect: only a render caught it, now asserted.
  const secs = [...keys].filter(k => k.querySelector('.sec')).length;
  ok(secs === 21, 'exactly 21 caps print a secondary glyph', String(secs));
  const bogus = [...keys].filter(k => {
    const p = k.querySelector('.pri'), q = k.querySelector('.sec');
    return p && q && p.textContent.toLowerCase() === q.textContent.toLowerCase();
  });
  ok(bogus.length === 0, 'no cap prints a case-variant secondary',
     bogus.map(k => k.querySelector('.pri').textContent).join(''));

  // Space is ONE unit and its mark is DRAWN (U+2334 is missing from many fonts).
  const spaceKey = [...keys].find(k => k.dataset.rc === '3,13');
  ok(!!(spaceKey && spaceKey.querySelector('.spc')), 'space cap uses the drawn CSS mark');
  ok(!doc.body.textContent.includes('\u2334'), 'U+2334 absent from rendered text');

  const qk = [...keys].find(k => k.dataset.lo === 'q');
  ok(!!qk && qk.querySelector('.pri').textContent === 'Q',
     'letter legend prints uppercase (q -> Q)', qk ? qk.querySelector('.pri').textContent : 'no q key');

  // ADR 0022: the board has NO wide key.
  ok([...keys].every(k => !/\bwide\b|span/.test(k.className)), 'no cap claims extra width');

  // Top edge. BtnRst is inert BY BEHAVIOUR -- it explains itself and
  // deliberately carries no disabled attribute, because a control that
  // silently does nothing would misrepresent the hardware.
  const g0 = doc.getElementById('btng0'), rst = doc.getElementById('btnrst');
  ok(!!g0 && typeof g0.onpointerdown === 'function', 'BtnG0 wired to send a press');
  ok(!!rst && typeof rst.onclick === 'function', 'BtnRst wired to explain itself');
  const st = doc.getElementById('g0state');
  if (rst && st) {
    const before = st.textContent;
    rst.onclick();
    ok(st.textContent !== before && /physical/i.test(st.textContent),
       'BtnRst click explains rather than acting', st.textContent);
  }

  console.log(fail === 0 ? '\nPASS: served page builds the measured ADV keyboard'
                         : '\nFAILURES: ' + fail);
  process.exit(fail === 0 ? 0 : 1);
}, 300);
