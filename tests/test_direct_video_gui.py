#!/usr/bin/env python3
"""Real local media APIs plus a simulated PPAPI message boundary, not a GPU test."""
import base64
import functools
import http.server
import json
import os
from pathlib import Path
import plistlib
import re
import secrets
import socket
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request

from browser_support import launch_browser, require_media_support
from media_server import MediaHandler
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[1]
BASE = 'http://127.0.0.1:45110'


class DirectVideo(unittest.TestCase):
    def start_receiver(self, token):
        for port in (5000, 45110):
            with socket.socket() as sock:
                sock.settimeout(.2)
                self.assertNotEqual(sock.connect_ex(('127.0.0.1', port)), 0)
        self.token = token
        self.log = tempfile.TemporaryFile()
        environment = dict(os.environ)
        environment.pop('AIRTIZEN_CONTROL_TOKEN', None)
        if token:
            environment['AIRTIZEN_CONTROL_TOKEN'] = token
        self.process = subprocess.Popen([str(ROOT/'native/build/airtizen_video_test'), '--serve'],
                                        stdout=self.log, stderr=self.log, env=environment)
        self.addCleanup(self.stop_receiver)
        for _ in range(80):
            if self.process.poll() is not None:
                self.log.seek(0)
                self.fail(self.log.read().decode(errors='replace'))
            try:
                with self.get('/status') as response:
                    if json.load(response)['video']['latest'] >= 30:
                        return
            except OSError:
                pass
            time.sleep(.05)
        self.fail('Native video fixture did not become ready')

    def stop_receiver(self):
        self.process.terminate()
        self.process.wait(timeout=5)
        self.log.close()
        self.assertEqual(self.process.returncode, 0)

    def get(self, path, *, authorize=True, method='GET', headers=None):
        fields = dict(headers or {})
        if authorize:
            fields['X-AirTizen-Control'] = self.token or '1'
        return urllib.request.urlopen(urllib.request.Request(BASE+path, method=method, headers=fields), timeout=5)

    def assert_http_error(self, code, path, **kwargs):
        with self.assertRaises(urllib.error.HTTPError) as failure:
            self.get(path, **kwargs)
        self.assertEqual(failure.exception.code, code)

    def test_authenticated_hls_fragments_decode_and_expire(self):
        self.check_hls(secrets.token_hex(32))

    def test_host_hls_uris_have_no_empty_token(self):
        self.check_hls('')

    def check_hls(self, token):
        self.start_receiver(token)
        if token:
            self.assert_http_error(403, '/video/live.m3u8', authorize=False)
            self.assert_http_error(403, '/video/live.m3u8?token='+'0'*64, authorize=False)
            self.assert_http_error(403, '/video/live.m3u8', headers={'Origin':'https://untrusted.example'})
        with self.get('/video/live.m3u8') as response:
            self.assertEqual(response.headers['Content-Type'], 'application/vnd.apple.mpegurl')
            manifest = response.read().decode()
        init = re.search(r'#EXT-X-MAP:URI="([^"]+)"', manifest)[1]
        segments = [line for line in manifest.splitlines() if line.startswith('/video/hls/')]
        self.assertTrue(segments)
        self.assertRegex(init, r'^/video/hls/[0-9]+/init')
        for path in [init]+segments:
            self.assertEqual('?token=' in path, bool(token))
            if token:
                self.assert_http_error(403, path.split('?')[0], authorize=False)
        data = b''
        for path in [init]+segments:
            with self.get(path, authorize=False) as response:
                self.assertEqual(response.headers['Content-Type'], 'video/mp4')
                data += response.read()
        # Decode bytes assembled from the actual manifest, init and segment API.
        # Chrome is not used to claim support for Tizen's native HLS engine.
        with sync_playwright() as playwright:
            browser = launch_browser(playwright)
            try:
                page = browser.new_page()
                require_media_support(page)
                page.set_content('<video id="decoded" muted autoplay></video>')
                page.evaluate("""encoded => {
                    const data=Uint8Array.from(atob(encoded), c=>c.charCodeAt(0));
                    const video=document.querySelector('video');
                    video.src=URL.createObjectURL(new Blob([data],{type:'video/mp4'}));
                    video.play();
                }""", base64.b64encode(data).decode())
                video = page.locator('#decoded')
                expect(video).to_have_js_property('videoWidth', 320)
                expect(video).to_have_js_property('videoHeight', 180)
                page.wait_for_function('document.querySelector("video").currentTime > 0.1')
            finally:
                browser.close()
        self.assert_http_error(410, '/video/hls/0/1/15')
        generation = int(re.search(r'/hls/([0-9]+)/', init)[1])
        for suffix in ('1/14', '2/16', '1/18446744073709551615'):
            self.assert_http_error(410, '/video/hls/'+str(generation)+'/'+suffix)
        with self.get('/stop', method='POST'):
            pass
        # A cached playlist must never fetch another stream's initialization.
        for path in [init]+segments:
            self.assert_http_error(410, path, authorize=False)
        self.assert_http_error(503, '/video/live.m3u8')
        with self.get('/start', method='POST'):
            pass
        for _ in range(80):
            with self.get('/status') as response:
                state=json.load(response)['video']
            if state['latest'] >= 30:
                break
            time.sleep(.05)
        else:
            self.fail('Video did not recover after receiver restart')
        self.assertNotEqual(state['generation'],generation)
        # Check while NEW initialization data exists; a stop-only check would
        # pass even if the generation check were accidentally removed.
        self.assert_http_error(410, init, authorize=False)
        with self.get('/video/live.m3u8') as response:
            current_init=re.search(r'#EXT-X-MAP:URI="([^"]+)"',response.read().decode())[1]
        with self.get(current_init,authorize=False) as response:
            self.assertIn(b'ftyp',response.read())

    def test_direct_path_keeps_shared_media_and_cleans_up_fallback(self):
        self.start_receiver(secrets.token_hex(32))
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(MediaHandler, directory=str(ROOT)))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        with sync_playwright() as playwright:
            browser = launch_browser(playwright)
            try:
                page = browser.new_page(viewport={'width':1920, 'height':1080})
                errors = []
                page.on('pageerror', lambda error: errors.append(str(error)))
                # Supply the same private control capability as the fixture;
                # production bootstrap and the fake PPAPI boundary remain separate.
                page.add_init_script("""document.addEventListener('DOMContentLoaded',function(){
                    var token=TOKEN;
                    window.AirTizenControl={header:function(){return token;},
                      authorize:function(xhr){xhr.setRequestHeader('X-AirTizen-Control',token);},
                      url:function(url){return url+(url.indexOf('?')<0?'?':'&')+'token='+token;}};
                });""".replace('TOKEN', json.dumps(self.token)))
                page.set_content('<iframe name="tv" sandbox="allow-scripts" style="width:1920px;height:1080px" src="http://127.0.0.1:'+str(server.server_port)+'/app/index.html"></iframe>')
                ui = page.frame(name='tv')
                expect(ui.get_by_role('heading', name='Receiver test mode')).to_be_visible()
                ui.evaluate("""() => {
                    AirTizenVideo.stop();
                    const container=document.createElement('div');container.id='receiver-module';
                    const embed=document.createElement('embed');container.appendChild(embed);document.body.appendChild(container);
                    window.nativeCommands=[];embed.postMessage=function(value){nativeCommands.push(value);};
                    AirTizenEmbedded.phase='loaded';AirTizenVideo.start();AirTizenVideo.start();
                    window.nativeVideoState=function(state){embed.dispatchEvent(new MessageEvent('message',{
                        data:JSON.stringify({type:'airtizen-native-video',state:state,frames:1,queuedFrames:0,droppedPictures:0,lastMs:180,averageMs:178.327,code:-4})}));};
                    nativeVideoState('playing');
                }""")
                module = ui.locator('#receiver-module')
                expect(module).to_have_css('width', '1920px')
                self.assertEqual(ui.evaluate('nativeCommands'), ['airtizen-video-on'])
                ui.locator('#settings-action').focus()
                page.keyboard.press('Enter')
                ui.get_by_role('button',name='Developer mode: Off').click()
                ui.get_by_role('button',name='Open diagnostics').click()
                expect(ui.locator('#runtime-details')).to_contain_text('TV presentation average: 178.327 ms')
                expect(ui.locator('#runtime-details')).to_contain_text('excludes sender/network/panel')
                page.keyboard.press('Escape')
                photo = urllib.request.Request('http://127.0.0.1:5000/photo', method='PUT',
                    data=(ROOT/'tests/fixtures/photo.jpg').read_bytes(), headers={'X-Apple-Session-ID':'direct-photo','X-Apple-AssetKey':'one'})
                urllib.request.urlopen(photo, timeout=3).close()
                expect(ui.locator('#photo-overlay')).to_be_visible()
                expect(ui.locator('#photo-b')).to_have_js_property('naturalWidth', 64)
                expect(module).to_have_css('width', '1px')
                location='http://127.0.0.1:'+str(server.server_port)+'/tests/fixtures/shared-av.mp4'
                play=urllib.request.Request('http://127.0.0.1:5000/play', data=plistlib.dumps({'Content-Location':location}, fmt=plistlib.FMT_BINARY),
                                           headers={'Content-Type':'application/x-apple-binary-plist'})
                urllib.request.urlopen(play, timeout=3).close()
                expect(ui.locator('#shared-video')).to_have_js_property('videoWidth', 320)
                expect(ui.locator('#photo-overlay')).not_to_be_visible()
                self.assertTrue(ui.evaluate('AirTizenVideo.isActive()'))
                self.assertEqual(ui.evaluate('AirTizenVideo.debugSnapshot().mode'), 'direct-ppapi')
                urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:5000/stop', data=b''), timeout=3).close()
                expect(ui.locator('#share-overlay')).not_to_be_visible()
                expect(module).to_have_css('width', '1920px')
                ui.evaluate("nativeVideoState('idle')")
                expect(module).to_have_css('width', '1px')
                self.assertFalse(ui.evaluate('AirTizenVideo.isActive()'))
                # Observe the fallback media request; desktop Chrome need not
                # implement the Samsung native HLS player or keep it playing.
                with page.expect_request(BASE+'/video/live.m3u8*'):
                    ui.evaluate("nativeVideoState('error')")
                self.assertEqual(ui.evaluate('AirTizenVideo.debugSnapshot().mode'), 'native-hls-fallback')
                self.assertEqual(ui.evaluate('nativeCommands'), ['airtizen-video-on', 'airtizen-video-off'])
                with self.get('/stop', method='POST'):
                    pass
                expect(ui.locator('#mirror-video')).not_to_have_attribute('src', re.compile('.+'))
                ui.evaluate("AirTizenVideo.stop(); nativeVideoState('playing')")
                expect(module).to_have_css('width', '1px')
                self.assertFalse(ui.evaluate('AirTizenVideo.isActive()'))
                self.assertEqual(errors, [])
            finally:
                browser.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)
