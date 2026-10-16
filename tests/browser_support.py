"""Select one browser consistently and fail clearly when media codecs are absent."""
import os
import shutil


def launch_browser(playwright):
    executable = os.environ.get('CHROMIUM_EXECUTABLE')
    channel = os.environ.get('CHROMIUM_CHANNEL')
    options = {'headless': True, 'args': ['--no-sandbox']}
    if executable:
        options['executable_path'] = executable
    elif channel:
        options['channel'] = channel
    elif shutil.which('chromium'):
        options['executable_path'] = shutil.which('chromium')
    browser = playwright.chromium.launch(**options)
    print('Browser: ' + browser.version + ' (' + (executable or channel or options.get('executable_path', 'playwright chromium')) + ')', flush=True)
    return browser


def require_media_support(page, *, mse_mime=None):
    support = page.evaluate('''mime => {
        const video = document.createElement('video');
        return {
            h264: video.canPlayType('video/mp4; codecs="avc1.42E01E"'),
            aac: video.canPlayType('audio/mp4; codecs="mp4a.40.2"'),
            mse: !mime || !!(window.MediaSource && MediaSource.isTypeSupported(mime))
        };
    }''', mse_mime)
    if not support['h264'] or not support['aac'] or not support['mse']:
        raise AssertionError(
            'Media tests require H.264/AAC and the received MSE format. '
            'Install Chrome with `python3 -m playwright install --with-deps chrome` '
            'and set CHROMIUM_CHANNEL=chrome, or set CHROMIUM_EXECUTABLE to a '
            'codec-enabled browser. Reported support: ' + repr(support)
            + '; stream: ' + repr(mse_mime)
        )
