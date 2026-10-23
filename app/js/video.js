/* Legacy Tizen 3.0: use the native HLS decoder instead of the broken MSE path. */
(function (global) {
  'use strict';
  var video, overlay, timer, epoch = 0, generation = -1, active = false, stopped = true;
  var initialized = false, updateShared, lastError = '', base = 'http://127.0.0.1:45110';
  function log(message) { if (global.AirTizenBridge) global.AirTizenBridge.log(message); }
  function schedule(ms) { clearTimeout(timer); if (!stopped) timer = setTimeout(poll, ms); }
  function dispose(reason) {
    epoch++; generation = -1; active = false;
    if (video) { video.pause(); video.removeAttribute('src'); video.load(); }
    if (overlay) overlay.className = 'hidden';
    if (reason) log('HLS reset: ' + reason);
  }
  function poll() {
    var xhr = new XMLHttpRequest(), token = epoch;
    xhr.open('GET', base + '/status', true); xhr.timeout = 2000;
    if (global.AirTizenControl) global.AirTizenControl.authorize(xhr);
    xhr.onload = function () {
      if (stopped || token !== epoch) return;
      var result, state;
      if (xhr.status !== 200) { dispose(); schedule(500); return; }
      try { result = JSON.parse(xhr.responseText); } catch (ignore) { schedule(300); return; }
      if (updateShared(result)) { if (generation !== -1) dispose('shared media'); schedule(200); return; }
      state = result.video;
      if (!state || !state.available) { if (generation !== -1) dispose('stream ended'); schedule(200); return; }
      if (state.generation !== generation) {
        if (generation !== -1) dispose('generation changed');
        if (state.latest >= 15) {
          generation = state.generation;
          video.src = global.AirTizenControl ? global.AirTizenControl.url(base + '/video/live.m3u8') : base + '/video/live.m3u8';
          video.load();
          var playing = video.play(); if (playing && playing.catch) playing.catch(function () {});
          log('HLS video opened generation=' + generation);
        }
      }
      schedule(200);
    };
    xhr.onerror = xhr.ontimeout = function () { if (!stopped && token === epoch) schedule(500); };
    xhr.send(null);
  }
  global.AirTizenHLSVideo = {
    start: function (shared) {
      if (!stopped) return;
      video = document.getElementById('mirror-video'); overlay = document.getElementById('video-overlay');
      stopped = false; updateShared = shared;
      if (!initialized) {
        video.addEventListener('playing', function () {
          if (!stopped && generation !== -1) { active = true; overlay.className = ''; log('HLS playing time=' + video.currentTime); }
        });
        video.addEventListener('error', function () {
          if (!stopped && generation !== -1) {
            lastError = 'HLS decoder error code=' + (video.error ? video.error.code : 0);
            log(lastError); dispose(); schedule(1000);
          }
        });
      }
      initialized = true; schedule(0);
    },
    stop: function () { stopped = true; clearTimeout(timer); dispose('stopped'); },
    isActive: function () { return active; },
    debugSnapshot: function () { return {mode:'native-hls', generation:generation, active:active, lastError:lastError, time:video ? video.currentTime : 0, ready:video ? video.readyState : 0}; }
  };
})(window);

