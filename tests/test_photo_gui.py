#!/usr/bin/env python3
"""Actual AirPlay photo upload/cache, loopback fetch and decoded browser images."""
import functools
import http.server
import json
import os
import secrets
from pathlib import Path
import plistlib
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
import zlib
from browser_support import launch_browser
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[1]
SESSION = 'photo-session-1'
CONTROL_TOKEN = secrets.token_hex(32)


def png():
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 32, 24, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress((b'\0' + b'\xff\0\0' * 32) * 24)) + chunk(b'IEND', b'')


def request(path, body=None, headers=None, method=None, port=5000):
    if port == 45110: headers = dict(headers or {}, **{'X-AirTizen-Control': CONTROL_TOKEN})
    req = urllib.request.Request('http://127.0.0.1:' + str(port) + path, data=body, headers=headers or {}, method=method)
    return urllib.request.urlopen(req, timeout=5)


def photo(body, key='first', action=None, session=SESSION, transition=None):
    headers = {'X-Apple-AssetKey': key, 'X-Apple-Session-ID': session}
    if action: headers['X-Apple-AssetAction'] = action
    if transition: headers['X-Apple-Transition'] = transition
    with request('/photo', body, headers, 'PUT') as response:
        return response.status


def status():
    with request('/status', port=45110) as response:
        return json.load(response)['photo']


def frame(sock):
    data = b''
    while b'\r\n\r\n' not in data:
        part = sock.recv(4096)
        if not part: raise EOFError('Reverse channel closed before event')
        data += part
    head, body = data.split(b'\r\n\r\n', 1)
    length = int([line.split(b':', 1)[1] for line in head.split(b'\r\n') if line.lower().startswith(b'content-length:')][0])
    while len(body) < length:
        part = sock.recv(length - len(body))
        if not part: raise EOFError('Truncated reverse event')
        body += part
    return head, body


