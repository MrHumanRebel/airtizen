/* Start the native receiver shipped inside this WGT; no separate ServiceApp. */
(function (global) {
  'use strict';
  var state = { phase: 'host', error: '' }, module, token = '';
  global.AirTizenControl = {
    header: function () { return token || '1'; },
    authorize: function (xhr) { if (token) xhr.setRequestHeader('X-AirTizen-Control', token); },
    url: function (value) { return token ? value + (value.indexOf('?') === -1 ? '?' : '&') + 'token=' + token : value; }
  };
  function fail(message) { state.phase = 'failed'; state.error = message; }
  function validIp(ip) {
    var pieces = String(ip || '').split('.'), i;
    if (pieces.length !== 4) return false;
    for (i = 0; i < 4; i++) if (!/^\d{1,3}$/.test(pieces[i]) || Number(pieces[i]) > 255) return false;
    return Number(pieces[0]) > 0 && Number(pieces[0]) !== 127 && Number(pieces[0]) < 224;
  }
  function identity() {
    var saved, bytes = new Uint8Array(6), i, result = '';
    try { saved = localStorage.getItem('airtizen.receiverId'); } catch (ignore) {}
    if (/^[0-9A-F]{12}$/.test(saved || '')) return saved;
    if (global.crypto && global.crypto.getRandomValues) global.crypto.getRandomValues(bytes);
    else for (i = 0; i < 6; i++) bytes[i] = Math.floor(Math.random() * 256);
    bytes[0] = (bytes[0] | 2) & 254; /* Local unicast identity, not an authentication key. */
    for (i = 0; i < bytes.length; i++) result += ('0' + bytes[i].toString(16)).slice(-2).toUpperCase();
    try { localStorage.setItem('airtizen.receiverId', result); } catch (ignore2) {}
    return result;
  }
  function launch(ip) {
    var random = new Uint8Array(32), index;
    if (!global.crypto || !global.crypto.getRandomValues) { fail('Secure random generation is unavailable on this TV.'); return; }
    try { global.crypto.getRandomValues(random); }
    catch (error) { fail('Secure receiver initialization failed.'); return; }
    for (index = 0; index < random.length; index++) token += ('0' + random[index].toString(16)).slice(-2);
    var container = document.createElement('div');
    container.id = 'receiver-module'; container.style.position = 'absolute';
    container.style.width = '1px'; container.style.height = '1px';
    container.addEventListener('load', function () { state.phase = 'loaded'; }, true);
    container.addEventListener('error', function () { fail('The bundled native receiver could not load: ' + (module.lastError || 'NaCl unavailable')); }, true);
    container.addEventListener('crash', function () { fail('The bundled native receiver stopped unexpectedly.'); }, true);
    module = document.createElement('embed');
    module.setAttribute('type', 'application/x-nacl'); module.setAttribute('src', 'receiver/receiver.nmf');
    module.setAttribute('width', '1'); module.setAttribute('height', '1');
    module.setAttribute('receiver-token', token);
    module.setAttribute('receiver-ip', ip); module.setAttribute('receiver-id', identity());
    module.setAttribute('receiver-name', 'AirTizen TV');
    state.phase = 'loading'; container.appendChild(module); document.body.appendChild(container);
  }
  function start() {
    var names = ['ETHERNET_NETWORK', 'WIFI_NETWORK'];
    if (!global.tizen || !global.tizen.systeminfo) return;
    state.phase = 'network';
    function query(index) {
      var finished = false, timer;
      if (index === names.length) { fail('No active TV IPv4 network. Connect the TV to the same LAN as your Apple device.'); return; }
      function next(data) {
        if (finished) return; finished = true; clearTimeout(timer);
        if (data && validIp(data.ipAddress)) launch(data.ipAddress); else query(index + 1);
      }
      timer = setTimeout(function () { next(null); }, 2500);
      try { global.tizen.systeminfo.getPropertyValue(names[index], next, function () { next(null); }); }
      catch (error) { next(null); }
    }
    query(0);
  }
  global.AirTizenEmbedded = state;
  document.addEventListener('DOMContentLoaded', start, false);
})(window);