/* Keep the validated HTTP HLS player as fallback; native PPAPI presents frames. */
(function (global) {
  'use strict';
  var module, container, timer, attachTimer, active = false, stopped = true, initialized = false;
  var attached = false, sharedActive = false, player = null, mode = 'direct-ppapi', last = {}, epoch = 0;
  function log(message) { if (global.AirTizenBridge) global.AirTizenBridge.log(message); }
  function size(full) {
    if (!container || !module) return;
    container.style.position = 'absolute'; container.style.left = '0'; container.style.top = '0'; container.style.zIndex = full ? '5' : '';
    container.style.width = full ? '1920px' : '1px'; container.style.height = full ? '1080px' : '1px';
    module.setAttribute('width', full ? '1920' : '1'); module.setAttribute('height', full ? '1080' : '1');
  }
  function updateShared(result) {
    /* Both sessions must receive inactive states when switching media types. */
    if (global.AirTizenShare) global.AirTizenShare.update(result && result.playback);
    if (global.AirTizenPhoto) global.AirTizenPhoto.update(result && result.photo);
    sharedActive = !!(global.AirTizenShare && global.AirTizenShare.isActive()) || !!(global.AirTizenPhoto && global.AirTizenPhoto.isActive());
    if (!player) size(active && !sharedActive);
    return sharedActive;
  }
  function schedule() { clearTimeout(timer); if (!stopped && !player) timer = setTimeout(poll, 200); }
  function poll() {
    var xhr = new XMLHttpRequest(), token = epoch;
    xhr.open('GET', 'http://127.0.0.1:45110/status', true); xhr.timeout = 2000;
    if (global.AirTizenControl) global.AirTizenControl.authorize(xhr);
    xhr.onload = function () {
      if (stopped || player || token !== epoch) return;
      var result;
      try { if (xhr.status === 200) result = JSON.parse(xhr.responseText); } catch (ignore) {}
      updateShared(result);
      schedule();
    };
    xhr.onerror = xhr.ontimeout = function () { if (!stopped && !player && token === epoch) schedule(); };
    xhr.send(null);
  }
  function detach() {
    if (module && attached) {
      module.removeEventListener('message', message);
      module.postMessage('airtizen-video-off');
    }
    attached = false; active = false; size(false);
  }
  function useFallback(reason) {
    if (player || stopped) return;
    clearTimeout(timer); clearTimeout(attachTimer); epoch++; detach();
    mode = 'native-hls-fallback'; player = global.AirTizenHLSVideo;
    log('Direct video unavailable; HLS fallback: ' + reason); player.start(updateShared);
  }
  function message(event) {
    if (stopped || player || typeof event.data !== 'string') return;
    var value; try { value = JSON.parse(event.data); } catch (ignore) { return; }
    if (value.type !== 'airtizen-native-video') return;
    last = value;
    if (value.state === 'playing') {
      active = true; size(!sharedActive);
      if (value.frames === 1) log('Direct hardware video playing');
    } else if (value.state === 'idle') { active = false; size(false); }
    else if (value.state === 'error') useFallback('code=' + value.code);
  }
  function attach() {
    if (stopped || player || attached) return;
    module = document.querySelector('#receiver-module embed'); container = document.getElementById('receiver-module');
    if (!module || !global.AirTizenEmbedded || global.AirTizenEmbedded.phase !== 'loaded') {
      if (global.AirTizenEmbedded && global.AirTizenEmbedded.phase === 'failed') { log('Direct video waiting stopped: embedded receiver failed'); return; }
      attachTimer = setTimeout(attach, 100); return;
    }
    if (typeof module.postMessage !== 'function') { useFallback('module messaging unavailable'); return; }
    module.addEventListener('message', message); attached = true;
    module.postMessage('airtizen-video-on'); log('Direct hardware video enabled');
  }
  global.AirTizenVideo = {
    start: function () {
      if (!stopped) return;
      stopped = false; player = null; last = {}; epoch++;
      if (!initialized) {
        if (global.AirTizenShare) global.AirTizenShare.start();
        if (global.AirTizenPhoto) global.AirTizenPhoto.start();
        initialized = true;
      }
      if (!global.AirTizenEmbedded || global.AirTizenEmbedded.phase === 'host') {
        mode = 'browser-mse'; player = global.AirTizenMSEVideo; player.start(updateShared);
      } else { mode = 'direct-ppapi'; attach(); schedule(); }
    },
    stop: function () {
      stopped = true; epoch++; clearTimeout(timer); clearTimeout(attachTimer); detach();
      if (player) player.stop();
      if (global.AirTizenShare) global.AirTizenShare.update(null);
      if (global.AirTizenPhoto) global.AirTizenPhoto.stop();
      sharedActive = false;
    },
    isActive: function () { return (player ? player.isActive() : active) || sharedActive; },
    debugSnapshot: function () { return {mode:mode, active:active, stopped:stopped, metrics:last, fallback:player ? player.debugSnapshot() : null}; }
  };
})(window);
