#!/usr/bin/env python3
"""Real process/socket integration checks; no Apple-device claims."""
import concurrent.futures
import base64
import hashlib
from http.client import HTTPConnection
import json
import os
from pathlib import Path
import plistlib
import socket
import secrets
import struct
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request
from cryptography.hazmat.primitives.asymmetric import ed25519, x25519
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('AIRTIZEN_BINARY', ROOT / 'native/build/airtizen_runtime'))
BASE = 'http://127.0.0.1:45110'
NAME = 'Test "Receiver" \\ TV'


def http(path='/status', method='GET', headers=None):
    request = urllib.request.Request(BASE + path, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, json.load(error)


def wire(request, fragment=False):
    with socket.create_connection(('127.0.0.1', 45110), timeout=4) as sock:
        if fragment:
            for chunk in [request[:3], request[3:17], request[17:]]:
                sock.sendall(chunk)
                time.sleep(.02)
        else:
            sock.sendall(request)
        data = b''
        while True:
            try:
                block = sock.recv(16384)
            except ConnectionResetError:
                if data:
                    break
                raise
            if not block:
                break
            data += block
        return data


def rtsp(method, path='*', body=b'', headers=None):
    with socket.create_connection(('127.0.0.1', 5000), timeout=4) as sock:
        fields = {'CSeq': '1', 'Content-Length': str(len(body))}
        fields.update(headers or {})
        request = f'{method} {path} RTSP/1.0\r\n' + ''.join(f'{k}: {v}\r\n' for k,v in fields.items()) + '\r\n'
        sock.sendall(request.encode() + body)
        data = b''
        while b'\r\n\r\n' not in data:
            block = sock.recv(8192)
            if not block:
                break
            data += block
        if b'\r\n\r\n' in data:
            header,body=data.split(b'\r\n\r\n',1)
            fields=dict(line.split(b': ',1) for line in header.split(b'\r\n')[1:])
            length=int(fields.get(b'Content-Length',b'0'))
            while len(body)<length:
                chunk=sock.recv(length-len(body))
                if not chunk: raise AssertionError('Truncated RTSP body')
                body+=chunk
            data=header+b'\r\n\r\n'+body
        return data


class Runtime(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Refuse to test someone else's service or mask a startup failure.
        for port in (5000, 45110):
            with socket.socket() as sock:
                sock.settimeout(.3)
                if sock.connect_ex(('127.0.0.1', port)) == 0:
                    raise RuntimeError(f'Port {port} is occupied; stop the owned development runtime first')
        cls.log = tempfile.TemporaryFile()
        cls.process = subprocess.Popen([str(BINARY), NAME], stdout=cls.log, stderr=cls.log)
        for _ in range(60):
            if cls.process.poll() is not None:
                cls.log.seek(0)
                raise RuntimeError(cls.log.read().decode(errors='replace'))
            try:
                if http()[0] == 200:
                    return
            except (OSError, urllib.error.URLError):
                pass
            time.sleep(.1)
        cls.process.terminate()
        cls.process.wait(timeout=5)
        raise RuntimeError('Runtime never became healthy')

    @classmethod
    def tearDownClass(cls):
        cls.process.terminate()
        cls.process.wait(timeout=5)
        cls.log.close()
        if cls.process.returncode != 0:
            raise AssertionError(f'Runtime exited with {cls.process.returncode}')

    def test_status_and_escaped_name(self):
        code, data = http()
        self.assertEqual(code, 200)
        self.assertEqual(data['receiverName'], NAME)
        self.assertEqual(data['service'], 'running')
        self.assertTrue(data['capabilities']['mirroring'])
        self.assertFalse(data['capabilities']['productionReady'])
        self.assertEqual(data['capabilities']['audioOutput'], 'host_stub')
        self.assertIsInstance(data['logs'], list)
        self.assertGreater(len(data['logs']), 0)
        self.assertEqual(data['status']['raop'], 'listening')

    def test_exact_path(self):
        for path in ('/status-not-a-route', '/start/extra', '/stop?x=1'):
            self.assertEqual(http(path)[0], 404)

    def test_control_requires_header(self):
        self.assertEqual(http('/stop', 'POST')[0], 403)
        self.assertEqual(http()[1]['service'], 'running')

    def test_foreign_origin_rejected(self):
        self.assertEqual(http('/stop', 'POST', {'Origin': 'https://untrusted.example', 'X-AirTizen-Control': '1'})[0], 403)
        self.assertEqual(http('/stop', 'POST', {'Origin': 'file://', 'X-AirTizen-Control': '1'})[0], 403)

    def test_host_rebinding_rejected(self):
        response = wire(b'GET /status HTTP/1.1\r\nHost: untrusted.example:45110\r\n\r\n')
        self.assertIn(b'403 Forbidden', response)

    def test_fragmented_request_and_content_length(self):
        response = wire(b'GET /status HTTP/1.1\r\nHost: 127.0.0.1:45110\r\n\r\n', True)
        header, body = response.split(b'\r\n\r\n', 1)
        fields = dict(line.split(b': ', 1) for line in header.split(b'\r\n')[1:])
        self.assertEqual(int(fields[b'Content-Length']), len(body))
        self.assertTrue(json.loads(body)['ok'])

    def test_preflight(self):
        response = wire(b'OPTIONS /start HTTP/1.1\r\nHost: 127.0.0.1:45110\r\nOrigin: null\r\nAccess-Control-Request-Headers: x-airtizen-control\r\n\r\n')
        self.assertIn(b'204 No Content', response)
        self.assertTrue(response.endswith(b'\r\n\r\n'))

    def test_oversized_headers_and_body(self):
        response = wire(b'GET /status HTTP/1.1\r\nHost: 127.0.0.1:45110\r\nX-Test: '+b'x'*4050)
        self.assertIn(b'431 ', response)
        response = wire(b'POST /start HTTP/1.1\r\nHost: 127.0.0.1:45110\r\nContent-Length: 5\r\n\r\n12345')
        self.assertIn(b'413 ', response)

    def test_concurrent_status(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            for code, data in pool.map(lambda _: http(), range(16)):
                self.assertEqual(code, 200)
                self.assertTrue(data['ok'])

    def test_pair_verify_real_crypto(self):
        self.verify_pairing('RTSP/1.0', True)

    def test_http_pair_verify_real_crypto(self):
        self.verify_pairing('HTTP/1.1', False)

    def verify_pairing(self, protocol, cseq):
        challenge = bytes(range(16))
        signing = ed25519.Ed25519PrivateKey.generate()
        ephemeral = x25519.X25519PrivateKey.generate()
        signing_pub = signing.public_key().public_bytes_raw()
        ephemeral_pub = ephemeral.public_key().public_bytes_raw()
        with socket.create_connection(('127.0.0.1',5000),timeout=4) as sock:
            def exchange(path, body, sequence, method="POST", apple_challenge=False):
                request=(f'{method} {path} {protocol}\r\n'+(f'CSeq: {sequence}\r\n' if cseq else '')+
                    (f'Apple-Challenge: {base64.b64encode(challenge).decode().rstrip("=")}\r\n' if apple_challenge else '')+
                    f'Content-Length: {len(body)}\r\n\r\n').encode()+body
                sock.sendall(request)
                response=b''
                while b'\r\n\r\n' not in response:
                    response+=sock.recv(8192)
                header,payload=response.split(b'\r\n\r\n',1)
                self.assertTrue(header.startswith((protocol+' 200 OK').encode()))
                if apple_challenge:self.assert_apple_response(header,challenge,'127.0.0.1')
                fields=dict(line.split(b': ',1) for line in header.split(b'\r\n')[1:])
                length=int(fields.get(b'Content-Length',b'0'))
                while len(payload)<length:
                    block=sock.recv(length-len(payload))
                    if not block: self.fail('Receiver closed handshake early')
                    payload+=block
                return payload
            exchange('*',b'',0,'OPTIONS',True)
            info=plistlib.loads(exchange('/info',b'',1,'GET'))
            server_signing=exchange('/pair-setup',signing_pub,2)
            self.assertEqual(info['pk'],server_signing)
            self.assertEqual(len(server_signing),32)
            handshake=exchange('/pair-verify',b'\x01\x00\x00\x00'+ephemeral_pub+signing_pub,3)
            self.assertEqual(len(handshake),96)
            server_ephemeral=handshake[:32]
            shared=ephemeral.exchange(x25519.X25519PublicKey.from_public_bytes(server_ephemeral))
            key=hashlib.sha512(b'Pair-Verify-AES-Key'+shared).digest()[:16]
            iv=hashlib.sha512(b'Pair-Verify-AES-IV'+shared).digest()[:16]
            cipher=Cipher(algorithms.AES(key),modes.CTR(iv))
            server_signature=cipher.decryptor().update(handshake[32:])
            ed25519.Ed25519PublicKey.from_public_bytes(server_signing).verify(server_signature,server_ephemeral+ephemeral_pub)
            client_signature=signing.sign(ephemeral_pub+server_ephemeral)
            encryptor=cipher.encryptor()
            encryptor.update(bytes(64))
            exchange('/pair-verify',b'\x00\x00\x00\x00'+encryptor.update(client_signature),4)
            state=http()[1]
            self.assertEqual(state['status']['pair'],'verified')
            self.assertTrue(any('signature verified' in line for line in state['logs']))

    def test_raop_info_identity(self):
        response=rtsp('GET','/info')
        self.assertIn(b'200 OK',response)
        info=plistlib.loads(response.split(b'\r\n\r\n',1)[1])
        self.assertEqual(info['name'],NAME)
        self.assertEqual(info['model'],'AirTizen1,1')
        self.assertEqual(len(info['pk']),32)
        self.assertNotEqual(info['deviceID'],'A1:B2:C3:D4:E5:F6')
        self.assertEqual(info['features'],0x4803a383)
        self.assertEqual({entry['type'] for entry in info['audioFormats']},{100,101})
        self.assertTrue(all(entry['audioOutputFormats']==0x1d40000 for entry in info['audioFormats']))
        display=info['displays'][0]
        self.assertEqual((display['width'],display['height'],display['maxFPS']),(1920,1080,30))
        self.assertAlmostEqual(display['refreshRate'],1/60)
        self.assertEqual(display['uuid'],info['pi'])
        self.assertEqual({item['type'] for item in info['audioLatencies']},{100,101})
        self.assertFalse(info['keepAliveLowPower'])
        self.assertFalse(info['keepAliveSendStatsAsBody'])

    def assert_apple_response(self, header, challenge, local_ip):
        fields=dict(line.split(b': ',1) for line in header.split(b'\r\n')[1:])
        self.assertIn(b'Apple-Response',fields)
        encoded=fields[b'Apple-Response']
        self.assertNotIn(b'=',encoded)
        signature=base64.b64decode(encoded+b'==',validate=True)
        self.assertEqual(len(signature),256)
        public=serialization.load_pem_public_key((ROOT/'tests/fixtures/airport-public.pem').read_bytes()).public_numbers()
        block=pow(int.from_bytes(signature,'big'),public.e,public.n).to_bytes(256,'big')
        with urllib.request.urlopen('http://127.0.0.1:5000/info',timeout=2) as response:
            device_id=plistlib.loads(response.read())['deviceID']
        address=socket.inet_pton(socket.AF_INET6 if ':' in local_ip else socket.AF_INET,local_ip)
        payload=challenge+address+bytes.fromhex(device_id.replace(':',''))
        payload=payload.ljust(32,b'\0')
        self.assertEqual(block,b'\0\1'+b'\xff'*(256-len(payload)-3)+b'\0'+payload)

    def test_apple_challenge_encodings_and_rejections(self):
        for size in (1,2,3,15,16):
            challenge=bytes(range(size))
            for padded in (True,False):
                text=base64.b64encode(challenge).decode()
                if not padded:text=text.rstrip('=')
                reply=rtsp('OPTIONS',headers={'Apple-Challenge':text})
                header=reply.split(b'\r\n\r\n',1)[0]
                self.assertTrue(header.startswith(b'RTSP/1.0 200 OK'))
                self.assert_apple_response(header,challenge,'127.0.0.1')
        for text in ('','!','A','AB','AQ===','AQ==trailing','====',base64.b64encode(bytes(17)).decode()):
            reply=rtsp('OPTIONS',headers={'Apple-Challenge':text})
            self.assertTrue(reply.startswith(b'RTSP/1.0 400 Bad Request'),reply)
            self.assertNotIn(b'Apple-Response:',reply)
            self.assertIn(b'Content-Length: 0\r\n',reply)
        self.assertEqual(http()[0],200)

    def test_apple_challenge_diagnostics_exclude_challenge_and_signature(self):
        challenge=secrets.token_bytes(16);text=base64.b64encode(challenge).decode().rstrip('=')
        reply=rtsp('OPTIONS',headers={'Apple-Challenge':text})
        fields=dict(line.split(b': ',1) for line in reply.split(b'\r\n\r\n',1)[0].split(b'\r\n')[1:])
        logs='\n'.join(http()[1]['logs'])
        self.assertIn('Apple-Challenge answered: RSA-2048 response signed',logs)
        self.assertNotIn(text,logs)
        self.assertNotIn(fields[b'Apple-Response'].decode(),logs)
        reply=rtsp('OPTIONS')
        self.assertNotIn(b'Apple-Response:',reply)
        self.assertIn('OPTIONS probe: Apple-Challenge absent','\n'.join(http()[1]['logs']))

    @staticmethod
    def txt_fields(data):
        fields={};position=0
        while position<len(data):
            length=data[position];position+=1
            item=data[position:position+length].decode();position+=length
            key,_,value=item.partition('=');fields[key]=value
        return fields

    def test_http_info_qualifiers_and_persistent_empty_responses(self):
        client=HTTPConnection('127.0.0.1',5000,timeout=2)
        try:
            client.request('GET','/info',headers={'CSeq':'7'})
            response=client.getresponse()
            self.assertEqual((response.version,response.status),(11,200))
            self.assertEqual(response.getheader('CSeq'),'7')
            info=plistlib.loads(response.read())
            body=plistlib.dumps({'qualifier':['txtAirPlay','txtRAOP']},fmt=plistlib.FMT_BINARY)
            client.request('GET','/info',body,{'Content-Type':'application/x-apple-binary-plist'})
            response=client.getresponse();self.assertEqual(response.status,200)
            qualified=plistlib.loads(response.read())
            self.assertEqual(set(qualified),{'txtAirPlay','txtRAOP'})
            for key in ('txtAirPlay','txtRAOP'):
                self.assertEqual(bytes.fromhex(self.txt_fields(qualified[key])['pk']),info['pk'])
            self.assertEqual(self.txt_fields(qualified['txtAirPlay'])['pi'],info['pi'])
            # An empty HTTP reply must have a length, otherwise the client
            # waits for EOF while the receiver keeps this socket open.
            client.request('POST','/feedback',b'')
            response=client.getresponse()
            self.assertEqual(response.status,200)
            self.assertEqual(response.getheader('Content-Length'),'0')
            self.assertEqual(response.read(),b'')
            client.request('GET','/info?txtAirPlay')
            response=client.getresponse();self.assertEqual(response.status,200)
            self.assertEqual(plistlib.loads(response.read())['txtAirPlay'],qualified['txtAirPlay'])
        finally:client.close()

    def test_pipelined_mixed_protocol_requests_and_fragmented_body(self):
        messages=[
            b'GET /info?txtAirPlay HTTP/1.1\r\nHost: receiver\r\n\r\n',
            b'GET /info RTSP/1.0\r\nCSeq: 1\r\n\r\n',
            b'POST /feedback HTTP/1.1\r\nContent-Length: 0\r\n\r\n',
            b'POST /pair-setup RTSP/1.0\r\nCSeq: 2\r\nContent-Length: 32\r\n\r\n'+bytes(32)]
        pending=b'';bodies=[]
        with socket.create_connection(('127.0.0.1',5000),timeout=2) as sender:
            # Coalesce complete requests, but split the final body across reads.
            packed=b''.join(messages)
            sender.sendall(packed[:-17]);time.sleep(.02);sender.sendall(packed[-17:])
            for protocol in (b'HTTP/1.1',b'RTSP/1.0',b'HTTP/1.1',b'RTSP/1.0'):
                while b'\r\n\r\n' not in pending:
                    part=sender.recv(8192);self.assertTrue(part);pending+=part
                header,pending=pending.split(b'\r\n\r\n',1)
                self.assertTrue(header.startswith(protocol+b' 200 '),header)
                fields=dict(line.split(b': ',1) for line in header.split(b'\r\n')[1:])
                length=int(fields[b'Content-Length'])
                while len(pending)<length:
                    part=sender.recv(8192);self.assertTrue(part);pending+=part
                bodies.append(pending[:length]);pending=pending[length:]
        self.assertEqual(bodies[2],b'')
        self.assertEqual(bodies[3],plistlib.loads(bodies[1])['pk'])
        self.assertEqual(bytes.fromhex(self.txt_fields(plistlib.loads(bodies[0])['txtAirPlay'])['pk']),bodies[3])

    def test_info_rejects_malformed_qualifiers(self):
        for value in ([],{'qualifier':'txtAirPlay'},{'qualifier':[123]}):
            body=plistlib.dumps(value,fmt=plistlib.FMT_BINARY)
            self.assertIn(b'400 Bad Request',rtsp('GET','/info',body))

    def test_handshake_logs_status_without_credentials(self):
        secret='never-log-iphone-secret'
        request=urllib.request.Request('http://127.0.0.1:5000/info?txtAirPlay&token='+secret,
            headers={'Authorization':secret})
        with urllib.request.urlopen(request,timeout=2) as response:self.assertEqual(response.status,200)
        logs='\n'.join(http()[1]['logs'])
        self.assertIn('GET /info status=200',logs)
        self.assertNotIn(secret,logs)

    def test_url_video_reverse_events(self):
        def command(path,body=b''):
            req=urllib.request.Request('http://127.0.0.1:5000'+path,data=body,method='POST')
            with urllib.request.urlopen(req,timeout=3) as response:
                self.assertEqual(response.status,200);return response.read()
        with socket.create_connection(('127.0.0.1',5000),timeout=3) as reverse:
            pending=b''
            def message():
                nonlocal pending
                while b'\r\n\r\n' not in pending:
                    block=reverse.recv(4096)
                    if not block:self.fail('Reverse channel closed')
                    pending+=block
                header,pending=pending.split(b'\r\n\r\n',1)
                fields=dict(line.split(b': ',1) for line in header.split(b'\r\n')[1:])
                length=int(fields.get(b'Content-Length',b'0'))
                while len(pending)<length:pending+=reverse.recv(4096)
                body,pending=pending[:length],pending[length:]
                return header,body
            reverse.sendall(b'POST /reverse HTTP/1.1\r\nHost: 127.0.0.1:5000\r\nX-Apple-Session-ID: 12345678-1234-1234-1234-123456789ABC\r\nUpgrade: PTTH/1.0\r\nConnection: Upgrade\r\nContent-Length: 0\r\n\r\n')
            self.assertIn(b'101 Switching Protocols',message()[0])
            command('/play',plistlib.dumps({'Content-Location':'http://192.0.2.1/example.mp4'},fmt=plistlib.FMT_BINARY))
            def event(expected):
                header,body=message();self.assertTrue(header.startswith(b'POST /event HTTP/1.1'))
                self.assertEqual(plistlib.loads(body)['state'],expected)
                reverse.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n')
            event('loading')
            generation=http()[1]['playback']['generation']
            path='/playback/report?generation='+str(generation)+'&position=1&duration=30&ready=1&failed=0'
            self.assertEqual(http(path,'POST',{'X-AirTizen-Control':'1'})[0],200)
            event('playing');command('/rate?value=0');event('paused')
            self.assertEqual(http(path+'&ended=2','POST',{'X-AirTizen-Control':'1'})[0],404)
            self.assertEqual(http(path+'&ended=1','POST',{'X-AirTizen-Control':'1'})[0],200)
            event('stopped')
            self.assertFalse(http()[1]['playback']['active'])
            command('/stop')
            self.assertEqual(http()[1]['service'],'running')
        with urllib.request.urlopen('http://127.0.0.1:5000/server-info',timeout=3) as response:
            self.assertEqual(plistlib.loads(response.read())['model'],'AirTizen1,1')

    def test_raop_options(self):
        response = rtsp('OPTIONS')
        self.assertIn(b'RTSP/1.0 200 OK', response)
        self.assertIn(b'Public:', response)
        self.assertIn(b'RECORD', response)
        self.assertIn(b'Server: AirTunes/220.68\r\n',response)
        self.assertNotIn(b'PAUSE',response)
        self.assertIn(b'501 Not Implemented',rtsp('PAUSE'))

    def test_options_header_diagnostics_redact_identifiers(self):
        secret='never-print-device-identity-0123456789'
        headers={'User-Agent':'AirPlay/901.33.7 ('+secret+')','DACP-ID':secret,
            'X-Apple-Session-ID':secret,'Authorization':secret,'X-Private-'+secret:secret}
        reply=rtsp('OPTIONS','rtsp://192.0.2.44/'+secret,headers=headers)
        self.assertTrue(reply.startswith(b'RTSP/1.0 200 OK'))
        logs='\n'.join(http()[1]['logs'])
        self.assertIn('protocol=RTSP/1.0 target=rtsp-uri',logs)
        self.assertIn('OPTIONS header User-Agent: AirPlay/901.33.7 bytes=',logs)
        self.assertIn('OPTIONS header DACP-ID: redacted bytes=',logs)
        self.assertIn('OPTIONS header <other>: redacted bytes=',logs)
        self.assertNotIn(secret,logs)
        rtsp('OPTIONS',headers={'User-Agent':'AirPlay/12345678901234567890'+secret})
        logs='\n'.join(http()[1]['logs'])
        self.assertIn('target=asterisk',logs)
        self.assertIn('OPTIONS header User-Agent: redacted bytes=',logs)
        self.assertNotIn(secret,logs)

    def test_discovery_profile_switch_preserves_identity_and_metadata(self):
        def info(path='/info'):
            with urllib.request.urlopen('http://127.0.0.1:5000'+path,timeout=2) as response:
                return plistlib.loads(response.read())
        original=info()
        self.assertEqual(http('/discovery/uxplay-test','POST')[0],403)
        self.assertEqual(http('/discovery/invalid','POST',{'X-AirTizen-Control':'1'})[0],404)
        self.assertEqual(http('/discovery/native')[0],405)
        try:
            for profile,features,model in (('uxplay-test',0x5a7ffee6,'AppleTV3,2'),('native',0x4803a383,'AirTizen1,1')):
                with socket.create_connection(('127.0.0.1',5000),timeout=2) as previous:
                    previous.sendall(b'OPTIONS * RTSP/1.0\r\nCSeq: 1\r\n\r\n')
                    old=b''
                    while b'\r\n\r\n' not in old:old+=previous.recv(4096)
                    code,state=http('/discovery/'+profile,'POST',{'X-AirTizen-Control':'1'})
                    self.assertEqual(code,200)
                    self.assertEqual(previous.recv(4096),b'')
                self.assertEqual(state['receiverName'],NAME)
                self.assertEqual(state['service'],'running')
                self.assertEqual(state['discovery']['profile'],profile)
                self.assertIn('Discovery profile='+profile,'\n'.join(state['logs']))
                self.assertEqual(state['discovery']['implementedFeatures'],'0x4803a383,0x0')
                self.assertEqual(state['discovery']['experimental'],profile=='uxplay-test')
                self.assertFalse(state['capabilities']['productionReady'])
                current=info();server=info('/server-info')
                self.assertEqual((current['features'],server['features']),(features,features))
                self.assertEqual((current['model'],server['model']),(model,model))
                self.assertEqual(current['displays'][0]['rotation'],bool(features & (1<<8)))
                self.assertEqual((current['deviceID'],current['pi']),(original['deviceID'],original['pi']))
                qualified=info('/info?txtAirPlay&txtRAOP')
                air=self.txt_fields(qualified['txtAirPlay']);raop=self.txt_fields(qualified['txtRAOP'])
                self.assertEqual(raop['ft'],air['features'])
                self.assertEqual(int(raop['ft'].split(',')[0],16),features)
                self.assertEqual((raop['am'],air['model']),(model,model))
                self.assertEqual((raop['vs'],air['srcvers'],current['sourceVersion']),('220.68',)*3)
                self.assertEqual(bytes.fromhex(raop['pk']),current['pk'])
                self.assertEqual(raop['pk'],air['pk'])
                if profile=='uxplay-test':
                    self.assertEqual((raop['rhd'],raop['sv'],raop['cn']),('5.6.0.0','false','0,1,2,3'))
                else:self.assertEqual(raop['cn'],'1,2,3')
        finally:http('/discovery/native','POST',{'X-AirTizen-Control':'1'})

    def test_raop_malformed_inputs(self):
        for method, path, body in [('SET_PARAMETER','*',b'volume: 0'), ('SETUP','*',b'bad'),
                                    ('POST','/pair-setup',b'bad'), ('POST','/pair-verify',b'bad'),
                                    ('POST','/fp-setup',b'bad')]:
            with self.subTest(method=method,path=path):
                self.assertIn(b'400 Bad Request', rtsp(method,path,body))
        for obj in ({'eiv':b''}, {'eiv':b'a'*16,'ekey':b'b'}, {'streams':[{'type':96}]}, {}):
            response = rtsp('SETUP', body=plistlib.dumps(obj,fmt=plistlib.FMT_BINARY))
            # Audio stream setup without preceding cryptographic setup must not crash.
            self.assertTrue(response.startswith(b'RTSP/1.0 '), response)
        bad_mode=bytearray(16);bad_mode[4]=3;bad_mode[14]=255
        self.assertTrue(rtsp('POST','/fp-setup',bad_mode).startswith(b'RTSP/1.0 '))
        self.assertEqual(http()[0], 200)

    def test_mdns_records_and_capabilities(self):
        def name(packet,offset):
            labels=[]
            while packet[offset]:
                length=packet[offset];offset+=1
                labels.append(packet[offset:offset+length].decode())
                offset+=length
            return '.'.join(labels),offset+1
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM,socket.IPPROTO_UDP) as sock:
            sock.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
            sock.bind(('',5353))
            sock.setsockopt(socket.IPPROTO_IP,socket.IP_ADD_MEMBERSHIP,socket.inet_aton('224.0.0.251')+socket.inet_aton('0.0.0.0'))
            sock.settimeout(7)
            question=b''.join(bytes([len(part)])+part for part in (b'_raop',b'_tcp',b'local'))+b'\0'
            sock.sendto(struct.pack('!6H',0,0,1,0,0,0)+question+struct.pack('!HH',12,1),('224.0.0.251',5353))
            for _ in range(30):
                packet,_=sock.recvfrom(4096)
                if len(packet)>=12 and packet[2]&128 and NAME.encode() in packet:break
            else:self.fail('Receiver did not advertise its identity')
        self.assertEqual(struct.unpack('!H',packet[6:8])[0],7)
        position=12;types=[];texts=[];txt_records={}
        for _ in range(7):
            owner,position=name(packet,position)
            kind,cls,ttl,size=struct.unpack('!HHIH',packet[position:position+10]);position+=10
            payload=packet[position:position+size];position+=size;types.append(kind)
            self.assertEqual(ttl,120)
            if kind!=12:self.assertTrue(cls&0x8000)
            if kind==33:self.assertEqual(struct.unpack('!H',payload[4:6])[0],5000)
            if kind==1:self.assertFalse(socket.inet_ntoa(payload).startswith('127.'))
            if kind==16:
                txt_records['txtAirPlay' if '._airplay.' in owner else 'txtRAOP']=payload
                pos=0
                while pos<len(payload):
                    length=payload[pos];texts.append(payload[pos+1:pos+1+length].decode());pos+=length+1
        self.assertEqual(sorted(types),[1,12,12,16,16,33,33])
        self.assertIn('cn=1,2,3',texts)
        self.assertIn('ft=0x4803a383,0x0',texts)
        self.assertIn('am=AirTizen1,1',texts)
        feature=[v for v in texts if v.startswith('features=')][0]
        bits=int(feature.split('=')[1].split(',')[0],16)
        self.assertFalse(bits & (1<<2))
        self.assertTrue(bits & (1<<0))
        self.assertTrue(bits & (1<<7))
        with urllib.request.urlopen('http://127.0.0.1:5000/info',timeout=2) as response:info=plistlib.loads(response.read())
        for text in txt_records.values():self.assertEqual(bytes.fromhex(self.txt_fields(text)['pk']),info['pk'])
        self.assertEqual(self.txt_fields(txt_records['txtAirPlay'])['pi'],info['pi'])

    def test_rejects_unsupported_audio_negotiation(self):
        for stream in ({'type':96,'ct':999}, {'type':96,'ct':2,'spf':65536},
                       {'type':96,'ct':2,'sr':192000}, {'type':96,'ct':2,'ch':8},
                       {'type':96,'ct':8,'spf':352}, {'type':96,'ct':'ALAC'}, {'type':96,'ct':2,'audioFormat':1<<19}, {'type':96,'ct':8,'audioFormat':1<<18}, {'type':96,'ct':2,'audioFormat':1<<20,'sr':44100}):
            with self.subTest(stream=stream):
                body=plistlib.dumps({'streams':[stream]},fmt=plistlib.FMT_BINARY)
                self.assertIn(b'400 Bad Request',rtsp('SETUP',body=body))

    def test_mirroring_requires_initial_session(self):
        body = plistlib.dumps({'streams':[{'type':110,'streamConnectionID':1}]},fmt=plistlib.FMT_BINARY)
        self.assertIn(b'400 Bad Request', rtsp('SETUP',body=body))

    def test_zy_discovery_profile_leaves_stopped_receiver_offline(self):
        try:
            self.assertEqual(http('/stop','POST',{'X-AirTizen-Control':'1'})[1]['service'],'offline')
            code,state=http('/discovery/uxplay-test','POST',{'X-AirTizen-Control':'1'})
            self.assertEqual(code,200)
            self.assertEqual(state['service'],'offline')
            self.assertEqual(state['discovery']['profile'],'uxplay-test')
            with socket.socket() as sock:
                sock.settimeout(.2)
                self.assertNotEqual(sock.connect_ex(('127.0.0.1',5000)),0)
        finally:
            http('/discovery/native','POST',{'X-AirTizen-Control':'1'})
            http('/start','POST',{'X-AirTizen-Control':'1'})

    def test_zz_lifecycle(self):
        for _ in range(3):
            self.assertEqual(http('/stop','POST',{'X-AirTizen-Control':'1'})[1]['service'], 'offline')
            self.assertEqual(http('/start','POST',{'X-AirTizen-Control':'1'})[1]['service'], 'running')
            self.assertIn(b'200 OK', rtsp('OPTIONS'))
        self.assertEqual(http('/start','POST',{'X-AirTizen-Control':'1'})[1]['service'], 'running')


class TokenControl(unittest.TestCase):
    def test_runtime_requires_configured_nonce(self):
        token=secrets.token_hex(32)
        with tempfile.TemporaryFile() as log:
            process=subprocess.Popen([str(BINARY)],stdout=log,stderr=log,env=dict(os.environ,AIRTIZEN_CONTROL_TOKEN=token))
            try:
                for _ in range(50):
                    self.assertIsNone(process.poll())
                    try:
                        if http()[0]==403: break
                    except OSError: time.sleep(.1)
                self.assertEqual(http()[0],403)
                self.assertEqual(http(headers={'Origin':'null','X-AirTizen-Control':'1'})[0],403)
                self.assertEqual(http(headers={'X-AirTizen-Control':'0'*64})[0],403)
                self.assertEqual(http(headers={'X-AirTizen-Control':token})[0],200)
                self.assertEqual(http('/status?token='+token)[0],200)
                self.assertEqual(http('/status?token='+'0'*64)[0],403)
                self.assertEqual(http('/stop?token='+token,'POST',{'X-AirTizen-Control':'1'})[0],403)
                self.assertEqual(http('/discovery/uxplay-test?token='+token,'POST',{'X-AirTizen-Control':'1'})[0],403)
                self.assertEqual(http('/discovery/uxplay-test','POST',{'X-AirTizen-Control':'0'*64})[0],403)
                self.assertEqual(http('/discovery/uxplay-test','POST',{'X-AirTizen-Control':token})[1]['discovery']['profile'],'uxplay-test')
                self.assertEqual(http('/discovery/native','POST',{'X-AirTizen-Control':token})[1]['discovery']['profile'],'native')
                self.assertEqual(http('/stop','POST',{'X-AirTizen-Control':token})[0],200)
                self.assertEqual(http('/start','POST',{'X-AirTizen-Control':token})[0],200)
                # Tizen 3 WebKit's local-file POST origin still requires the
                # private nonce; CORS must echo the exact accepted origin.
                self.assertEqual(http('/stop','POST',{'Origin':'file://'})[0],403)
                self.assertEqual(http('/stop','POST',{'Origin':'file://','X-AirTizen-Control':'1'})[0],403)
                self.assertEqual(http('/stop','POST',{'Origin':'file://','X-AirTizen-Control':token})[0],200)
                request=urllib.request.Request(BASE+'/start',method='POST',headers={'Origin':'file://','X-AirTizen-Control':token})
                with urllib.request.urlopen(request,timeout=5) as response:
                    self.assertEqual(response.headers['Access-Control-Allow-Origin'],'file://')
                    self.assertEqual(json.load(response)['service'],'running')
                with urllib.request.urlopen(urllib.request.Request(BASE+'/status',headers={'Origin':'null','X-AirTizen-Control':token}),timeout=5) as response:
                    self.assertEqual(response.headers['Access-Control-Allow-Origin'],'null')
                for bad in ('file:///untrusted','file://foreign','https://foreign.invalid'):
                    self.assertEqual(http(headers={'Origin':bad,'X-AirTizen-Control':token})[0],403)
                duplicate=('POST /stop HTTP/1.1\r\nHost: 127.0.0.1:45110\r\nOrigin: file://\r\nOrigin: null\r\nX-AirTizen-Control: '+token+'\r\nContent-Length: 0\r\n\r\n').encode()
                self.assertIn(b'403 Forbidden',wire(duplicate))
                with urllib.request.urlopen(urllib.request.Request(BASE+'/start',method='OPTIONS',headers={'Origin':'file://'}),timeout=5) as preflight:
                    self.assertEqual(preflight.status,204)
                    self.assertEqual(preflight.headers['Access-Control-Allow-Origin'],'file://')
                self.assertEqual(http(headers={'Origin':'https://foreign.invalid','X-AirTizen-Control':token})[0],403)
                with urllib.request.urlopen(urllib.request.Request(BASE+'/status',method='OPTIONS',headers={'Origin':'null'}),timeout=2) as preflight:
                    self.assertEqual(preflight.status,204)
                self.assertNotIn(token,json.dumps(http(headers={'X-AirTizen-Control':token})[1]))
            finally:
                process.terminate();process.wait(timeout=5)
            self.assertEqual(process.returncode,0)
        for invalid in ('','0'*63,'x'*64):
            result=subprocess.run([str(BINARY)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env=dict(os.environ,AIRTIZEN_CONTROL_TOKEN=invalid),timeout=5)
            self.assertNotEqual(result.returncode,0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
