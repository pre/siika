// Siikapaneeli web UI — the arch's single hand-written index.html in PROGMEM
// (plans/architecture.md): vanilla JS, no framework, no build step; ships
// inside every OTA image so firmware and UI can never drift apart.
#pragma once

const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Siikapaneeli</title>
<style>
body{font-family:monospace;margin:1.2em;background:#08222a;color:#a0d2dc;max-width:660px}
h1{color:#e65a00}
h2{margin-top:1.4em;border-top:1px solid #1c4652;padding-top:.6em}
button{background:#113844;color:#a0d2dc;border:1px solid #2aa47a;padding:.5em 1em;margin:.15em;cursor:pointer;font-family:inherit}
button.on{background:#2aa47a;color:#052}
#big{display:flex;gap:1em;flex-wrap:wrap}
#big div{text-align:center;background:#0c303a;padding:.6em 1em;min-width:4.5em}
#big b{font-size:2em;display:block;color:#e0f4f8}
canvas{border:1px solid #2aa47a;touch-action:none;image-rendering:pixelated;max-width:100%}
input{background:#113844;color:#a0d2dc;border:1px solid #2aa47a;font-family:inherit;padding:.3em}
label{display:block;margin:.3em 0}
#msg{color:#e65a00;min-height:1.2em}
</style>
<h1>SIIKAPANEELI</h1>
<div>
<button id=m_idle onclick="setMode('idle')">IDLE</button>
<button id=m_draw onclick="setMode('draw')">DRAW</button>
<button id=m_clock onclick="setMode('clock')">CLOCK</button>
<button style="border-color:#e65a00;color:#e65a00" onclick="fetch('/api/trigger',{method:'POST'})">SIIKA!</button>
</div>
<div id=msg></div>

<h2>TILASTOT</h2>
<div id=big>
<div><b id=v_lastHour>-</b>TUNTI</div>
<div><b id=v_today>-</b>T&Auml;N&Auml;&Auml;N</div>
<div><b id=v_yest>-</b>EILEN</div>
<div><b id=v_total>-</b>YHTEENS&Auml;</div>
</div>
<p id=small>...</p>

<h2>PIIRR&Auml;</h2>
<input type=color id=pen value="#e65a00">
<button onclick="clearDraw()">TYHJENN&Auml;</button>
<br><canvas id=cv></canvas>

<h2>ASETUKSET</h2>
<form id=sform onsubmit="return saveSettings(event)"></form>

<h2>OTA</h2>
<form method=POST action=/update enctype=multipart/form-data>
<input type=file name=update> <input type=submit value=Flash>
</form>

<script>
const $=id=>document.getElementById(id);
let W=48,H=16,px=[],CELL=12;
const jget=async u=>(await fetch(u)).json();
const jpost=(u,b)=>fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});
function setMode(m){jpost('/api/mode',{mode:m}).then(poll)}

async function poll(){
 try{
  const s=await jget('/api/status');
  for(const k of ['lastHour','today','yest','total'])$('v_'+k).textContent=s[k];
  $('small').textContent=`mode ${s.mode} (base ${s.baseMode}) | up ${s.uptimeS} s | heap ${s.heap} | rssi ${s.rssi} | ntp ${s.timeKnown}`;
  for(const m of ['idle','draw','clock'])$('m_'+m).classList.toggle('on',s.baseMode==m);
 }catch(e){$('small').textContent='ei yhteyttä'}
}

// --- draw: pixel pen; the panel itself is the preview ---
const cv=$('cv'),cx=cv.getContext('2d');
function buildCanvas(){
 CELL=Math.max(6,Math.floor(Math.min(620/W,220/H)));
 cv.width=W*CELL;cv.height=H*CELL;
 px=Array(W*H).fill('000000');repaint();
}
function repaint(){
 for(let y=0;y<H;y++)for(let x=0;x<W;x++){
  cx.fillStyle='#'+px[y*W+x];cx.fillRect(x*CELL,y*CELL,CELL-1,CELL-1);
 }
}
function paint(e){
 if(!(e.buttons&1))return;
 const r=cv.getBoundingClientRect();
 const x=Math.floor((e.clientX-r.left)/(r.width/W)),y=Math.floor((e.clientY-r.top)/(r.height/H));
 if(x<0||x>=W||y<0||y>=H)return;
 px[y*W+x]=$('pen').value.slice(1);repaint();
}
cv.addEventListener('pointerdown',e=>{cv.setPointerCapture(e.pointerId);paint(e)});
cv.addEventListener('pointermove',paint);
cv.addEventListener('pointerup',sendFrame);
function sendFrame(){fetch('/api/draw',{method:'POST',body:px.join('')})}
function clearDraw(){px.fill('000000');repaint();sendFrame()}

// --- settings: generated key -> input, firmware clamps on POST ---
async function loadSettings(){
 const S=await jget('/api/settings');
 W=S.panelsX*16;H=S.panelsY*16;buildCanvas();
 const f=$('sform');f.innerHTML='';
 for(const [k,v] of Object.entries(S)){
  let inp;
  if(typeof v==='boolean')inp=`<input type=checkbox name=${k} ${v?'checked':''}>`;
  else if(/Color$/.test(k))inp=`<input type=color name=${k} value="#${v}">`;
  else inp=`<input type=number name=${k} value="${v}">`;
  f.insertAdjacentHTML('beforeend',`<label>${k} ${inp}</label>`);
 }
 f.insertAdjacentHTML('beforeend','<button>TALLENNA</button>');
}
async function saveSettings(ev){
 ev.preventDefault();
 const o={};
 for(const el of $('sform').elements){
  if(!el.name)continue;
  if(el.type==='checkbox')o[el.name]=el.checked;
  else if(el.type==='color')o[el.name]=el.value.slice(1).toUpperCase();
  else o[el.name]=+el.value;
 }
 const r=await(await jpost('/api/settings',o)).json();
 $('msg').textContent=r.rebootNeeded?'Tallennettu — ruudukon koko vaatii uudelleenkäynnistyksen':'Tallennettu';
 return false;
}

loadSettings();poll();setInterval(poll,2000);
</script>
)HTML";
