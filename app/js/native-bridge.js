(function (global) {
  function now(){ return new Date().toISOString(); }
  var logLines = [];
  var runtimeBase = 'http://127.0.0.1:45110';
  var pollTimeoutMs = 1800;

  function log(msg){
    logLines.push(now() + ' ' + msg);
    if (logLines.length > 220) logLines.shift();
  }

  function copyStatus(src) {
    var dst = {
      mdns: 'stopped',
      raop: 'stopped',
      pair: 'idle',
      audio: 'idle',
      connected: false,
      device: '',
      codec: '—',
      rate: '—',
      buffer: '—',
      detail: 'AirTizen native runtime is not reachable.'
    };
    if (!src) return dst;
    for (var k in src) {
      if (src.hasOwnProperty(k)) dst[k] = src[k];
    }
    return dst;
  }

  function xhrJson(method, path, payload, cb) {
    var xhr;
    try { xhr = new XMLHttpRequest(); }
    catch (ex) { cb({ ok:false, error:'XMLHttpRequest unavailable: ' + ex.message }); return; }

    var done = false;
    var timer = setTimeout(function(){
      if (done) return;
      done = true;
      try { xhr.abort(); } catch (ignore) {}
      cb({ ok:false, error:'runtime timeout' });
    }, pollTimeoutMs);

    xhr.onreadystatechange = function(){
      if (xhr.readyState !== 4 || done) return;
      done = true;
      clearTimeout(timer);
      if (xhr.status < 200 || xhr.status >= 300) {
        var reason = '';
        try {
          var failure = JSON.parse(xhr.responseText || '{}');
          var errorCode = failure && (failure.error || (failure.status && failure.status.lastError));
          // Show only machine error identifiers, never arbitrary response data
          // or request headers (which contain the private control nonce).
          if (typeof errorCode === 'string' && /^[a-z_][a-z0-9_]{0,95}$/.test(errorCode)) reason = ' (' + errorCode + ')';
        } catch (ignoreJSON) {}
        cb({ ok:false, error:'runtime HTTP ' + xhr.status + reason });
        return;
      }
      var parsed;
      try { parsed = JSON.parse(xhr.responseText || '{}'); }
      catch (ex) { cb({ ok:false, error:'invalid runtime JSON: ' + ex.message }); return; }
      cb(parsed);
    };

    try {
      xhr.open(method, runtimeBase + path, true);
      xhr.setRequestHeader('Accept', 'application/json');
      if (method !== 'POST' && global.AirTizenControl) global.AirTizenControl.authorize(xhr);
      if (method === 'POST') xhr.setRequestHeader('X-AirTizen-Control', global.AirTizenControl ? global.AirTizenControl.header() : '1');
      if (payload) xhr.setRequestHeader('Content-Type', 'application/json');
      xhr.send(payload ? JSON.stringify(payload) : null);
    } catch (ex2) {
      done = true;
      clearTimeout(timer);
      cb({ ok:false, error:ex2.message });
    }
  }

  function NativeBridge(){
    this.remoteLogs = [];
    this.runtimeReachable = false;
    this.lastStatus = copyStatus({
      mdns: 'checking',
      raop: 'checking',
      detail: 'Checking AirTizen native runtime on 127.0.0.1:45110…'
    });
    log('AirTizen bridge loaded');
    log('Native runtime endpoint: ' + runtimeBase);
  }

  NativeBridge.prototype.applyRuntimeResult = function(res, source){
    if (res && res.ok && res.status) {
      this.lastFailure = null;
      this.runtimeReachable = true;
      this.lastStatus = copyStatus(res.status);
      if (res.logs && res.logs instanceof Array) this.remoteLogs = res.logs.slice(-32);
      if (res.log) {
        var remoteLines = String(res.log).split('\n');
        for (var i=0; i<remoteLines.length; i++) {
          if (remoteLines[i]) log('[runtime] ' + remoteLines[i]);
        }
      }
      return { ok:true, status:this.lastStatus, service:res.service || "running", discovery:res.discovery || {}, protocols:res.protocols || {}, nowPlaying:res.nowPlaying || {}, capabilities:res.capabilities || {}, audio:res.audio || {}, video:res.video || {}, receiverName:res.receiverName || "AirTizen TV" };
    }

    this.runtimeReachable = false;
    this.lastStatus = copyStatus({
      mdns: 'offline',
      raop: 'offline',
      pair: 'idle',
      audio: 'idle',
      connected: false,
      detail: 'The receiver service is unavailable. Check the bundled receiver loader and network status in diagnostics.'
    });
    var failure = res && res.error ? res.error : 'runtime unavailable';
    if (this.lastFailure !== failure) log(source + ' failed: ' + failure);
    this.lastFailure = failure;
    return { ok:false, status:this.lastStatus, error:res && res.error ? res.error : 'runtime unavailable' };
  };

  NativeBridge.prototype.start = function(cb){
    var self = this;
    log('start requested');
    xhrJson('POST', '/start', null, function(res){
      var result = self.applyRuntimeResult(res, 'start');
      if (cb) cb(result);
    });
  };

  NativeBridge.prototype.stop = function(cb){
    var self = this;
    log('stop requested');
    xhrJson('POST', '/stop', null, function(res){
      if (cb) cb(self.applyRuntimeResult(res, 'stop'));
    });
  };
  NativeBridge.prototype.discovery = function(profile, cb){
    var self = this;
    if (profile !== 'native' && profile !== 'uxplay-test') { if (cb) cb({ok:false,error:'invalid discovery profile'}); return; }
    log('discovery profile requested: ' + profile);
    xhrJson('POST', '/discovery/' + profile, null, function(res){
      if (cb) cb(self.applyRuntimeResult(res, 'discovery'));
    });
  };

  NativeBridge.prototype.status = function(cb){
    var self = this;
    xhrJson('GET', '/status', null, function(res){
      var result = self.applyRuntimeResult(res, 'status');
      result.log = (self.remoteLogs || []).concat(logLines).join('\n');
      if (cb) cb(result);
    });
  };

  NativeBridge.prototype.clearLog = function(){ logLines = []; log('log cleared'); };
  NativeBridge.prototype.log = log;
  global.AirTizenBridge = new NativeBridge();
})(window);
