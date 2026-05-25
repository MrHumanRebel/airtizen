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
        cb({ ok:false, error:'runtime HTTP ' + xhr.status });
        return;
      }
      try { cb(JSON.parse(xhr.responseText || '{}')); }
      catch (ex) { cb({ ok:false, error:'invalid runtime JSON: ' + ex.message }); }
    };

    try {
      xhr.open(method, runtimeBase + path, true);
      xhr.setRequestHeader('Accept', 'application/json');
      if (payload) xhr.setRequestHeader('Content-Type', 'application/json');
      xhr.send(payload ? JSON.stringify(payload) : null);
    } catch (ex2) {
      done = true;
      clearTimeout(timer);
      cb({ ok:false, error:ex2.message });
    }
  }

  function NativeBridge(){
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
      this.runtimeReachable = true;
      this.lastStatus = copyStatus(res.status);
      if (res.log) {
        var remoteLines = String(res.log).split('\n');
        for (var i=0; i<remoteLines.length; i++) {
          if (remoteLines[i]) log('[runtime] ' + remoteLines[i]);
        }
      }
      return { ok:true, status:this.lastStatus, service:res.service || "running", protocols:res.protocols || {} };
    }

    this.runtimeReachable = false;
    this.lastStatus = copyStatus({
      mdns: 'offline',
      raop: 'offline',
      pair: 'idle',
      audio: 'idle',
      connected: false,
      detail: 'No AirTizen native/service runtime answered on 127.0.0.1:45110. The WGT web UI alone cannot publish Bonjour/mDNS or run the RAOP receiver.'
    });
    if (res && res.error) log(source + ' failed: ' + res.error);
    return { ok:false, status:this.lastStatus, error:res && res.error ? res.error : 'runtime unavailable' };
  };

  NativeBridge.prototype.start = function(cb){
    var self = this;
    log('start requested');
    xhrJson('POST', '/start', { name:'AirTizen TV' }, function(res){
      var result = self.applyRuntimeResult(res, 'start');
      if (cb) cb(result);
    });
  };

  NativeBridge.prototype.status = function(cb){
    var self = this;
    xhrJson('GET', '/status', null, function(res){
      var result = self.applyRuntimeResult(res, 'status');
      result.log = logLines.join('\n');
      if (cb) cb(result);
    });
  };

  NativeBridge.prototype.clearLog = function(){ logLines = []; log('log cleared'); };
  NativeBridge.prototype.log = log;
  global.AirTizenBridge = new NativeBridge();
})(window);
