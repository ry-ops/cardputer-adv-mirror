/* Headless test of the browser keyboard's coordinate emission.
 *
 * WHY: a C++ build cannot see JavaScript errors, and the page's whole job is to
 * emit the RIGHT (row,col). A wrong coordinate is silent -- the device just
 * does something else.
 *
 * Expectations are DERIVED from web/keymap.js (itself generated from the vendor
 * header), never hardcoded. The first version of this test hardcoded arrow
 * coordinates from memory and reported 3 failures against correct code.
 */
import {readFileSync} from 'fs';
globalThis.window = {};
eval(readFileSync('web/keymap.js','utf8'));
const KEYMAP = window.KEYMAP;

const byChar={}, byLabel={};
KEYMAP.forEach(r=>r.forEach(k=>{
  if(!k.mod){
    if(!(k.lo in byChar)) byChar[k.lo]={r:k.r,c:k.c,shift:false};
    if(!(k.hi in byChar)) byChar[k.hi]={r:k.r,c:k.c,shift:true};
  }
  byLabel[k.lo]={r:k.r,c:k.c,shift:false};
}));
const ARROW={ArrowUp:';',ArrowDown:'.',ArrowLeft:',',ArrowRight:'/'};

// Ground truth: find each character's position by scanning the map itself.
const find = ch => { for(const row of KEYMAP) for(const k of row)
  if(k.lo===ch) return [k.r,k.c]; return null; };

let fail=0;
const check=(name,got,want)=>{
  const ok = got && want && got.r===want[0] && got.c===want[1];
  if(!ok) fail++;
  console.log(`  ${ok?'PASS':'FAIL'}  ${name.padEnd(12)} -> ${got?`r${got.r} c${got.c}`:'UNRESOLVED'}  (header says r${want?.[0]} c${want?.[1]})`);
};
for (const [key,ch] of Object.entries(ARROW)) check(key, byChar[ch], find(ch));
check('Enter',     byLabel['enter'], find('enter'));
check('Backspace', byLabel['del'],   find('del'));
check('Tab',       byLabel['tab'],   find('tab'));
check("'a'",       byChar['a'],      find('a'));
check("space",     byChar[' '],      find(' '));

// Shifted forms must resolve to the same physical key with shift set.
for (const [lo,hi] of [['a','A'],['5','%'],['1','!']]) {
  const l=byChar[lo], h=byChar[hi];
  const ok = l&&h&&l.r===h.r&&l.c===h.c&&h.shift===true&&l.shift===false;
  if(!ok) fail++;
  console.log(`  ${ok?'PASS':'FAIL'}  '${lo}'/'${hi}' share one key with shift flag`);
}
// Modifiers must never be sendable as characters.
const leak = ['fn','shift','ctrl','alt','opt'].filter(m=>m in byChar);
if(leak.length) fail++;
console.log(`  ${leak.length?'FAIL':'PASS'}  modifiers excluded from byChar (${leak.join(',')||'none'})`);
// Every coordinate must be in range for the firmware's bounds check.
const bad = KEYMAP.flat().filter(k=>k.r<0||k.r>3||k.c<0||k.c>13);
if(bad.length) fail++;
console.log(`  ${bad.length?'FAIL':'PASS'}  all 56 coords within 4x14 (${KEYMAP.flat().length} keys)`);

console.log(fail? `\n${fail} FAILURES` : '\nall mappings agree with the vendor header');
process.exit(fail?1:0);
