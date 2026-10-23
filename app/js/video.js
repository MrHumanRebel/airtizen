/* Legacy Tizen 3.0: use the native HLS decoder instead of the broken MSE path. */
(function (global) {
  'use strict';
  var video,overlay,timer,epoch=0,generation=-1,active=false,stopped=true,lastError='';
  var base='http://127.0.0.1:45110';
  function log(s){if(global.AirTizenBridge)global.AirTizenBridge.log(s);}
  function schedule(ms){clearTimeout(timer);if(!stopped)timer=setTimeout(poll,ms);}
  function dispose(reason){
    epoch++;generation=-1;active=false;
    if(video){video.pause();video.removeAttribute('src');video.load();}
    if(overlay)overlay.className='hidden';
    if(reason)log('HLS reset: '+reason);
  }
  function poll(){
    var x=new XMLHttpRequest(),token=epoch;
    x.open('GET',base+'/status',true);x.timeout=2000;
    if(global.AirTizenControl)global.AirTizenControl.authorize(x);
    x.onload=function(){
      if(stopped||token!==epoch)return;
      var result;try{result=JSON.parse(x.responseText);}catch(ignore){schedule(300);return;}
      if(global.AirTizenShare){global.AirTizenShare.update(result.playback);if(global.AirTizenShare.isActive()){if(generation!==-1)dispose('shared media');schedule(200);return;}}
      if(global.AirTizenPhoto){global.AirTizenPhoto.update(result.photo);if(global.AirTizenPhoto.isActive()){if(generation!==-1)dispose('photo');schedule(200);return;}}
      var state=result.video;
      if(!state){schedule(200);return;}
      if(state.generation!==generation){
        if(generation!==-1)dispose('generation changed');
        if(state.available&&state.latest>=15){
          generation=state.generation;
          video.src=global.AirTizenControl.url(base+'/video/live.m3u8');
          video.load();var playing=video.play();if(playing&&playing.catch)playing.catch(function(){});
          log('HLS video opened generation='+generation);
        }
      }
      schedule(200);
    };
    x.onerror=x.ontimeout=function(){if(token===epoch)schedule(500);};x.send(null);
  }
  global.AirTizenVideo={
    start:function(){
      video=document.getElementById('mirror-video');overlay=document.getElementById('video-overlay');stopped=false;
      video.addEventListener('playing',function(){if(generation!==-1){active=true;overlay.className='';log('HLS playing time='+video.currentTime);}});
      video.addEventListener('error',function(){if(generation!==-1){lastError='HLS decoder error code='+(video.error?video.error.code:0);log(lastError);dispose();schedule(1000);}});
      if(global.AirTizenShare)global.AirTizenShare.start();if(global.AirTizenPhoto)global.AirTizenPhoto.start();schedule(0);
    },
    stop:function(){stopped=true;clearTimeout(timer);dispose('stopped');},
    isActive:function(){return active||!!(global.AirTizenPhoto&&global.AirTizenPhoto.isActive())||!!(global.AirTizenShare&&global.AirTizenShare.isActive());},
    debugSnapshot:function(){return {mode:'native-hls',generation:generation,active:active,lastError:lastError,time:video?video.currentTime:0,ready:video?video.readyState:0};}
  };
})(window);

/* Keep the validated HTTP HLS player as fallback; native PPAPI presents frames. */
(function(global){
 'use strict';
 var fallback=global.AirTizenVideo,module,timer,active=false,stopped=true,usingFallback=false,last={},container;
 function log(s){if(global.AirTizenBridge)global.AirTizenBridge.log(s);}
 function size(full){
  if(!container||!module)return;
  container.style.position='absolute';container.style.left='0';container.style.top='0';container.style.zIndex=full?'5':'';
  container.style.width=full?'1920px':'1px';container.style.height=full?'1080px':'1px';
  module.setAttribute('width',full?'1920':'1');module.setAttribute('height',full?'1080':'1');
 }
 function useFallback(reason){
  if(usingFallback||stopped)return;usingFallback=true;active=false;size(false);
  if(module)module.postMessage('airtizen-video-off');
  log('Direct video unavailable; HLS fallback: '+reason);fallback.start();
 }
 function message(event){
  if(stopped||typeof event.data!=='string')return;
  var value;try{value=JSON.parse(event.data);}catch(ignore){return;}
  if(value.type!=='airtizen-native-video')return;last=value;
  if(usingFallback)return;
  if(value.state==='playing'){active=true;size(true);if(value.frames===1)log('Direct hardware video playing');}
  else if(value.state==='idle'){active=false;size(false);}
  else if(value.state==='error')useFallback('code='+value.code);
 }
 function attach(){
  if(stopped)return;
  module=document.querySelector('#receiver-module embed');container=document.getElementById('receiver-module');
  if(!module||!global.AirTizenEmbedded||global.AirTizenEmbedded.phase!=='loaded'){timer=setTimeout(attach,100);return;}
  module.addEventListener('message',message);module.postMessage('airtizen-video-on');log('Direct hardware video enabled');
 }
 global.AirTizenVideo={
  start:function(){stopped=false;usingFallback=false;attach();},
  stop:function(){stopped=true;clearTimeout(timer);active=false;if(module){module.removeEventListener('message',message);module.postMessage('airtizen-video-off');}size(false);if(usingFallback)fallback.stop();},
  isActive:function(){return usingFallback?fallback.isActive():active;},
  debugSnapshot:function(){return {mode:usingFallback?'native-hls-fallback':'direct-ppapi',active:active,stopped:stopped,metrics:last,fallback:usingFallback?fallback.debugSnapshot():null};}
 };
})(window);