class PhotoShare(unittest.TestCase):
    def test_photo_decode_cache_session_bounds_and_remote_stop(self):
        for port in (5000, 45110):
            with socket.socket() as sock:
                sock.settimeout(.2)
                self.assertNotEqual(sock.connect_ex(('127.0.0.1', port)), 0)
        with tempfile.TemporaryFile() as log:
            process = subprocess.Popen([str(ROOT / 'native/build/airtizen_runtime')], stdout=log, stderr=log, env=dict(os.environ, AIRTIZEN_CONTROL_TOKEN=CONTROL_TOKEN))
            server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(ROOT)))
            threading.Thread(target=server.serve_forever, daemon=True).start()
            try:
                for _ in range(50):
                    if process.poll() is not None: self.fail('Receiver failed to start')
                    try: status(); break
                    except OSError: time.sleep(.1)
                for supplied in (None, '1', '0' * 64):
                    headers = {'Origin': 'null'}
                    if supplied: headers['X-AirTizen-Control'] = supplied
                    req = urllib.request.Request('http://127.0.0.1:45110/status', headers=headers)
                    with self.assertRaises(urllib.error.HTTPError) as failure: urllib.request.urlopen(req, timeout=2)
                    self.assertEqual(failure.exception.code, 403)
                # CORS preflight discloses no data and does not require the secret value.
                with urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:45110/status', method='OPTIONS', headers={'Origin':'null','Access-Control-Request-Headers':'X-AirTizen-Control'}), timeout=2) as preflight:
                    self.assertEqual(preflight.status, 204)
                with sync_playwright() as playwright:
                    browser = launch_browser(playwright)
                    page = browser.new_page(viewport={'width': 1920, 'height': 1080})
                    errors = []
                    # Exercise the real token bootstrap against the host receiver.
                    # Desktop Chrome cannot execute NaCl; fixed randomness here is a test fixture only.
                    page.add_init_script('window.crypto.getRandomValues = function(a) { var hex=' + json.dumps(CONTROL_TOKEN) + '; for(var i=0;i<a.length;i++) a[i]=parseInt(hex.substr(i*2,2),16); return a; }; window.tizen={systeminfo:{getPropertyValue:function(n,ok){ok({ipAddress:"192.0.2.20",macAddress:"02:11:22:33:44:66"});}}};')
                    page.on('pageerror', lambda error: errors.append(str(error)))
                    page.set_content('<iframe name="tv" sandbox="allow-scripts" style="width:1920px;height:1080px" src="http://127.0.0.1:' + str(server.server_port) + '/app/index.html"></iframe>')
                    ui = page.frame(name='tv')
                    expect(ui.get_by_role('heading', name='Receiver test mode')).to_be_visible()
                    self.assertEqual(photo(png()), 200)
                    expect(ui.locator('#photo-overlay')).to_be_visible()
                    expect(ui.locator('#photo-b')).to_have_js_property('naturalWidth', 32)
                    ui.wait_for_function('fetch("http://127.0.0.1:45110/status", {headers:{"X-AirTizen-Control":AirTizenControl.header()}}).then(r=>r.json()).then(s=>s.photo.ready)')
                    first = status()
                    # This exercises the photo-specific >1 MiB request allowance.
                    jpeg = (ROOT / 'tests/fixtures/photo.jpg').read_bytes() + b'\0' * (1024 * 1024)
                    self.assertEqual(photo(jpeg, 'second', 'cacheOnly'), 200)
                    self.assertEqual(status()['generation'], first['generation'])
                    with self.assertRaises(urllib.error.HTTPError) as failure: photo(b'', 'second', 'displayCached', 'other-session')
                    self.assertEqual(failure.exception.code, 412)
                    self.assertEqual(photo(b'', 'second', 'displayCached', transition='Dissolve'), 200)
                    expect(ui.locator('#photo-a')).to_have_js_property('naturalWidth', 64)
                    expect(ui.locator('#photo-a')).to_have_css('opacity', '1')
                    with request('/photo/' + str(status()['generation']), port=45110) as image:
                        self.assertEqual(image.headers['Content-Type'], 'image/jpeg')
                        self.assertEqual(image.read(), jpeg)
                    with self.assertRaises(urllib.error.HTTPError) as failure: request('/photo/' + str(first['generation']), port=45110)
                    self.assertEqual(failure.exception.code, 404)
                    for bad in (b'<svg onload="alert(1)"/>', png()[:24]):
                        with self.assertRaises(urllib.error.HTTPError) as failure: photo(bad)
                        self.assertEqual(failure.exception.code, 400)
                    with socket.create_connection(('127.0.0.1', 5000), timeout=3) as reverse:
                        reverse.sendall(('POST /reverse HTTP/1.1\r\nHost: localhost\r\nX-Apple-Session-ID: ' + SESSION + '\r\nContent-Length: 0\r\n\r\n').encode())
                        head, _ = frame(reverse); self.assertIn(b'101', head)
                        # Let its polling thread observe the active photo session.
                        page.wait_for_timeout(300)
                        page.keyboard.press('Escape')
                        expect(ui.locator('#photo-overlay')).not_to_be_visible()
                        # Stopping the whole app closes reverse connections; exercise
                        # protocol /stop separately after restarting the receiver.
                    ui.get_by_role('button', name='Start receiver', exact=True).click()
                    expect(ui.get_by_role('button', name='Stop receiver', exact=True)).to_be_visible()
                    self.assertEqual(photo(png()), 200)
                    expect(ui.locator('#photo-overlay')).to_be_visible()
                    with socket.create_connection(('127.0.0.1', 5000), timeout=3) as reverse:
                        reverse.sendall(('POST /reverse HTTP/1.1\r\nX-Apple-Session-ID: ' + SESSION + '\r\nContent-Length: 0\r\n\r\n').encode())
                        frame(reverse)
                        with request('/stop', b'', method='POST'): pass
                        _, body = frame(reverse)
                        event = plistlib.loads(body)
                        self.assertEqual((event['category'], event['state']), ('photo', 'stopped'))
                        self.assertGreater(event['sessionID'], 0)
                        reverse.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n')
                    expect(ui.locator('#photo-overlay')).not_to_be_visible()
                    with self.assertRaises(urllib.error.HTTPError) as failure: photo(b'', 'first', 'displayCached')
                    self.assertEqual(failure.exception.code, 412)
                    # Header dimensions alone do not prove a decodable photo.
                    broken = bytes([255,216,255,192,0,17,8,0,2,0,3,3,1,17,0,2,17,0,3,17,0,255,217])
                    self.assertEqual(photo(broken, 'broken'), 200)
                    ui.wait_for_function('fetch("http://127.0.0.1:45110/status", {headers:{"X-AirTizen-Control":AirTizenControl.header()}}).then(r=>r.json()).then(s=>s.photo.failed)')
                    self.assertEqual(photo(png(), 'recovery'), 200)
                    ui.wait_for_function('fetch("http://127.0.0.1:45110/status", {headers:{"X-AirTizen-Control":AirTizenControl.header()}}).then(r=>r.json()).then(s=>s.photo.ready && !s.photo.failed)')
                    expect(ui.locator('#photo-overlay')).to_be_visible()
                    self.assertEqual(errors, [])
                    browser.close()
            finally:
                process.terminate(); process.wait(timeout=5); server.shutdown(); server.server_close()
            self.assertEqual(process.returncode, 0)


if __name__ == '__main__': unittest.main(verbosity=2)
