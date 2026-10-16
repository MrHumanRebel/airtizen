#!/usr/bin/env python3
"""Decode native H.264/fMP4 output in Chromium through the real media HTTP API."""
import functools
import http.server
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.request
from playwright.sync_api import sync_playwright,expect
ROOT=Path(__file__).resolve().parents[1]
class Mirroring(unittest.TestCase):
    def test_native_fragments_play_in_browser(self):
        for port in (5000,45110):
            with socket.socket() as sock:
                sock.settimeout(.2)
                self.assertNotEqual(sock.connect_ex(('127.0.0.1',port)),0,'A development runtime is already using the test ports')
        log=tempfile.TemporaryFile()
        process=subprocess.Popen([str(ROOT/'native/build/airtizen_video_test'),'--serve'],stdout=log,stderr=log)
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(http.server.SimpleHTTPRequestHandler,directory=str(ROOT/'app')))
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            for _ in range(60):
                if process.poll() is not None:
                    log.seek(0);self.fail(log.read().decode(errors='replace'))
                try:
                    with urllib.request.urlopen('http://127.0.0.1:45110/status',timeout=1) as response:state=json.load(response)
                    if state['video']['available']:break
                except OSError:pass
                time.sleep(.1)
            else:self.fail('Native video was not initialized')
            with urllib.request.urlopen('http://127.0.0.1:45110/video/init') as response:
                self.assertIn(b'ftyp',response.read())
                self.assertEqual(int(response.headers['X-AirTizen-Generation']),state['video']['generation'])
            with sync_playwright() as playwright:
                browser=playwright.chromium.launch(executable_path=os.environ.get('CHROMIUM_EXECUTABLE') or shutil.which('chromium'),headless=True,args=['--no-sandbox'])
                page=browser.new_page(viewport={'width':1920,'height':1080})
                errors=[];page.on('pageerror',lambda error:errors.append(str(error)))
                page.set_content('<style>body{margin:0}</style><iframe name="tv" sandbox="allow-scripts" style="width:1920px;height:1080px;border:0" src="http://127.0.0.1:'+str(server.server_port)+'/index.html"></iframe>')
                ui=page.frame(name='tv');video=ui.locator('#mirror-video')
                expect(video).to_have_js_property('videoWidth',320,timeout=20000)
                expect(video).to_have_js_property('videoHeight',180)
                expect(ui.locator('#video-overlay')).to_be_visible()
                first=video.evaluate('(element) => element.currentTime')
                page.wait_for_timeout(600)
                self.assertGreater(video.evaluate('(element) => element.currentTime'),first)
                self.assertGreaterEqual(video.evaluate('(element) => element.readyState'),2)
                page.screenshot(path='/tmp/airtizen-mirror.png')
                self.assertEqual(errors,[])
                # Back stops the owned receiver rather than exiting the TV app mid-session.
                ui.locator('#receiver-action').focus();page.keyboard.press('Escape')
                expect(ui.get_by_role('heading',name='Receiver stopped',exact=True)).to_be_visible(timeout=5000)
                expect(ui.locator('#video-overlay')).not_to_be_visible(timeout=5000)
                browser.close()
        finally:
            process.terminate();process.wait(timeout=5);server.shutdown();server.server_close();log.close()
        self.assertEqual(process.returncode,0)
if __name__=='__main__':unittest.main(verbosity=2)
