(function (global) {
  'use strict';
  var video, overlay, media, source, url, timer, epoch = 0, generation = -1, next = 0, latest = 0;
  var active = false, stopped = true, initialized = false, updateShared, lastError = '', base = 'http://127.0.0.1:45110';
  function log(message) { if (global.AirTizenBridge) global.AirTizenBridge.log(message); }
  function schedule(delay) { clearTimeout(timer); if (!stopped) timer = setTimeout(poll, delay); }
  function request(path, binary, token, cb) {
    var xhr = new XMLHttpRequest(), done = false;
    xhr.open('GET', base + path, true); xhr.timeout = 2000;
    if (global.AirTizenControl) global.AirTizenControl.authorize(xhr);
    if (binary) xhr.responseType = 'arraybuffer';
    function finish(error) {
      if (done) return; done = true;
      if (stopped || token !== epoch) return;
      if (error || xhr.status !== 200) { cb(null, xhr.status); return; }
      if (binary) cb(xhr.response, Number(xhr.getResponseHeader('X-AirTizen-Generation')));
      else { var data; try { data = JSON.parse(xhr.responseText); } catch (ignore) { cb(null); return; } cb(data); }
    }
    xhr.onload = function () { finish(false); }; xhr.onerror = xhr.ontimeout = function () { finish(true); };
    xhr.send(null);
  }
  function dispose() {
    epoch++; clearTimeout(timer); generation = -1; source = null; media = null; active = false;
    if (video) { video.pause(); video.removeAttribute('src'); video.load(); }
    if (url) { global.URL.revokeObjectURL(url); url = null; }
    if (overlay) overlay.className = 'hidden';
  }
  function failed(message) {
    if (message !== lastError) log(message); lastError = message;
    dispose(); schedule(1500);
  }
  function append(data) {
    try { source.appendBuffer(data); }
    catch (error) { failed('Video buffer error: ' + error.message); }
  }
  function begin(state) {
    var token;
    dispose(); token = epoch;
    if (!global.MediaSource || !global.MediaSource.isTypeSupported(state.mime)) {
      failed('This TV browser does not support the received H.264 MP4 format'); return;
    }
    generation = state.generation; next = state.start; latest = state.latest;
    media = new global.MediaSource(); url = global.URL.createObjectURL(media); video.src = url;
    media.addEventListener('sourceopen', function () {
      if (stopped || token !== epoch) return;
      try {
        source = media.addSourceBuffer(state.mime);
        source.addEventListener('error', function () { if (token === epoch) failed('H.264 playback failed'); });
        source.addEventListener('updateend', function () {
          if (token !== epoch) return;
          if (video.buffered.length) {
            var end = video.buffered.end(video.buffered.length - 1);
            if (video.currentTime < video.buffered.start(0) || end - video.currentTime > 1) video.currentTime = Math.max(video.buffered.start(0), end - 0.15);
            if (!active) { active = true; overlay.className = ''; log('H.264 mirroring playback started'); }
            var playing = video.play(); if (playing && playing.catch) playing.catch(function () {});
            if (video.currentTime > 20 && video.buffered.start(0) < video.currentTime - 15) {
              source.remove(0, video.currentTime - 10); return;
            }
          }
          schedule(0);
        });
      } catch (error) { failed('Cannot create video decoder: ' + error.message); return; }
      request('/video/init', true, token, function (data, receivedGeneration) {
        if (!data || receivedGeneration !== generation) { failed('Video initialization changed'); return; }
        append(data);
      });
    });
  }
  function poll() {
    var token = epoch;
    request('/status', false, token, function (result) {
      if (updateShared(result)) { if (active || generation !== -1) dispose(); schedule(100); return; }
      var state = result && result.video;
      if (!state || !state.available) { if (active || generation !== -1) dispose(); schedule(200); return; }
      if (state.generation !== generation) { begin(state); return; }
      latest = state.latest;
      if (!source || source.updating) { schedule(30); return; }
      if (next > latest) { schedule(30); return; }
      request('/video/segment/' + next, true, token, function (data, receivedGeneration) {
        if (!data || receivedGeneration !== generation) { dispose(); schedule(100); return; }
        next++; append(data);
      });
    });
  }
  global.AirTizenMSEVideo = {
    start: function (shared) {
      if (!stopped) return;
      stopped = false; updateShared = shared;
      video = document.getElementById('mirror-video'); overlay = document.getElementById('video-overlay');
      if (!initialized) video.addEventListener('error', function () { if (!stopped && generation !== -1) failed('Video decoder error'); });
      initialized = true;
      schedule(0);
    },
    stop: function () { stopped = true; dispose(); },
    isActive: function () { return active; },
    debugSnapshot: function () { return {mode:'browser-mse', active:active, generation:generation, lastError:lastError}; }
  };
})(window);
