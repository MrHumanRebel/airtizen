/* Legacy AirPlay photo uploads/cache are served by the receiver inside this WGT. */
(function (global) {
  'use strict';
  var images, overlay, generation = -1, current = 0, active = false;
  function report(value, failed) {
    var xhr = new XMLHttpRequest();
    xhr.open('POST', 'http://127.0.0.1:45110/photo/report?generation=' + value + '&failed=' + (failed ? 1 : 0), true);
    xhr.timeout = 1500; xhr.setRequestHeader('X-AirTizen-Control', '1'); xhr.send(null);
  }
  function reset() {
    var i;
    generation = -1; active = false;
    if (overlay) overlay.className = 'hidden';
    if (images) for (i = 0; i < images.length; i++) {
      images[i].onload = images[i].onerror = null;
      images[i].removeAttribute('src'); images[i].style.opacity = '0';
    }
  }
  global.AirTizenPhoto = {
    start: function () {
      overlay = document.getElementById('photo-overlay');
      images = [document.getElementById('photo-a'), document.getElementById('photo-b')];
      reset();
    },
    update: function (state) {
      var next, token, i;
      if (!state || !state.active) { if (generation !== -1) reset(); return; }
      active = true;
      if (state.generation === generation) return;
      generation = state.generation; token = generation; next = 1 - current;
      for (i = 0; i < images.length; i++) images[i].style.transition = state.dissolve ? 'opacity 350ms ease' : 'none';
      images[next].onload = function () {
        if (generation !== token) return;
        overlay.className = ''; images[next].style.opacity = '1'; images[current].style.opacity = '0';
        current = next; report(token, false);
      };
      images[next].onerror = function () {
        if (generation !== token) return;
        report(token, true);
        if (global.AirTizenBridge) global.AirTizenBridge.log('The received photo could not be decoded by this TV');
      };
      images[next].src = 'http://127.0.0.1:45110/photo/' + token;
    },
    isActive: function () { return active; },
    stop: reset
  };
})(window);
