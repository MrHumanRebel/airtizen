(function(){
  var focusables = [], focusIndex = 0, overlayOpen = false;
  function $(id){ return document.getElementById(id); }
  function pad(n){ return n < 10 ? '0'+n : ''+n; }
  function updateClock(){ var d=new Date(); $('clock').innerHTML = pad(d.getHours()) + ':' + pad(d.getMinutes()); }
  function collectFocus(){ focusables = []; var all = document.getElementsByTagName('*'); for(var i=0;i<all.length;i++){ if(all[i].getAttribute('data-focus')==='1') focusables.push(all[i]); } }
  function setFocus(i){ if(!focusables.length) return; if(focusables[focusIndex]) focusables[focusIndex].className = focusables[focusIndex].className.replace(/\bfocus\b/g,''); focusIndex = (i + focusables.length) % focusables.length; focusables[focusIndex].className += ' focus'; }
  function action(){ var el=focusables[focusIndex]; if(!el) return; var a=el.getAttribute('data-action'); if(a==='start') startReceiver(); if(a==='diagnostics') showLog(); if(a==='settings') log('settings selected'); }
  function setText(id, txt){ var el=$(id); if(el) el.innerHTML = txt; }
  function cls(id, name){ var el=$(id); if(el) el.className = 'card-value ' + name; }
  function refresh(){
    window.AirTizenBridge.status(function(res){
      var s=res.status;
      setText('st-mdns', s.mdns); cls('st-mdns', s.mdns==='advertising'?'ok':'wait');
      setText('st-raop', s.raop); cls('st-raop', s.raop==='listening'?'ok':'wait');
      setText('st-pair', s.pair); cls('st-pair', s.pair==='verified'?'ok':'wait');
      setText('st-audio', s.audio); cls('st-audio', s.audio==='playing'?'ok':'wait');
      setText('codec', s.codec); setText('rate', s.rate); setText('buffer', s.buffer);
      setText('hero-status', s.connected ? 'Connected: ' + s.device : ('Service: ' + (res.service || 'offline')));
      setText('hero-detail', s.detail || 'Waiting for runtime status');
      $('small-log').innerHTML = (res.log || '').split('\n').slice(-3).join('\n');
      if(overlayOpen) $('full-log').innerHTML = res.log || '';
    });
  }
  function startReceiver(){ window.AirTizenBridge.start(function(){ refresh(); }); }
  function showLog(){ overlayOpen=true; $('log-overlay').className=''; refresh(); }
  function hideLog(){ overlayOpen=false; $('log-overlay').className='hidden'; }
  function clearLog(){ window.AirTizenBridge.clearLog(); refresh(); }
  function log(m){ window.AirTizenBridge.log(m); refresh(); }
  function key(e){ var k=e.keyCode||e.which;
    if(overlayOpen){ if(k===10009 || k===27 || k===13){ hideLog(); return; } if(k===403){ clearLog(); return; } }
    if(k===38 || k===37){ setFocus(focusIndex-1); return; }
    if(k===40 || k===39){ setFocus(focusIndex+1); return; }
    if(k===13){ action(); return; }
    if(k===406){ showLog(); return; }
    if(k===403){ clearLog(); return; }
    if(k===10009){ try{ tizen.application.getCurrentApplication().exit(); }catch(ex){} }
  }
  window.onload=function(){ updateClock(); setInterval(updateClock,1000); collectFocus(); setFocus(0); document.addEventListener('keydown', key, false); startReceiver(); setInterval(refresh,1600); };
})();
