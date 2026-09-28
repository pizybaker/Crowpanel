#pragma once

// Upload page served to the phone. Talks to Transfer.cpp's endpoints:
//   GET  /api/books            -> {"free":bytes,"books":[{"path":..,"size":..},..]}
//   POST /upload?size=N        multipart "file"
//   POST /api/delete  path=..  (form field; a book or a wallpaper)
//   GET  /api/wallpapers       -> {"current":path,"items":[{"path":..,"size":..},..]}
//   GET  /wallpaper?path=..    the image itself
//   POST /upload-wallpaper?size=N  multipart "file" (400x300 JPEG made in the browser)
//   POST /api/wallpaper  path=..   make it the home-screen wallpaper
static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CrowPanel Reader</title>
<style>
:root{--ink:#111;--muted:#666;--line:#ddd;--accent:#111;--bg:#fafafa;--card:#fff;--bad:#b00020}
@media (prefers-color-scheme:dark){:root{--ink:#eee;--muted:#999;--line:#333;--accent:#eee;--bg:#111;--card:#1b1b1b}}
*{box-sizing:border-box}
body{margin:0;font:16px/1.4 -apple-system,system-ui,sans-serif;color:var(--ink);background:var(--bg)}
main{max-width:560px;margin:0 auto;padding:20px 16px 40px}
h1{font-size:22px;margin:4px 0 2px}
.sub{color:var(--muted);font-size:14px;margin-bottom:20px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:16px}
h2{font-size:15px;margin:0 0 12px;text-transform:uppercase;letter-spacing:.04em;color:var(--muted)}
.pick{display:block;border:2px dashed var(--line);border-radius:10px;padding:22px;text-align:center;cursor:pointer}
.pick input{display:none}
button{font:inherit;border:0;border-radius:8px;padding:10px 16px;background:var(--accent);color:var(--bg);cursor:pointer}
button:disabled{opacity:.4}
.row{display:flex;align-items:center;gap:10px;padding:10px 0;border-top:1px solid var(--line)}
.row:first-child{border-top:0}
.name{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.size{color:var(--muted);font-size:13px;white-space:nowrap}
.del{background:none;color:var(--bad);padding:6px 8px}
progress{width:100%;height:6px;margin-top:6px}
.err{color:var(--bad);font-size:13px}
.empty{color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:12px;margin-top:12px}
.wp{border:1px solid var(--line);border-radius:8px;overflow:hidden}
.wp.cur{border:2px solid var(--accent)}
.wp img{display:block;width:100%;aspect-ratio:4/3;object-fit:cover;background:#fff;filter:grayscale(1)}
.wp div{display:flex;align-items:center;justify-content:space-between;padding:6px 8px;font-size:13px}
.wp .name{font-size:13px}
.tag{font-weight:600}
.use{padding:6px 12px;font-size:14px}
</style></head><body><main>
<h1>CrowPanel Reader</h1>
<div class="sub" id="free">Connected</div>
<div class="card">
  <h2>Add books</h2>
  <label class="pick"><input id="files" type="file" multiple accept=".epub,.txt">
    <b>Choose .epub or .txt files</b><br><span class="size">They go into the books folder</span></label>
  <div id="queue"></div>
  <p><button id="go" disabled>Upload</button></p>
</div>
<div class="card"><h2>On the reader</h2><div id="books" class="empty">Loading…</div></div>
<div class="card">
  <h2>Wallpaper</h2>
  <label class="pick"><input id="img" type="file" accept="image/*">
    <b>Add an image</b><br><span class="size">Cropped to the 400&times;300 screen on your phone</span></label>
  <div id="imgmsg" class="size"></div>
  <div id="walls" class="grid"></div>
</div>
</main><script>
const $=id=>document.getElementById(id);
const fmt=n=>n>1048576?(n/1048576).toFixed(1)+' MB':Math.max(1,Math.round(n/1024))+' KB';
const esc=s=>s.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
let picked=[];
$('files').onchange=()=>{
  picked=[...$('files').files].filter(f=>/\.(epub|txt)$/i.test(f.name));
  $('queue').innerHTML=picked.map((f,i)=>`<div class="row" style="display:block"><div style="display:flex;gap:10px">
    <span class="name">${esc(f.name)}</span><span class="size" id="s${i}">${fmt(f.size)}</span></div>
    <progress id="p${i}" max="1" value="0"></progress></div>`).join('')||'<p class="err">Only .epub and .txt files can be added.</p>';
  $('go').disabled=!picked.length;
};
function send(f,i){return new Promise(done=>{
  const x=new XMLHttpRequest(),d=new FormData();d.append('file',f,f.name);
  x.upload.onprogress=e=>{if(e.lengthComputable)$('p'+i).value=e.loaded/e.total};
  x.onload=()=>{$('s'+i).textContent=x.status==200?'Done':'Failed';if(x.status!=200)$('s'+i).className='err';done()};
  x.onerror=()=>{$('s'+i).textContent='Failed';$('s'+i).className='err';done()};
  x.open('POST','/upload?size='+f.size);x.send(d);
})}
$('go').onclick=async()=>{
  $('go').disabled=true;$('files').disabled=true;
  for(let i=0;i<picked.length;i++)await send(picked[i],i);
  $('files').disabled=false;$('files').value='';picked=[];load();
};
async function del(p){
  if(!confirm('Delete '+p.split('/').pop()+' from the reader?'))return;
  await fetch('/api/delete',{method:'POST',body:new URLSearchParams({path:p})});load();
}
async function load(){
  try{
    const r=await (await fetch('/api/books')).json();
    $('free').textContent=fmt(r.free)+' free on the SD card';
    $('books').className=r.books.length?'':'empty';
    $('books').innerHTML=r.books.length?r.books.map(b=>`<div class="row"><span class="name">${esc(b.path.split('/').pop())}</span>
      <span class="size">${fmt(b.size)}</span><button class="del" data-p="${esc(b.path)}">Delete</button></div>`).join(''):'No books yet.';
    document.querySelectorAll('.del').forEach(b=>b.onclick=()=>del(b.dataset.p));
  }catch(e){$('books').textContent='Could not reach the reader.'}
}
// Scale and centre-crop to the panel (same as the reader does), as a small
// baseline JPEG the reader can decode, whatever the phone's format is.
function toWallpaper(file){return new Promise((ok,bad)=>{
  const img=new Image();
  img.onload=()=>{
    const c=document.createElement('canvas');c.width=400;c.height=300;
    const g=c.getContext('2d'),s=Math.max(400/img.naturalWidth,300/img.naturalHeight);
    const w=img.naturalWidth*s,h=img.naturalHeight*s;
    g.fillStyle='#fff';g.fillRect(0,0,400,300);g.drawImage(img,(400-w)/2,(300-h)/2,w,h);
    URL.revokeObjectURL(img.src);
    c.toBlob(b=>b?ok(b):bad(),'image/jpeg',0.92);
  };
  img.onerror=()=>bad();
  img.src=URL.createObjectURL(file);
})}
$('img').onchange=async()=>{
  const f=$('img').files[0];if(!f)return;
  $('imgmsg').textContent='Preparing…';$('imgmsg').className='size';
  try{
    const blob=await toWallpaper(f),d=new FormData();
    const name=(f.name.replace(/\.[^.]*$/,'')||'wallpaper').replace(/[^\w\- ]/g,'_')+'.jpg';
    d.append('file',blob,name);
    $('imgmsg').textContent='Uploading…';
    const r=await fetch('/upload-wallpaper?size='+blob.size,{method:'POST',body:d});
    if(!r.ok)throw new Error(await r.text());
    $('imgmsg').textContent='Added. Tap Use to make it your wallpaper.';
  }catch(e){$('imgmsg').textContent='Could not add that image. '+(e&&e.message?e.message:'');$('imgmsg').className='err'}
  $('img').value='';loadWalls();
};
async function useWall(p){await fetch('/api/wallpaper',{method:'POST',body:new URLSearchParams({path:p})});loadWalls()}
async function delWall(p){
  if(!confirm('Delete '+p.split('/').pop()+' from the reader?'))return;
  await fetch('/api/delete',{method:'POST',body:new URLSearchParams({path:p})});loadWalls();
}
async function loadWalls(){
  try{
    const r=await (await fetch('/api/wallpapers')).json();
    $('walls').innerHTML=r.items.length?r.items.map(w=>{const cur=w.path==r.current,n=esc(w.path.split('/').pop()),p=esc(w.path);
      return `<div class="wp${cur?' cur':''}"><img src="/wallpaper?path=${encodeURIComponent(w.path)}&s=${w.size}" alt="${n}">
      <div><span class="name">${n}</span>${cur?'<span class="tag">Current</span>':`<button class="use" data-u="${p}">Use</button>`}</div>
      <div><span></span><button class="del" data-w="${p}">Delete</button></div></div>`}).join(''):'<p class="empty">No wallpapers yet.</p>';
    document.querySelectorAll('[data-u]').forEach(b=>b.onclick=()=>useWall(b.dataset.u));
    document.querySelectorAll('[data-w]').forEach(b=>b.onclick=()=>delWall(b.dataset.w));
  }catch(e){$('walls').textContent='Could not reach the reader.'}
}
load();loadWalls();
</script></body></html>)HTML";
