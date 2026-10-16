/* Capability evidence only: presence of an API does not prove usable sockets or playback. */
(function (global) {
  'use strict';
  var version = 'unknown';
  try { version = global.tizen.systeminfo.getCapability('http://tizen.org/feature/platform.version'); } catch (ignore) {}
  global.AirTizenPlatform = {
    version: version,
    userAgent: navigator.userAgent,
    webAssembly: typeof global.WebAssembly !== 'undefined',
    mediaSource: typeof global.MediaSource !== 'undefined'
  };
})(window);
