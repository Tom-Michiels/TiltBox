// Single-page dashboard: sessions list with audio, transcript, code, device outcome, cost.
export function dashboardHtml() {
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TiltBox sessions</title>
<style>
:root{--bg:#fff;--fg:#1a1a1a;--mut:#666;--line:#e3e3e3;--card:#f7f7f8;--ok:#1a7f37;--bad:#c62828;--warn:#b26a00;--acc:#2458d6}
@media(prefers-color-scheme:dark){:root{--bg:#121214;--fg:#eaeaea;--mut:#9a9a9a;--line:#2a2a2e;--card:#1b1b1f;--ok:#4caf50;--bad:#ef5350;--warn:#ffb74d;--acc:#7aa2ff}}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{display:flex;gap:12px;align-items:center;padding:12px 16px;border-bottom:1px solid var(--line);position:sticky;top:0;background:var(--bg);flex-wrap:wrap}
h1{font-size:16px;margin:0 8px 0 0}
main{padding:12px 16px;max-width:1200px;margin:0 auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 14px;margin:10px 0}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.mut{color:var(--mut)}.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}
.tag{display:inline-block;padding:1px 8px;border-radius:999px;border:1px solid var(--line);font-size:12px}
.tr{font-size:15px;margin:8px 0;white-space:pre-wrap}
pre{background:var(--bg);border:1px solid var(--line);border-radius:8px;padding:10px;overflow:auto;max-height:60vh;font:12px/1.4 ui-monospace,Menlo,monospace;margin:8px 0}
button,select,input{font:inherit;padding:4px 10px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--fg)}
button{cursor:pointer}button:hover{border-color:var(--acc)}
audio{height:30px;max-width:100%}
details summary{cursor:pointer;color:var(--acc)}
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px;margin:8px 0}
.stat{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:8px 10px}.stat b{display:block;font-size:18px}
@media(max-width:600px){main{padding:8px}}
</style></head><body>
<header><h1>TiltBox sessions</h1>
<select id="filter"><option value="">all</option><option value="ok">device ok</option><option value="error">errors</option><option value="noresult">no device result</option></select>
<input id="q" placeholder="search transcript / device" size="24">
<button onclick="load()">Refresh</button>
<span id="pv" class="mut"></span>
<a href="/api/sessions?limit=500" class="mut" style="margin-left:auto">JSON export</a></header>
<main>
<div class="stats" id="stats"></div>
<div id="list"></div>
<div class="row"><button id="more" onclick="more()" style="display:none">Load older</button></div>
</main>
<script>
let sessions=[],oldest=0;
const esc=s=>String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const fmt=ts=>new Date(ts).toLocaleString();
const money=c=>c==null?'':'$'+c.toFixed(4);
async function api(path,opt){const r=await fetch(path,opt);if(r.status===401){document.cookie='tb_admin=;path=/;max-age=0';location.replace('/');throw new Error('auth')}return r.json()}
async function load(){const d=await api('/api/sessions?limit=50');sessions=d.sessions;document.getElementById('pv').textContent='prompt '+d.prompt_version;render()}
async function more(){const d=await api('/api/sessions?limit=50&before='+oldest);sessions=sessions.concat(d.sessions);render()}
function status(s){
  if(s.error)return '<span class="tag bad">cloud error</span>';
  if(!s.result_status)return '<span class="tag mut">no device result yet</span>';
  if(s.result_status==='running'||s.result_status==='ok'||s.result_status==='aborted')return '<span class="tag ok">device '+esc(s.result_status)+'</span>';
  return '<span class="tag bad">device '+esc(s.result_status)+'</span>';
}
function render(){
  const f=document.getElementById('filter').value,q=document.getElementById('q').value.toLowerCase();
  let rows=sessions.filter(s=>{
    if(f==='ok'&&!(s.result_status==='running'||s.result_status==='ok'||s.result_status==='aborted'))return false;
    if(f==='error'&&!(s.error||(s.result_status&&!['running','ok','aborted'].includes(s.result_status))))return false;
    if(f==='noresult'&&(s.result_status||s.error))return false;
    if(q&&!((s.transcript||'')+' '+(s.device||'')).toLowerCase().includes(q))return false;return true});
  oldest=sessions.length?sessions[sessions.length-1].ts:0;
  document.getElementById('more').style.display=sessions.length>=50?'':'none';
  const n=sessions.length,ok=sessions.filter(s=>['running','ok','aborted'].includes(s.result_status)).length,err=sessions.filter(s=>s.error).length,
    cost=sessions.reduce((a,s)=>a+(s.cost_usd||0),0),gen=sessions.filter(s=>s.gen_ms).map(s=>s.gen_ms),stt=sessions.filter(s=>s.stt_ms).map(s=>s.stt_ms);
  const med=a=>a.length?a.sort((x,y)=>x-y)[a.length>>1]:0;
  document.getElementById('stats').innerHTML=['sessions|'+n,'device ok|'+ok,'cloud errors|'+err,'cost|'+money(cost),'median STT|'+med(stt)+' ms','median gen|'+med(gen)+' ms']
    .map(x=>{const[k,v]=x.split('|');return '<div class="stat"><span class="mut">'+k+'</span><b>'+v+'</b></div>'}).join('');
  document.getElementById('list').innerHTML=rows.map(s=>\`<div class="card" id="s-\${s.id}">
  <div class="row"><b>\${fmt(s.ts)}</b> \${status(s)} <span class="mut">\${esc(s.device)} · \${s.audio_seconds??'?'} s · \${esc(s.stt_model)} \${s.stt_ms||''} ms → \${esc(s.gen_model)} \${s.gen_ms||''} ms · \${s.code_chars||0} chars · \${money(s.cost_usd)} · prompt \${esc(s.prompt_version)}</span></div>
  <div class="tr">\${esc(s.transcript)||'<span class="mut">(no transcript)</span>'}</div>
  \${s.error?'<div class="bad">'+esc(s.error)+'</div>':''}
  <div class="row"><audio controls preload="none" src="/audio/\${s.id}.wav"></audio>
   <button onclick="showCode('\${s.id}')">code</button>
   \${s.result_output?'<button onclick="toggle(\\'out-'+s.id+'\\')">device output</button>':''}
   <button onclick="del('\${s.id}')" title="delete session and audio">delete</button></div>
  \${s.result_output?'<pre id="out-'+s.id+'" style="display:none">'+esc(s.result_output)+'</pre>':''}
  <pre id="code-\${s.id}" style="display:none"></pre></div>\`).join('')||'<p class="mut">No sessions yet.</p>';
}
function toggle(id){const e=document.getElementById(id);e.style.display=e.style.display==='none'?'':'none'}
async function showCode(id){const e=document.getElementById('code-'+id);if(e.style.display!=='none'){e.style.display='none';return}
  if(!e.textContent){const d=await api('/api/sessions/'+id);e.textContent=d.code||'(no code)';
    if(d.replays&&d.replays.length)e.textContent+='\\n\\n# ---- replays ----\\n'+d.replays.map(r=>'# '+fmt(r.ts)+' '+r.gen_model+' prompt '+r.prompt_version+' host:'+r.host_status+' '+(r.note||'')).join('\\n')}
  e.style.display=''}
async function del(id){if(!confirm('Delete this session and its audio?'))return;await api('/api/sessions/'+id,{method:'DELETE'});sessions=sessions.filter(s=>s.id!==id);render()}
document.getElementById('filter').onchange=render;document.getElementById('q').oninput=render;
if(location.search.includes('token='))history.replaceState(null,'','/');
load();
</script></body></html>`;
}
