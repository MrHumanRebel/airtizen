#!/usr/bin/env python3
"""Chromium smoke tests of the ES5 TV UI and its real HTTP bridge."""
import functools
import http.server
import threading
import json
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
import urllib.request
from browser_support import launch_browser
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[1]

class GUI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.log = tempfile.TemporaryFile()
        cls.runtime = subprocess.Popen([str(ROOT/'native/build/airtizen_runtime')],stdout=cls.log,stderr=cls.log)
        for _ in range(50):
            if cls.runtime.poll() is not None:
                raise RuntimeError('Test receiver failed to start; check occupied ports')
            try:
                urllib.request.urlopen('http://127.0.0.1:45110/status',timeout=1).close()
                break
            except OSError:
                time.sleep(.1)
        else:
            cls.runtime.terminate();cls.runtime.wait(timeout=5)
            raise RuntimeError('Receiver did not start')
        cls.server = http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(http.server.SimpleHTTPRequestHandler,directory=str(ROOT/'app')))
        cls.server_thread = threading.Thread(target=cls.server.serve_forever,daemon=True)
        cls.server_thread.start()
        cls.playwright = sync_playwright().start()
        cls.browser = launch_browser(cls.playwright)

    @classmethod
    def tearDownClass(cls):
        cls.browser.close();cls.playwright.stop();cls.server.shutdown();cls.server.server_close()
        cls.runtime.terminate();cls.runtime.wait(timeout=5);cls.log.close()

    def setUp(self):
        self.context = self.browser.new_context(viewport={'width':1920,'height':1080})
        self.page = self.context.new_page()
        self.errors=[]
        self.page.on('pageerror', lambda error: self.errors.append(str(error)))
        self.page.set_content('<style>body{margin:0}</style><iframe name="tv" sandbox="allow-scripts" style="width:1920px;height:1080px;border:0" src="http://127.0.0.1:'+str(self.server.server_port)+'/index.html"></iframe>')
        self.ui=self.page.frame(name='tv')
        self.ui.get_by_role('heading',name='Receiver test mode').wait_for()

    def tearDown(self):
        self.assertEqual(self.errors,[])
        self.context.close()

    def test_real_bridge_and_control(self):
        self.assertIn('Host test mode',self.ui.locator('#capability-note').inner_text())
        self.ui.get_by_role('button',name='Stop receiver',exact=True).click()
        self.ui.get_by_role('heading',name='Receiver stopped',exact=True).wait_for()
        self.ui.get_by_role('button',name='Start receiver',exact=True).click()
        self.ui.get_by_role('heading',name='Receiver test mode').wait_for()

    def test_developer_toggle_and_logs(self):
        self.assertFalse(self.ui.locator('#log-overlay').is_visible())
        self.ui.get_by_role('button',name='Settings',exact=True).click()
        self.assertFalse(self.ui.locator('#diagnostics-action').is_visible())
        self.ui.get_by_role('button',name='Developer mode: Off').click()
        self.assertEqual(self.ui.locator('#developer-toggle').get_attribute('aria-pressed'),'true')
        self.ui.get_by_role('button',name='Open diagnostics').click()
        self.assertIn('RAOP listener started',self.ui.locator('#full-log').inner_text())
        self.assertIn('WebAssembly:',self.ui.locator('#runtime-details').inner_text())
        self.page.keyboard.press('Escape')
        self.assertFalse(self.ui.locator('#log-overlay').is_visible())
        self.ui.get_by_role('button',name='Settings',exact=True).click()
        self.assertTrue(self.ui.get_by_role('button',name='Developer mode: On').is_visible())

    def test_remote_navigation_and_layout(self):
        self.ui.locator('#receiver-action').focus()
        self.page.keyboard.press('ArrowRight')
        self.assertEqual(self.ui.locator(':focus').get_attribute('id'),'settings-action')
        self.page.keyboard.press('Enter')
        self.assertTrue(self.ui.locator('#settings-overlay').is_visible())
        self.page.keyboard.press('Escape')
        self.assertEqual(self.ui.locator(':focus').get_attribute('id'),'settings-action')
        for selector in ('header','main','footer','#receiver-action','#settings-action'):
            box=self.ui.locator(selector).bounding_box()
            self.assertGreaterEqual(box['x'],0)
            self.assertGreaterEqual(box['y'],0)
            self.assertLessEqual(box['x']+box['width'],1920)
            self.assertLessEqual(box['y']+box['height'],1080)
        self.page.screenshot(path='/tmp/airtizen-ui.png')

    def test_embedded_module_uses_tv_network(self):
        self.context.add_init_script("""window.tizen={systeminfo:{getPropertyValue:function(name,ok,error){
            if(name==='ETHERNET_NETWORK')ok({ipAddress:'192.0.2.20'});
            else error({message:'inactive'});
        }}};""")
        self.page.goto('http://127.0.0.1:'+str(self.server.server_port)+'/index.html')
        module=self.page.locator('#receiver-module embed')
        expect(module).to_have_attribute('type','application/x-nacl')
        expect(module).to_have_attribute('src','receiver/receiver.nmf')
        expect(module).to_have_attribute('receiver-ip','192.0.2.20')
        token=module.get_attribute('receiver-token')
        self.assertRegex(token,r'^[0-9a-f]{64}$')
        self.assertNotIn(token,self.page.evaluate('JSON.stringify(window.AirTizenEmbedded)'))
        identity=module.get_attribute('receiver-id')
        self.assertRegex(identity,r'^[0-9A-F]{12}$')
        self.assertEqual(int(identity[:2],16)&3,2)
        self.page.reload()
        expect(self.page.locator('#receiver-module embed')).to_have_attribute('receiver-id',identity)
        self.assertNotEqual(self.page.locator('#receiver-module embed').get_attribute('receiver-token'),token)
        # This checks bootstrap parameters only; desktop Chrome cannot execute NaCl.
        self.page.locator('#receiver-module').evaluate("el => el.dispatchEvent(new Event('error'))")
        self.assertEqual(self.page.evaluate('window.AirTizenEmbedded.phase'),'failed')

    def test_embedded_module_rejects_missing_lan(self):
        self.context.add_init_script("""window.tizen={systeminfo:{getPropertyValue:function(name,ok){
            ok({ipAddress:'127.0.0.1'});
        }}};""")
        self.page.goto('http://127.0.0.1:'+str(self.server.server_port)+'/index.html')
        self.assertEqual(self.page.locator('#receiver-module').count(),0)
        self.assertEqual(self.page.evaluate('window.AirTizenEmbedded.phase'),'failed')
        self.assertIn('No active TV IPv4',self.page.evaluate('window.AirTizenEmbedded.error'))

    def test_remote_text_is_not_html(self):
        with urllib.request.urlopen('http://127.0.0.1:45110/status') as response:
            data=json.load(response)
        data['status']['connected']=True
        data['status']['device']='<img src=x onerror="window.injected=true">'
        data['nowPlaying']={'title':'<script>window.injected=true</script>', 'artist':'A' * 256, 'album':'B' * 256}
        self.page.route('http://127.0.0.1:45110/status',lambda route:route.fulfill(status=200,content_type='application/json',headers={'Access-Control-Allow-Origin':'null'},body=json.dumps(data)))
        expect(self.ui.locator('#device-name')).to_have_text(data['status']['device'])
        self.assertEqual(self.ui.locator('#device-name img').count(),0)
        expect(self.ui.locator('#track-title')).to_have_text(data['nowPlaying']['title'])
        self.assertEqual(self.ui.locator('#track-title script').count(),0)
        for selector in ('#now', '#track-title', '#track-artist', 'footer'):
            box=self.ui.locator(selector).bounding_box()
            self.assertGreaterEqual(box['x'],0)
            self.assertLessEqual(box['x']+box['width'],1920)
            self.assertLessEqual(box['y']+box['height'],1080)
        self.assertIsNone(self.ui.evaluate('window.injected'))

    def test_control_error_reason_is_visible_without_response_secrets(self):
        endpoint='http://127.0.0.1:45110/start'
        def reject(route):
            route.fulfill(status=403,headers={'Access-Control-Allow-Origin':'null'},
                          content_type='application/json',body='{"ok":false,"error":"invalid_origin","token":"never-log-me"}')
        self.page.route(endpoint,reject)
        result=self.ui.evaluate('''() => new Promise(resolve => AirTizenBridge.start(resolve))''')
        self.assertEqual(result['error'],'runtime HTTP 403 (invalid_origin)')
        status=self.ui.evaluate('''() => new Promise(resolve => AirTizenBridge.status(resolve))''')
        self.assertIn('start failed: runtime HTTP 403 (invalid_origin)',status['log'])
        self.assertNotIn('never-log-me',status['log'])

if __name__=='__main__':
    unittest.main(verbosity=2)
