#!/usr/bin/env python3
"""Real process/socket integration checks; no Apple-device claims."""
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import plistlib
import socket
import struct
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request
from cryptography.hazmat.primitives.asymmetric import ed25519, x25519
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
        signing = ed25519.Ed25519PrivateKey.generate()
        ephemeral = x25519.X25519PrivateKey.generate()
        signing_pub = signing.public_key().public_bytes_raw()
        ephemeral_pub = ephemeral.public_key().public_bytes_raw()
        with socket.create_connection(('127.0.0.1',5000),timeout=4) as sock:
            def exchange(path, body, sequence, method="POST"):
                request=(f'{method} {path} RTSP/1.0\r\nCSeq: {sequence}\r\nContent-Length: {len(body)}\r\n\r\n').encode()+body
                sock.sendall(request)
                response=b''
                while b'\r\n\r\n' not in response:
                    response+=sock.recv(8192)
                header,payload=response.split(b'\r\n\r\n',1)
                self.assertIn(b'200 OK',header)
                fields=dict(line.split(b': ',1) for line in header.split(b'\r\n')[1:])
                length=int(fields.get(b'Content-Length',b'0'))
                while len(payload)<length:
                    block=sock.recv(length-len(payload))
                    if not block: self.fail('Receiver closed handshake early')
                    payload+=block
                return payload
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
        self.assertEqual(info['features'],0x48038381)
        self.assertEqual({entry['type'] for entry in info['audioFormats']},{100,101})
        self.assertTrue(all(entry['audioOutputFormats']==0x1d40000 for entry in info['audioFormats']))

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
        position=12;types=[];texts=[]
        for _ in range(7):
            owner,position=name(packet,position)
            kind,cls,ttl,size=struct.unpack('!HHIH',packet[position:position+10]);position+=10
            payload=packet[position:position+size];position+=size;types.append(kind)
            self.assertEqual(ttl,120)
            if kind!=12:self.assertTrue(cls&0x8000)
            if kind==33:self.assertEqual(struct.unpack('!H',payload[4:6])[0],5000)
            if kind==1:self.assertFalse(socket.inet_ntoa(payload).startswith('127.'))
            if kind==16:
                pos=0
                while pos<len(payload):
                    length=payload[pos];texts.append(payload[pos+1:pos+1+length].decode());pos+=length+1
        self.assertEqual(sorted(types),[1,12,12,16,16,33,33])
        self.assertIn('cn=1,2,3',texts)
        feature=[v for v in texts if v.startswith('features=')][0]
        bits=int(feature.split('=')[1].split(',')[0],16)
        self.assertFalse(bits & (1<<2))
        self.assertTrue(bits & (1<<0))
        self.assertTrue(bits & (1<<7))

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

    def test_zz_lifecycle(self):
        for _ in range(3):
            self.assertEqual(http('/stop','POST',{'X-AirTizen-Control':'1'})[1]['service'], 'offline')
            self.assertEqual(http('/start','POST',{'X-AirTizen-Control':'1'})[1]['service'], 'running')
            self.assertIn(b'200 OK', rtsp('OPTIONS'))
        self.assertEqual(http('/start','POST',{'X-AirTizen-Control':'1'})[1]['service'], 'running')


if __name__ == '__main__':
    unittest.main(verbosity=2)
