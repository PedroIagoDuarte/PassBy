// PassBy — painel. Comunica-se com o ESP32 via /api/*. (demo-data.js substitui window.PASSBY_API na demonstração)
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)],p2=n=>String(n).padStart(2,'0');
const call=async(u,m)=>{const c=new AbortController(),t=setTimeout(()=>c.abort(),2500);try{const r=await fetch(u,{method:m||'GET',signal:c.signal});if(!r.ok)throw 0;return await r.json()}finally{clearTimeout(t)}};
const api=window.PASSBY_API||{status:()=>call('/api/status'),data:()=>call('/api/data'),buzzer:on=>call('/api/buzzer?on='+(on?1:0),'POST'),reset:()=>call('/api/reset','POST'),clear:()=>call('/api/clear','POST'),time:()=>call('/api/time?t='+Math.floor(Date.now()/1000),'POST')};
const IC={visao:'<rect x="2" y="2" width="5" height="5" rx="1"/><rect x="9" y="2" width="5" height="5" rx="1"/><rect x="2" y="9" width="5" height="5" rx="1"/><rect x="9" y="9" width="5" height="5" rx="1"/>',hist:'<circle cx="8" cy="8" r="6"/><path d="M8 4.5V8l2.5 1.5"/>',stats:'<path d="M2 14V8M6 14V3M10 14V6M14 14V10"/>',conf:'<path d="M2 4h12M2 8h12M2 12h12"/><circle cx="5" cy="4" r="1.5"/><circle cx="11" cy="8" r="1.5"/><circle cx="7" cy="12" r="1.5"/>',ent:'<path d="M13 3v10M2 8h8M7 5l3 3-3 3"/>',sai:'<path d="M3 3v10M6 8h8M11 5l3 3-3 3"/>',rst:'<path d="M3 8a5 5 0 1 0 1.8-3.8M3 2v3h3"/>'};
const ic=n=>`<svg class="ic" viewBox="0 0 16 16">${IC[n]}</svg>`;
const LOGO='<svg viewBox="0 0 64 64"><path d="M20 19C9 20 5 12 8 5M44 19c11 1 15-7 12-14"/><path d="M19 27C10 24 5 27 5 30c4 4 11 3 15 1M45 27c9-3 14 0 14 3-4 4-11 3-15 1"/><path d="M20 17c8-3 16-3 24 0 2 12 0 22-3 29-1 8-5 12-9 12s-8-4-9-12c-3-7-5-17-3-29Z"/><path d="M28 21c2 2 6 2 8 0M25 47c4 3 10 3 14 0M27 52h.1M37 52h.1M25 33h.1M39 33h.1"/></svg>';
const NAV=[['index','Visão geral','visao'],['historico','Histórico','hist'],['estatisticas','Estatísticas','stats'],['configuracoes','Configurações','conf']];
const page=document.body.dataset.page;
document.body.insertAdjacentHTML('afterbegin',`<aside class="side"><div class="brand">${LOGO}<div>Pass<span>By</span></div></div>${NAV.map(n=>`<a class="nv ${n[0]==page?'on':''}" href="${n[0]}.html">${ic(n[2])}${n[1]}</a>`).join('')}${window.PASSBY_DEMO?'<div class="demo">MODO DEMONSTRAÇÃO</div>':''}<div class="dev off" id="dev"><i></i><div><b>ESP32</b><small id="devTxt">Conectando…</small></div></div></aside>`);
const S={online:false,fail:0,last:0,ent:0,sai:0,buzzer:true,rev:-1,events:[],exits:[],resets:[],now:Date.now()/1000};
const dk=t=>{const d=new Date(t*1000);return d.getFullYear()+'-'+p2(d.getMonth()+1)+'-'+p2(d.getDate())};
const hm=t=>{const d=new Date(t*1000);return p2(d.getHours())+':'+p2(d.getMinutes())};
const hms=t=>{const d=new Date(t*1000);return p2(d.getHours())+':'+p2(d.getMinutes())+':'+p2(d.getSeconds())};
const fd=k=>k.split('-').reverse().join('/');
const today=()=>dk(Date.now()/1000);
// por dia: {e: entradas, s: saidas}
const perDay=()=>{const m={},g=k=>m[k]||(m[k]={e:0,s:0});S.events.forEach(t=>g(dk(t)).e++);S.exits.forEach(t=>g(dk(t)).s++);return m};
const Z={e:0,s:0};
const perHour=ev=>{const h=Array(24).fill(0);ev.forEach(t=>h[new Date(t*1000).getHours()]++);return h};
const hr=h=>p2(h)+'h–'+p2((h+1)%24)+'h';
const off=n=>{const d=new Date();d.setDate(d.getDate()+n);return d};
const sum=(ks,pd,f)=>ks.reduce((a,x)=>a+pd[x][f],0);
const pl=(n,u,p)=>n+' '+(n==1?u:p);
// grafico de barras com duas series: v = entradas, w = saidas
function bars(id,items,hiLast){const mx=Math.max(1,...items.map(i=>Math.max(i.v,i.w)));const lb=(i,n)=>i.s&&n?`<em>${n}</em>`:'';$(id).innerHTML=items.map((i,x)=>{const hi=hiLast&&x==items.length-1?'hi':'';return `<div class="col"><div class="pair"><div class="bar ${hi}" style="height:${i.v/mx*100}%">${lb(i,i.v)}</div><div class="bar out ${hi}" style="height:${i.w/mx*100}%">${lb(i,i.w)}</div></div><u>${i.l}</u></div>`}).join('')}
const R={
index(){const on=S.online;$('#countIn').textContent=on?S.ent:'—';$('#countOut').textContent=on?S.sai:'—';$('#kConn').textContent=on?'Conectado':'Desconectado';
$('#kLast').textContent=on?'agora':(S.last?'há '+Math.round((Date.now()-S.last)/1000)+' s (sem sinal)':'—');
$('#kBuz').textContent=on?(S.buzzer?'Ativado':'Desativado'):'—';
const t=today(),h=perHour(S.events.filter(x=>dk(x)==t)),o=perHour(S.exits.filter(x=>dk(x)==t));bars('#chHora',h.map((v,i)=>({v,w:o[i],l:i%3==0?p2(i):'',s:0})))},
historico(){const pd=perDay(),g=k=>pd[k]||Z;bars('#ch7',[...Array(7)].map((_,i)=>{const d=off(i-6),k=dk(d/1000);return{v:g(k).e,w:g(k).s,l:d.toLocaleDateString('pt-BR',{weekday:'short'}).replace('.',''),s:1}}),1);
const sel=$('#dia').value,tp=$('#tipo').value;$('#diaTotal').textContent=sel?g(sel).e:'—';$('#diaSaidas').textContent=sel?g(sel).s:'—';$('#diaTxt').textContent=sel?'entradas e saídas em '+fd(sel):'Selecione uma data.';
$('#dias').innerHTML=Object.keys(pd).sort().reverse().slice(0,30).map(k=>`<div class="click" data-d="${k}"><span>${fd(k)}</span><span>${pl(pd[k].e,'entrada','entradas')} · ${pl(pd[k].s,'saída','saídas')}</span></div>`).join('')||'<p class="muted">Sem registros.</p>';
const all=[...(tp=='s'?[]:S.events.map(t=>({t,k:'e'}))),...(tp=='e'?[]:S.exits.map(t=>({t,k:'s'}))),...(tp?[]:S.resets.map(t=>({t,k:'r'})))].sort((a,b)=>b.t-a.t).filter(x=>!sel||dk(x.t)==sel).slice(0,sel?200:40);
$('#lstTitulo').textContent=sel?'Registros de '+fd(sel):'Registros recentes';
const TP={e:['ev-in','tp in','ent','Entrada'],s:['ev-out','tp out','sai','Saída'],r:['rs','tp','rst','Contagem reiniciada']};
$('#lista').innerHTML=all.map(x=>{const c=TP[x.k];return `<div class="${c[0]}"><span>${fd(dk(x.t))} · ${hms(x.t)}</span><span class="${c[1]}">${ic(c[2])}${c[3]}</span></div>`}).join('')||'<p class="muted">Sem registros.</p>'},
estatisticas(){const pd=perDay(),k=today(),ds=Object.keys(pd),pre=k.slice(0,7),mk=ds.filter(x=>x.startsWith(pre)),hj=pd[k]||Z;
$('#sHoje').textContent=hj.e;$('#sHojeS').textContent='entradas · '+pl(hj.s,'saída','saídas');
$('#sMes').textContent=sum(mk,pd,'e');$('#sMesS').textContent='entradas · '+pl(sum(mk,pd,'s'),'saída','saídas');
const fm=n=>(n/ds.length).toFixed(1).replace('.',',');
$('#sMedia').textContent=ds.length?fm(S.events.length):'—';$('#sMediaS').textContent=ds.length?'entradas/dia · '+fm(S.exits.length)+' saídas/dia':'dias com registro';
bars('#ch14',[...Array(14)].map((_,i)=>{const d=off(i-13),x=pd[dk(d/1000)]||Z;return{v:x.e,w:x.s,l:i%2?'':p2(d.getDate()),s:0}}),1);
const ms=[...Array(6)].map((_,i)=>{const d=new Date();d.setDate(1);d.setMonth(d.getMonth()+i-5);const p=d.getFullYear()+'-'+p2(d.getMonth()+1),kk=ds.filter(x=>x.startsWith(p));return{v:sum(kk,pd,'e'),w:sum(kk,pd,'s'),l:d.toLocaleDateString('pt-BR',{month:'short'}).replace('.',''),s:1}});bars('#ch6m',ms,1);
const he=perHour(S.events),hs=perHour(S.exits),top=he.map((v,i)=>[i,v,hs[i],v+hs[i]]).filter(x=>x[3]).sort((a,b)=>b[3]-a[3]).slice(0,5),mx=Math.max(1,...top.map(x=>Math.max(x[1],x[2])));
$('#sPico').textContent=top.length?hr(top[0][0]):'—';
$('#picos').innerHTML=top.map(x=>`<div class="pk"><span>${hr(x[0])}</span><div class="dbl"><i style="width:${x[1]/mx*100}%"></i><i class="s" style="width:${x[2]/mx*100}%"></i></div><b>${x[1]} · ${x[2]}</b></div>`).join('')||'<p class="muted">Sem registros.</p>'},
configuracoes(){$('#buz').checked=S.buzzer;const r=S.resets[S.resets.length-1];$('#ultReset').textContent=r?'Última reinicialização: '+fd(dk(r))+' às '+hm(r):'Nenhuma reinicialização registrada.'}};
function paint(){const on=S.online;$('#dev').classList.toggle('off',!on);$('#devTxt').textContent=on?'Conectado':'Desconectado';$('#alert').hidden=on;
$$('[data-needs]').forEach(e=>e.disabled=!on);R[page]()}
async function tick(){try{const s=await api.status();S.fail=0;if(!S.online)S.rev=-1;S.online=true;S.last=Date.now();
// "in"/"out" = totais; "count" (firmware antigo) equivale as entradas
Object.assign(S,{ent:s.in!==undefined?s.in:s.count,sai:s.out||0,buzzer:!!s.buzzer});
if(s.timeOk===false&&api.time)await api.time();
if(s.rev!==S.rev&&(S.rev<0||Date.now()-(S.tData||0)>10000)){const d=await api.data();S.events=d.events||[];S.exits=d.exits||[];S.resets=d.resets||[];S.rev=s.rev;S.tData=Date.now()}}catch(e){if(++S.fail>=2)S.online=false}paint()}
const act=async f=>{try{await f();S.rev=-1;await tick()}catch(e){alert('Falha na comunicação com o ESP32. Tente novamente.')}};
if(page=='historico'){$('#dia').value=today();$('#dia').onchange=paint;$('#tipo').onchange=paint;$('#recentes').onclick=()=>{$('#dia').value='';paint()};$('#dias').onclick=e=>{const r=e.target.closest('[data-d]');if(r){$('#dia').value=r.dataset.d;paint()}}}
if(page=='configuracoes'){$('#buz').onchange=e=>act(()=>api.buzzer(e.target.checked));
$('#reset').onclick=()=>confirm('Zerar os totais de entradas e saídas? O histórico de eventos será preservado.')&&act(api.reset);
$('#clear').onclick=()=>confirm('Excluir TODO o histórico? Esta ação não pode ser desfeita. Os totais atuais não serão alterados.')&&act(api.clear)}
setInterval(tick,2000);setInterval(()=>{if(page=='index')paint()},1000);tick();
