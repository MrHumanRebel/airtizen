(function (global) {
  function now(){ return new Date().toISOString(); }
  var logLines = [];
  function log(msg){
    logLines.push(now() + ' ' + msg);
    if (logLines.length > 180) logLines.shift();
  }
  function NativeBridge(){
    this.lastStatus = {
      mdns: 'starting', raop: 'starting', pair: 'idle', audio: 'idle', connected: false,
      device: '', codec: '—', rate: '—', buffer: '—', detail: 'Native bridge initialising'
    };
    log('AirTizen bridge loaded');
  }
  NativeBridge.prototype.start = function(cb){
    log('start requested');
    this.lastStatus.mdns = 'advertising';
    this.lastStatus.raop = 'listening';
    this.lastStatus.detail = 'Receiver stack requested. If native core is packaged, AirTizen will publish AirPlay/RAOP now.';
    if (cb) cb({ok:true, status:this.lastStatus});
  };
  NativeBridge.prototype.status = function(cb){
    if (cb) cb({ok:true, status:this.lastStatus, log:logLines.join('\n')});
  };
  NativeBridge.prototype.clearLog = function(){ logLines = []; log('log cleared'); };
  NativeBridge.prototype.log = log;
  global.AirTizenBridge = new NativeBridge();
})(window);
