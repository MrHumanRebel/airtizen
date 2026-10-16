#!/usr/bin/env python3
"""AirPlay HTTP URL video/audio sharing and browser playback/control regression."""
import functools,http.server,json,os,plistlib,shutil,socket,subprocess,tempfile,threading,time,unittest,urllib.request,urllib.error
from pathlib import Path
from media_server import MediaHandler
from playwright.sync_api import sync_playwright,expect
ROOT=Path(__file__).resolve().parents[1]
def airplay(path,body=None):
    request=urllib.request.Request('http://127.0.0.1:5000'+path,data=body,method='POST' if body is not None else 'GET',headers={'Content-Type':'application/x-apple-binary-plist'})
    with urllib.request.urlopen(request,timeout=4) as response:return response.read()
class VideoShare(unittest.TestCase):
    def test_url_audio_video_pause_seek_and_stop(self):
        log=tempfile.TemporaryFile()
        for port in (5000,45110):
            with socket.socket() as sock:sock.settimeout(.2);self.assertNotEqual(sock.connect_ex(('127.0.0.1',port)),0)
        process=subprocess.Popen([str(ROOT/'native/build/airtizen_runtime')],stdout=log,stderr=log)
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(MediaHandler,directory=str(ROOT)))
        threading.Thread(target=server.serve_forever,daemon=True).start()
        try:
            for _ in range(50):
                if process.poll() is not None:self.fail('Receiver failed to start')
                try:urllib.request.urlopen('http://127.0.0.1:45110/status',timeout=1).close();break
                except OSError:time.sleep(.1)
            for location in ('file:///etc/passwd','javascript:alert(1)','https://user:password@example.org/video.mp4','http://'):
                with self.assertRaises(urllib.error.HTTPError) as result:airplay('/play',plistlib.dumps({'Content-Location':location},fmt=plistlib.FMT_BINARY))
                self.assertEqual(result.exception.code,400)
            with sync_playwright() as playwright:
                browser=playwright.chromium.launch(executable_path=os.environ.get('CHROMIUM_EXECUTABLE') or shutil.which('chromium'),headless=True,args=['--no-sandbox'])
                page=browser.new_page(viewport={'width':1920,'height':1080});errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
                page.set_content('<iframe name="tv" sandbox="allow-scripts" style="width:1920px;height:1080px" src="http://127.0.0.1:'+str(server.server_port)+'/app/index.html"></iframe>')
                ui=page.frame(name='tv');expect(ui.get_by_role('heading',name='Receiver test mode')).to_be_visible()
                location='http://127.0.0.1:'+str(server.server_port)+'/tests/fixtures/shared-av.mp4'
                airplay('/play',plistlib.dumps({'Content-Location':location,'Start-Position':0.0},fmt=plistlib.FMT_BINARY))
                video=ui.locator('#shared-video');expect(video).to_have_js_property('videoWidth',320)
                expect(video).to_have_js_property('videoHeight',180)
                video.focus();page.keyboard.press('Enter')
                expect(video).to_have_js_property('paused',False)
                page.wait_for_timeout(650)
                self.assertGreater(video.evaluate('(v) => v.currentTime'),.1)
                self.assertGreater(video.evaluate('(v) => v.webkitAudioDecodedByteCount || 0'),0)
                airplay('/rate?value=0',b'');expect(video).to_have_js_property('paused',True)
                airplay('/scrub?position=1.5',b'');page.wait_for_timeout(500)
                self.assertAlmostEqual(video.evaluate('(v) => v.currentTime'),1.5,delta=.15)
                status=plistlib.loads(airplay('/playback-info'))
                self.assertAlmostEqual(status['duration'],3,delta=.2)
                self.assertEqual(status['rate'],0)
                airplay('/rate?value=1',b'');expect(video).to_have_js_property('paused',False)
                airplay('/stop',b'');expect(ui.locator('#share-overlay')).not_to_be_visible()
                self.assertEqual(errors,[]);browser.close()
        finally:
            process.terminate();process.wait(timeout=5);server.shutdown();server.server_close();log.close()
        self.assertEqual(process.returncode,0)
if __name__=='__main__':unittest.main(verbosity=2)
