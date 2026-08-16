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
// Scope the byte parse to the kIndexHtmlGz array specifically. The header now
// also carries kStampWebp (ADR 0033) and kAdvLabelWebp (ADR 0035), and a
// header-wide /0x../g match would concatenate all three arrays and fail the
// length check against kIndexHtmlGzLen.
const htmlArr = (hdr.match(/kIndexHtmlGz\[\][^{]*\{([\s\S]*?)\};/) || [])[1] || '';
const bytes = Buffer.from((htmlArr.match(/0x[0-9a-fA-F]{2}/g) || []).map(h => parseInt(h, 16)));
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
    // jsdom has no layout engine, so it has no concept of pointer type either
    // (ADR 0045's isTouch check). Fixed 'false': every assertion below is
    // exercising the desktop/physical-keyboard capture path, not the mobile
    // hidden-input relay, so a real (non-coarse) pointer is the correct answer
    // here, not just the only one jsdom can give.
    w.matchMedia = () => ({ matches: false });
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
    // The note is transient and shown by CLASS, not by standing text: the
    // request was buttons only, no other text on the case (ADR 0029).
    ok(st.classList.contains('show'), 'BtnRst note is revealed on press');
  }

  const styleText = Array.from(doc.querySelectorAll('style')).map(s=>s.textContent).join('\n');
  // Top-edge button SIDES, measured from the top-face photo (ADR 0030).
  // The top view is mirrored vs the front view, so BtnG0 is on the RIGHT of
  // the front face. Guarding the sides catches a re-mirroring regression.
  {
    const g0l = parseFloat((/#topedge #btng0\{left:([\d.]+)%/.exec(styleText)||[])[1]);
    const rl  = parseFloat((/#topedge #btnrst\{left:([\d.]+)%/.exec(styleText)||[])[1]);
    ok(g0l > 50, 'BtnG0 sits on the right of the FRONT face', g0l + '%');
    ok(rl < 50, 'BtnRst sits on the left of the FRONT face', rl + '%');
  }

  // Front-face detail (ADR 0030): mic and grille. The two CSS screws were
  // REMOVED at the user's request (ADR 0035) -- assert they stay gone, markup
  // and stylesheet both, so a later "restore the detail" pass has to be
  // deliberate rather than accidental.
  ok(doc.querySelectorAll('#dev .screw').length === 0,
     'no CSS screws on the case');
  ok(!/\.screw\{/.test(styleText) && !doc.getElementById('scr1') && !doc.getElementById('scr2'),
     'screw rule and both screw nodes are gone from the page');
  // The microphone is no longer a separate element: it is printed on the ADV
  // label and the label is now a photograph (ADR 0035), so a CSS #devmic drew
  // a SECOND mic 1% away from the photographed one. Measured centres were
  // 10.35%/25.99% (photo) against 10.46%/24.96% (CSS) -- close enough to read
  // as a smear rather than as two icons, which is how it survived review.
  ok(!doc.getElementById('devmic') && !/#devmic\{/.test(styleText),
     'no duplicate CSS microphone over the photographed one');
  ok(!!doc.getElementById('devgrille'), 'speaker grille present');

  // Buttons carry no visible label -- title/aria only.
  ok(!!g0 && g0.textContent.trim() === '' && !!g0.getAttribute('aria-label'),
     'BtnG0 is unlabelled but named for screen readers');
  ok(!!rst && rst.textContent.trim() === '' && !!rst.getAttribute('aria-label'),
     'BtnRst is unlabelled but named for screen readers');
  // Both must sit on the case, not in page flow.
  const te = doc.getElementById('topedge'), devEl = doc.getElementById('dev');
  ok(!!te && !!devEl && devEl.contains(te), 'top-edge buttons are on the case');
  ok(!!devEl && devEl.contains(st), 'press note is on the case');
  // No standing top-edge prose survived the move.
  ok(doc.querySelectorAll('#topedge .tlbl, #topedge .tinfo').length === 0,
     'no TOP EDGE label or pin-list text remains');

  // ---- Hamburger menu (ADR 0027) ----------------------------------------
  // These assert BEHAVIOUR, not markup: the failure mode of a menu is that it
  // opens and then cannot be dismissed, stranding itself over the keyboard.
  const menu = doc.getElementById('menu'), mbtn = doc.getElementById('menubtn');
  ok(!!menu && !!mbtn, 'menu and toggle exist');
  if (menu && mbtn) {
    ok(menu.hidden === true, 'menu starts closed');

    // Every control the user asked to move must actually be inside it.
    for (const id of ['full','swap','inv','shot','hwkb','sweep',
                      'fps','tps','kbs','st','tot','ksent','kcov'])
      ok(!!menu.querySelector('#' + id), 'moved into menu: #' + id);

    mbtn.onclick({ stopPropagation(){} });
    ok(menu.hidden === false, 'toggle opens the menu');
    ok(mbtn.getAttribute('aria-expanded') === 'true', 'aria-expanded tracks open');

    // Outside click closes. Dispatched on the document, as a real click bubbles.
    doc.dispatchEvent(new dom.window.Event('click', { bubbles: true }));
    ok(menu.hidden === true, 'outside click closes the menu');

    // A click INSIDE must not close it -- stopPropagation guards this.
    mbtn.onclick({ stopPropagation(){} });
    menu.onclick({ stopPropagation(){} });
    ok(menu.hidden === false, 'click inside the menu keeps it open');

    // Escape closes when capture is OFF.
    dom.window.dispatchEvent(new dom.window.KeyboardEvent('keydown', { key: 'Escape' }));
    ok(menu.hidden === true, 'Escape closes the menu');
  }

  // Capture badge: this mode swallows the user's real keystrokes, so its
  // indicator must live OUTSIDE the menu and survive the menu closing.
  const badge = doc.getElementById('capbadge'), hwkb = doc.getElementById('hwkb');
  ok(!!badge, 'capture badge exists');
  if (badge) ok(!menu.contains(badge), 'capture badge is outside the menu');
  if (badge && hwkb) {
    ok(!badge.classList.contains('on'), 'badge hidden while capture is off');
    hwkb.onclick();
    ok(badge.classList.contains('on'), 'badge shows when capture is on');
    // With capture ON, Escape belongs to the DEVICE, not the menu.
    mbtn.onclick({ stopPropagation(){} });
    dom.window.dispatchEvent(new dom.window.KeyboardEvent('keydown', { key: 'Escape' }));
    ok(menu.hidden === false, 'Escape is yielded to the device while capturing');
    doc.getElementById('capoff').onclick({ stopPropagation(){} });
    ok(!badge.classList.contains('on'), 'stop button clears capture');
  }

  // ---- Mockup + zoom (ADR 0028) -----------------------------------------
  const dev = doc.getElementById('dev'), scr = doc.getElementById('devscreen'),
        shell = doc.getElementById('shell'), zb = doc.getElementById('zoombtn');
  ok(!!dev && !!scr && !!shell && !!zb, 'mockup, glass, shell and zoom button exist');

  // The keyboard must live INSIDE the case, not beside it.
  const kbEl = doc.getElementById('kb');
  ok(!!dev && !!kbEl && dev.contains(kbEl), 'keyboard is inside the case');

  // THE invariant: exactly one canvas, always. Two live canvases would double
  // the per-frame blit and drift apart when one missed a tile.
  ok(doc.querySelectorAll('canvas').length === 1, 'exactly one canvas exists');

  if (dev && scr && shell && zb) {
    const cvEl = doc.getElementById('c');
    ok(scr.contains(cvEl), 'canvas starts inset in the glass');
    ok(shell.hidden === true, 'popped-out shell starts hidden');
    ok(doc.getElementById('zoom').disabled === true,
       'zoom select disabled while inset');

    zb.onclick();
    ok(shell.contains(cvEl), 'zoom moves the canvas out to the shell');
    ok(doc.querySelectorAll('canvas').length === 1, 'still exactly one canvas after popping out');
    ok(shell.hidden === false, 'shell visible when popped out');
    ok(scr.classList.contains('out'), 'glass marked as vacated');
    ok(doc.getElementById('zoom').disabled === false, 'zoom select active when popped out');

    zb.onclick();
    ok(scr.contains(cvEl), 'zoom again re-insets the canvas');
    ok(doc.querySelectorAll('canvas').length === 1, 'still exactly one canvas after re-inset');
    ok(!scr.classList.contains('out'), 'glass no longer marked vacated');
    // Same NODE throughout -- proves it moved rather than being recreated,
    // which would drop the 2d context and silently freeze the stream.
    ok(doc.getElementById('c') === cvEl, 'the canvas node was moved, not recreated');
  }

  // ---- Front-face detail: paint order and bands (ADR 0031) ----------------
  // The label is an opaque panel. When it was emitted LAST it covered the
  // grille, which was correctly positioned and
  // simply invisible -- a defect no geometry assertion could ever have caught,
  // because every element had the right coordinates. These assert the ORDER.
  {
    const dev = doc.getElementById('dev');
    const kids = Array.from(dev.children).map(n => n.id || n.className);
    const idx = n => kids.indexOf(n);
    ok(idx('devlabel') < idx('devgrille'), 'label paints before the grille');

    // Bands, from the product photo. The grille lives BELOW the label bottom
    // edge on bare plastic; putting it inside the label band is the old bug.
    const css  = styleText;
    const pick = (sel, prop) => {
      const b = css.slice(css.indexOf(sel));
      const m = b.slice(0, b.indexOf('}')).match(new RegExp(prop + ':([\\d.]+)%'));
      return m ? parseFloat(m[1]) : null;
    };
    const labTop = pick('#devlabel{', 'top'), labH = pick('#devlabel{', 'height');
    const labBot = labTop + labH;
    const grTop  = pick('#devgrille{', 'top');
    // 30.713%, re-measured off the border groove for the photo crop (ADR 0035).
    // The old 31.08% came from a threshold that had leaked into the plastic.
    ok(Math.abs(labBot - 30.713) < 0.5, 'label bottom edge at the measured 30.713%');
    ok(grTop > labBot, 'grille sits below the label bottom edge, not under it');
    ok(Math.abs(grTop - 31.81) < 0.3, 'grille top at the measured 31.81%');
    ok(pick('#devmic{', 'top') < labBot, 'mic sits within the label band');
    // Slots are black pills; orange here would mean the mic-colour assumption
    // crept back in. Photo reads (0,0,0) against (206,206,206) plastic.
    ok(!/#devgrille i\{[^}]*#e6531f/.test(css), 'grille slots are not orange');
    ok(/#devgrille i\{[^}]*background:#0a0a0a/.test(css), 'grille slots are black');
    ok(doc.getElementById('devgrille').children.length === 2, 'grille has two slots');
  }

  // ---- Hamburger contents, and the removed note (ADR 0027, ADR 0032) ------
  {
    const menu = doc.getElementById('menu');
    // Open/close behaviour is already covered above; this block asserts only
    // WHAT the menu contains, so it must not re-assert menu.hidden -- the
    // earlier capture test deliberately leaves the menu open.
    ok(!!doc.getElementById('menubtn'), 'hamburger button exists');
    for (const id of ['full','shot','swap','inv','hwkb','sweep'])
      ok(menu.querySelector('#' + id) !== null, `menu contains #${id}`);
    for (const id of ['fps','tps','kbs','tot','st','ksent','kcov'])
      ok(menu.querySelector('#' + id) !== null, `menu contains readout #${id}`);
    // The capture badge must stay OUTSIDE the menu: capture rewires the user's
    // physical keyboard, and that state has to survive the menu closing.
    ok(menu.querySelector('#capbadge') === null, 'capture badge lives outside the menu');
    ok(doc.getElementById('capbadge') !== null, 'capture badge still exists in the bar');
    // Removed at the user's request; the facts live in ADR 0019 / ADR 0001.
    ok(!/Clicking a key sends/.test(html), 'explanatory note removed');
    ok(doc.querySelector('.note') === null, 'no .note block remains');
    ok(!/\.note\{/.test(styleText), 'orphaned .note CSS rule removed');
  }

  // ---- keys are only counted when they actually go out (ADR 0037)
  // send() returns false when the socket is down; noteSent() used to count
  // unconditionally, so the page reported success for presses it dropped.
  // Asserted on SOURCE rather than behaviour: jsdom has no WebSocket peer, so
  // exercising the closed-socket path here would test the mock, not the page.
  ok(/const send\s*=\s*o\s*=>\s*\{[^}]*return false;[^}]*return true;\s*\}/s.test(html),
     'send() returns a boolean rather than short-circuiting to undefined');
  {
    const calls = html.match(/noteSent\([^)]*\)/g) || [];
    const invocations = calls.filter(s => !/^noteSent\(r,c,ok\)/.test(s));
    ok(invocations.length === 3, `all three noteSent call sites found (${invocations.length})`);
    ok(invocations.every(s => /send\(/.test(s)),
       'every noteSent() call is passed send()\'s return, not called blind');
  }
  ok(!/send\(\{t:'key'[^}]*\}\);\s*\n\s*noteSent/.test(html),
     'no call site sends then counts as two independent statements');
  ok(doc.getElementById('kdrop'), 'dropped-key counter is on the page');
  ok(/keysDropped\+\+/.test(html), 'drops are counted, not merely ignored');

  // ---- SD card feature removed entirely (ADR 0036)
  // The user asked for the SD function gone from everything: firmware manager,
  // menu screen, HTTP routes and this panel. Asserted NEGATIVELY, like the
  // screws and the duplicate mic before it -- a panel whose fetches 404 would
  // sit there showing an eternal "checking..." rather than fail visibly, and
  // dead CSS for a removed element is inert rather than wrong. Both are the
  // kind of leftover that survives review.
  for (const id of ['sd','sdstate','sdname','sdfs','sdcap','sdused','sdfree',
                    'sdspd','sdbar','sdfill','sddetail','sdref','sdmnt','sdumnt',
                    'sdfmt','sdau','fmt'])
    ok(!doc.getElementById(id), `no #${id} -- SD panel is gone`);
  ok(!/#sd\{|#sdbar\{|#sdfill\{|\.sdgrid\{|#sddetail\{/.test(styleText),
     'no orphaned SD stylesheet rules');
  ok(!/\/api\/sd\//.test(html), 'page makes no /api/sd requests');
  ok(!/sdRender|sdStatus|sdPost|fmtB/.test(html),
     'SD script helpers removed, including the fmtB formatter only it used');

  // ---- CARDPUTER ADV label photo (ADR 0035) -------------------------------
  {
    const lab = doc.getElementById('devlabel');
    const img = lab.querySelector('img');
    ok(!!img, 'ADV label is a photograph, superseding the CSS build of ADR 0034');
    ok(img.getAttribute('src') === '/advlabel.webp',
       'served from its own firmware route, not a base64 data: URI');
    // The CSS reconstruction is fully retired: its brackets, its measured
    // cap-height sizes, its width correction and its fill must all be gone.
    // Leaving any of them would style a panel that is now an <img>, so the
    // rules would be silently dead rather than wrong -- worse to debug later.
    for (const dead of ['#devlabel i{', '#devlabel .l1', '#devlabel .l2',
                        'scaleX(1.158)', 'padding-bottom:22.42%', 'background:#e8e8e8'])
      ok(!styleText.includes(dead), `retired CSS-label rule absent: ${dead}`);
    ok(lab.querySelectorAll('i').length === 0, 'no leftover CSS bracket elements');
    ok(!/CARDPUTER/.test(lab.textContent), 'no leftover CSS wordmark text');
    // overflow:hidden + object-fit:fill. The box IS the crop aspect, so fill
    // does not distort; cover would crop the brackets the crop was cut to keep.
    ok(/#devlabel\{[^}]*overflow:hidden/.test(styleText), 'label clips to its rounded box');
    ok(/#devlabel img\{[^}]*object-fit:fill/.test(styleText), 'photo fills the measured box');
    // Box measured off the panel's dark border groove, not the pale fill: the
    // fill is only 17 levels above the surrounding plastic and the plastic has
    // a gradient, so a global threshold leaks. Do NOT "round these off".
    const g = (p) => parseFloat((styleText.match(
      new RegExp('#devlabel\\{[^}]*' + p + ':([\\d.]+)%')) || [])[1]);
    ok(Math.abs(g('left')  -  1.880) < 0.03, `label left at measured 1.880% (got ${g('left')})`);
    ok(Math.abs(g('top')   -  2.559) < 0.03, `label top at measured 2.559% (got ${g('top')})`);
    ok(Math.abs(g('width') - 21.034) < 0.03, `label width measured 21.034% (got ${g('width')})`);
    ok(Math.abs(g('height')- 28.154) < 0.03, `label height measured 28.154% (got ${g('height')})`);
    // The crop's aspect and the box's aspect must agree, or object-fit:fill
    // distorts the wordmark. Case aspect is parsed FROM the stylesheet, not
    // duplicated as a constant. The Stamp block below parses its own copy --
    // that one is `const` inside its own block and is not visible here.
    const caseAR = parseFloat((styleText.match(/height:calc\(var\(--cw\)\/([\d.]+)\)/) || [])[1]);
    const cropAR = 180 / 155;
    const boxAR  = (g('width') * caseAR) / g('height');
    ok(Math.abs(boxAR - cropAR) < 0.01,
       `box aspect ${boxAR.toFixed(4)} matches the crop's ${cropAR.toFixed(4)}`);
    ok(img.getAttribute('width') === '180' && img.getAttribute('height') === '155',
       'intrinsic size declared, so the panel reserves layout before the image loads');
  }

  // ---- Stamp-S3A label photo (ADR 0033) -----------------------------------
  {
    const img = doc.querySelector('#devsticker img');
    ok(img !== null, 'sticker is an <img>, not CSS art');
    ok(img.getAttribute('src') === 'stamp.webp', 'sticker points at the firmware route');
    // Not a data: URI. Base64 is 33% overhead on already-compressed bytes and
    // would ride in the gzipped page on every load instead of being cached.
    ok(!/^data:/.test(img.getAttribute('src') || ''), 'sticker is not base64-inlined');
    ok((img.getAttribute('alt') || '').length > 10, 'sticker image has real alt text');
    // Intrinsic size must match the crop, so the box cannot silently letterbox.
    ok(img.getAttribute('width') === '263' && img.getAttribute('height') === '188',
       'sticker declares its intrinsic 263x188');
    // The rainbow gradient was decorative and is gone for good.
    ok(!/#devsticker\{[^}]*repeating-linear-gradient/.test(styleText),
       'invented rainbow gradient removed');
    const px = (prop) => {
      const b = styleText.slice(styleText.indexOf('#devsticker{'));
      const m = b.slice(0, b.indexOf('}')).match(new RegExp(prop + ':([\\d.]+)%'));
      return m ? parseFloat(m[1]) : null;
    };
    ok(Math.abs(px('left') - 68.39) < 0.1, 'sticker left at the measured 68.39%');
    ok(Math.abs(px('top') - 2.19) < 0.1, 'sticker top at the measured 2.19%');
    // Box aspect must equal the crop's own (263/188 = 1.399) under the case
    // aspect, or object-fit:fill would distort the pinout text.
    // Case aspect comes FROM the stylesheet (#dev height is --cw/1.5558), not
    // a constant duplicated here -- a hardcoded copy would go stale the moment
    // the measured case ratio changed.
    const caseAR = parseFloat((styleText.match(/height:calc\(var\(--cw\)\/([\d.]+)\)/) || [])[1]);
    ok(caseAR > 1.4 && caseAR < 1.7, `case aspect parsed from CSS (${caseAR})`);
    // width% is of case WIDTH, height% is of case HEIGHT = width/caseAR, so
    // the pixel ratio is (w%/h%) * caseAR. Dividing instead gives 0.579 --
    // worth stating, because both directions look plausible written down.
    const ar = (px('width') / px('height')) * caseAR;
    ok(Math.abs(ar - 263 / 188) < 0.04, `sticker box matches crop aspect (got ${ar.toFixed(3)})`);
  }

  console.log(fail === 0 ? '\nPASS: served page builds the measured ADV keyboard'
                         : '\nFAILURES: ' + fail);
  process.exit(fail === 0 ? 0 : 1);
}, 300);
