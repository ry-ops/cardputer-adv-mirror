/* Every key the user enumerated must exist as a reachable target.
   Derived from the vendor keymap, then checked against the explicit list --
   so this fails if a legend is unrendered, not merely if a coordinate is absent. */
import {readFileSync} from 'fs';
globalThis.window={};
eval(readFileSync('web/keymap.js','utf8'));
const K=window.KEYMAP, flat=K.flat();

// What the UI will actually paint on the caps.
const painted=new Set();
for(const k of flat){
  const isSpace=(k.r===3&&k.c===13);
  if(k.mod||isSpace){ painted.add(isSpace?'space':k.lo); }
  else { painted.add(k.lo); if(k.hi!==k.lo) painted.add(k.hi); if(k.cap) painted.add(k.cap); }
}

const expect={
 'row1 alnum':['`','1','2','3','4','5','6','7','8','9','0','-','=','del'],
 'row2 qwerty':['q','w','e','r','t','y','u','i','o','p','[',']','\\','fn','tab'],
  // 'Aa' is the shift cap's actual silkscreen on the ADV, not the word "shift".
 'row3 home'  :['a','s','d','f','g','h','j','k','l',';',"'",'enter','Aa'],
 'row4 bottom':['ctrl','opt','alt','space','z','x','c','v','b','n','m',',','.','/'],
 'shifted'    :['~','!','@','#','$','%','^','&','*','(',')','_','+','{','}','|',':','"','<','>','?'],
 'arrows'     :['\u2191','\u2193','\u2190','\u2192'],
};
let bad=0;
for(const [grp,keys] of Object.entries(expect)){
  const miss=keys.filter(c=>!painted.has(c));
  console.log((miss.length?'MISSING':'ok     ')+'  '+grp.padEnd(12)+
              (miss.length?'-> '+miss.join(' '):`${keys.length} keys`));
  bad+=miss.length;
}
console.log('\npainted legends:',painted.size,'| matrix positions:',flat.length);
console.log(bad? `FAIL: ${bad} missing` : 'PASS: every enumerated key is represented');
process.exit(bad?1:0);
