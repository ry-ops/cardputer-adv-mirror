/* Prove the upper legend sends shift=true and the lower sends shift=false,
   for the SAME coordinate -- i.e. both legends are genuinely reachable. */
import {readFileSync} from 'fs';
globalThis.window={};
eval(readFileSync('web/keymap.js','utf8'));
const K=window.KEYMAP;
const mods={shift:false,fn:false};
const sent=[];
const send=o=>sent.push(o);
function sendKey(k,forceShift){
  if(k.mod){mods[k.mod]=!mods[k.mod];return;}
  const sh=forceShift||mods.shift;
  send({r:k.r,c:k.c,shift:sh,ch:sh?k.hi:k.lo});
}
let fails=0;
for(const k of K.flat()){
  if(k.mod||k.hi===k.lo) continue;
  sendKey(k);            // lower legend
  sendKey(k,true);       // upper legend
  const [lo,hi]=sent.splice(-2);
  const ok = lo.ch===k.lo && !lo.shift && hi.ch===k.hi && hi.shift &&
             lo.r===hi.r && lo.c===hi.c;
  if(!ok){ fails++; console.log('  FAIL',JSON.stringify(k),lo,hi); }
}
console.log(fails? `FAIL ${fails}`
  : 'PASS: every dual-legend cap emits both characters from one coordinate');
process.exit(fails?1:0);
