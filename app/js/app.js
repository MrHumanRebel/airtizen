(function () {
  'use strict';
  var overlay = '', developer = false, running = false, busy = false, focusIndex = 0;
  var timer, savedFocus, lastResult, pendingControl = null, artwork = -1;
  function $(id) { return document.getElementById(id); }
  function text(id, value) { var el = $(id); if (el) el.textContent = value == null ? '' : String(value); }
  function visible(el, show) { el.className = el.className.replace(/\s*\bhidden\b/g, '') + (show ? '' : ' hidden'); }
  function buttons() {
    var root = overlay ? $(overlay) : $('shell'), all = root.getElementsByTagName('button'), result = [], i;
    for (i = 0; i < all.length; i++) if (!all[i].disabled && all[i].className.indexOf('hidden') === -1) result.push(all[i]);
    return result;
  }
  function focus(index) {
    var list = buttons();
    if (!list.length) return;
    focusIndex = (index + list.length) % list.length;
    list[focusIndex].focus();
  }
  function show(id) {
    if (overlay) visible($(overlay), false);
    else savedFocus = document.activeElement;
    overlay = id; visible($(id), true); focus(0);
    if (lastResult) render(lastResult);
  }
  function close() {
    if (!overlay) return;
    visible($(overlay), false); overlay = '';
    if (savedFocus && !savedFocus.disabled) savedFocus.focus(); else focus(0);
  }
  function setDeveloper(value) {
    developer = value;
    try { localStorage.setItem('airtizen.developer', value ? '1' : '0'); } catch (ignore) {}
    text('developer-toggle', 'Developer mode: ' + (value ? 'On' : 'Off'));
    $('developer-toggle').setAttribute('aria-pressed', value ? 'true' : 'false');
    visible($('diagnostics-action'), value);
  }
  function render(res) {
    var s = res.status || {}, caps = res.capabilities || {}, track = res.nowPlaying || {};
    running = res.ok && res.service === 'running';
    lastResult = res;
    text('receiver-name', res.receiverName || 'AirTizen TV');
    text('hero-status', !res.ok ? 'Receiver unavailable' : res.service === 'failed' ? 'Receiver startup failed' : s.connected ? 'You’re connected' : running ? (caps.audioOutput === 'host_stub' ? 'Receiver test mode' : 'Audio service running') : 'Receiver stopped');
    text('hero-detail', s.detail || 'Open diagnostics to check the receiver service.');
    text('receiver-action', running ? 'Stop receiver' : 'Start receiver');
    $('receiver-action').setAttribute('data-action', running ? 'stop' : 'start');
    text('connection-state', res.ok ? (running ? 'Service connected' : 'Service stopped') : 'Service unavailable');
    $('connection-state').className = running ? 'online' : '';
    text('capability-note', !res.ok ? 'Waiting for receiver capabilities. Open diagnostics for connection errors.' : caps.mirroring ? (caps.photos ? 'Screen mirroring, music, video and photos' : 'Screen mirroring and audio') : 'Audio receiver · Screen mirroring is not available in this build.');
    if (caps.audioOutput === 'host_stub') text('capability-note', 'Host test mode · Audio output is silent; video uses the browser decoder.');
    visible($('now'), !!s.connected);
    text('device-name', s.device || 'AirPlay device');
    text('track-title', track.title || 'AirPlay audio');
    text('track-artist', [track.artist, track.album].filter(function (value) { return !!value; }).join(' · '));
    if ((track.artwork || 0) !== artwork) {
      artwork = track.artwork || 0;
      var artworkUrl = artwork ? 'http://127.0.0.1:45110/artwork/' + artwork : 'assets/airtizen-logo.png';
      $('cover-art').src = artwork && window.AirTizenControl ? window.AirTizenControl.url(artworkUrl) : artworkUrl;
    }
    text('media-state', s.audio === 'playing' ? 'Playing audio' : s.audio === 'host_stub' ? 'Audio received by host test output' : 'Connected');
    var platform = window.AirTizenPlatform || {}, embedded = window.AirTizenEmbedded || {};
    if (!res.ok && embedded.error) text('hero-detail', embedded.error);
    text('runtime-details', 'Embedded receiver: ' + (embedded.phase || 'not loaded') + '\n' + 'Platform: ' + (platform.version || 'unknown') + '   WebAssembly: ' + !!platform.webAssembly + '   MediaSource: ' + !!platform.mediaSource + '\n' + (platform.userAgent || '') + '\n' + 'mDNS: ' + s.mdns + '   RAOP: ' + s.raop + '   Pairing: ' + s.pair + '\nAudio: ' + s.audio + '   Rate: ' + s.rate + '\nLast error: ' + (s.lastError || 'none'));
    if (developer) {
      text('full-log', res.log || 'No recent log entries.');
      $('full-log').scrollTop = $('full-log').scrollHeight;
    }
  }
  function refresh() {
    if (busy) { timer = setTimeout(refresh, 1600); return; }
    busy = true;
    window.AirTizenBridge.status(function (res) {
      busy = false; render(res); timer = setTimeout(refresh, 1600);
      if (pendingControl) { var command = pendingControl; pendingControl = null; control(command); }
    });
  }
  function control(action) {
    if (busy) { pendingControl = action; return; }
    busy = true; clearTimeout(timer); $('receiver-action').disabled = true;
    window.AirTizenBridge[action](function (res) {
      busy = false; $('receiver-action').disabled = false; render(res);
      timer = setTimeout(refresh, 1600);
    });
  }
  function action(name) {
    if (name === 'start' || name === 'stop') control(name);
    else if (name === 'settings') show('settings-overlay');
    else if (name === 'developer') setDeveloper(!developer);
    else if (name === 'diagnostics' && developer) show('log-overlay');
    else if (name === 'close') close();
    else if (name === 'clear') { window.AirTizenBridge.clearLog(); text('full-log', 'Local log cleared. Service logs are retained.'); }
  }
  function key(e) {
    var code = e.keyCode || e.which, list = buttons(), i;
    for (i = 0; i < list.length; i++) if (document.activeElement === list[i]) focusIndex = i;
    if (code === 10009 || code === 27) {
      if (overlay) close();
      else if (window.AirTizenVideo && window.AirTizenVideo.isActive()) control('stop');
      else { try { window.tizen.application.getCurrentApplication().exit(); } catch (ignore) {} }
    } else if (code === 38 || code === 37) focus(focusIndex - 1);
    else if (code === 40 || code === 39) focus(focusIndex + 1);
    else if (code === 13) { if (!overlay && window.AirTizenShare && window.AirTizenShare.isActive()) { window.AirTizenShare.play(); e.preventDefault(); return; } if (document.activeElement) action(document.activeElement.getAttribute('data-action')); }
    else if (code === 406 && developer) show('log-overlay');
    else return;
    e.preventDefault();
  }
  function clock() {
    var now = new Date(), h = now.getHours(), m = now.getMinutes();
    text('clock', (h < 10 ? '0' : '') + h + ':' + (m < 10 ? '0' : '') + m);
  }
  window.onload = function () {
    try { developer = localStorage.getItem('airtizen.developer') === '1'; } catch (ignore) {}
    $('cover-art').onerror = function () { if (this.getAttribute('src') !== 'assets/airtizen-logo.png') this.src = 'assets/airtizen-logo.png'; };
    setDeveloper(developer); clock(); setInterval(clock, 10000);
    document.addEventListener('keydown', key, false);
    document.addEventListener('click', function (e) {
      if (e.target && e.target.tagName === 'BUTTON') action(e.target.getAttribute('data-action'));
    }, false);
    try { window.tizen.tvinputdevice.registerKey('ColorF3Blue'); } catch (ignore2) {}
    focus(0); refresh();
    if (window.AirTizenVideo) window.AirTizenVideo.start();
  };
  window.onunload = function () { clearTimeout(timer); if (window.AirTizenVideo) window.AirTizenVideo.stop(); };
})();
