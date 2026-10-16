(function (global) {
  'use strict';
  var video, overlay, hint, state, command = -1, lastReport = 0;
  function log(message) { if (global.AirTizenBridge) global.AirTizenBridge.log(message); }
  function report() {
    if (!state || !state.active || new Date().getTime() - lastReport < 400) return;
    lastReport = new Date().getTime();
    var position = isFinite(video.currentTime) ? video.currentTime : 0;
    var duration = isFinite(video.duration) ? video.duration : 0;
    var ready = video.readyState >= 2 && (!video.paused || state.rate === 0);
    var xhr = new XMLHttpRequest();
    xhr.open('POST', 'http://127.0.0.1:45110/playback/report?generation=' + state.generation + '&position=' + position.toFixed(3) + '&duration=' + duration.toFixed(3) + '&ready=' + (ready ? 1 : 0) + '&failed=' + (video.error ? 1 : 0), true);
    xhr.timeout = 1500; xhr.setRequestHeader('X-AirTizen-Control', '1'); xhr.send(null);
  }
  function play() {
    var result;
    if (!video || !state || !state.active) return;
    try { result = video.play(); }
    catch (error) { hint.className = ''; return; }
    if (result && result.catch) result.catch(function () { hint.className = ''; log('Press OK to allow video playback'); });
  }
  function apply() {
    if (!state || video.readyState < 1) return;
    if (state.command !== command) {
      if (state.seek >= 0 && isFinite(video.duration)) video.currentTime = Math.min(state.seek, video.duration);
      command = state.command;
    }
    if (state.rate === 0) video.pause(); else if (video.paused && !video.ended) play();
  }
  global.AirTizenShare = {
    start: function () {
      video = document.getElementById('shared-video'); overlay = document.getElementById('share-overlay'); hint = document.getElementById('share-play-hint');
      video.addEventListener('loadedmetadata', function () {
        if (state && isFinite(video.duration) && state.fraction > 0) video.currentTime = video.duration * state.fraction;
        apply(); report();
      });
      video.addEventListener('playing', function () { hint.className = 'hidden'; report(); });
      video.addEventListener('error', function () { log('Shared media could not be decoded by this TV'); report(); });
      video.addEventListener('timeupdate', report); video.addEventListener('pause', report); video.addEventListener('ended', report);
    },
    update: function (next) {
      var changed = !state || !next || state.generation !== next.generation;
      state = next;
      if (!state || !state.active) {
        if (video && video.getAttribute('src')) { video.pause(); video.removeAttribute('src'); video.load(); }
        if (overlay) overlay.className = 'hidden'; return;
      }
      overlay.className = '';
      if (changed) { command = -1; lastReport = 0; hint.className = 'hidden'; video.src = state.url; video.load(); log('Shared media session started'); }
      apply();
    },
    play: play,
    isActive: function () { return !!(state && state.active); }
  };
})(window);
