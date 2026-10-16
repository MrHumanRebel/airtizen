#!/usr/bin/env python3
"""Create UTF-8 build copies of legacy UTF-16 AirPlayServerLib sources."""
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: prepare_donor_sources.py AIRPLAY_LIB_DIR OUTPUT_DIR")
source = Path(sys.argv[1])
out = Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)

def read(name: str) -> str:
    data = (source / name).read_bytes()
    return (data.decode("utf-16") if data.startswith((b"\xff\xfe", b"\xfe\xff")) else data.decode("utf-8")).replace("\r\n", "\n")

def write(name: str, text: str) -> None:
    path = out / name
    normalized = text.replace("\r\n", "\n")
    if not path.exists() or path.read_text(encoding="utf-8") != normalized:
        path.write_text(normalized, encoding="utf-8")

handlers = read("raop_handlers.h").replace('"plist/include/plist.h"', '"plist/plist/plist.h"')
handlers = handlers.replace('#include "plist/plist/plist.h"', '#include "plist/plist/plist.h"\n#include "airtizen_audio_parameters.h"')
handlers = handlers.replace('if (!strcmp(content_type,', 'if (content_type && !strcmp(content_type,')
# Enforce short-circuit on the image/png branch too.
handlers = handlers.replace('|| !strcmp(content_type, "image/png")', '|| (content_type && !strcmp(content_type, "image/png"))')
# Donor otherwise leaks request plists and encryption material on every setup.
handlers = handlers.replace('memcpy(aesiv, eiv, 16);', 'memcpy(aesiv, eiv, 16);\n        free(eiv);')
handlers = handlers.replace('int ret = fairplay_decrypt(conn->fairplay, ekey, aeskey);', 'int ret = fairplay_decrypt(conn->fairplay, ekey, aeskey);\n        free(ekey);\n        if (ret != 0) { plist_free(root_node); http_response_set_disconnect(response, 1); return; }')
handlers = handlers.replace('http_response_add_header(response, "Content-Type", "application/octet-stream");\n\t\tbreak;\n\t}', 'if (conn->raop->callbacks.pairing_verified) conn->raop->callbacks.pairing_verified(conn->raop->callbacks.cls);\n        http_response_add_header(response, "Content-Type", "application/octet-stream");\n\t\tbreak;\n\t}')
start = handlers.index('\t// Binary plist with display capabilities:')
end = handlers.index('\n}\n\nstatic void\nraop_handler_pairsetup', start)
handlers = handlers[:start] + """    unsigned char public_key[32];
    char receiver[96], device_id[32];
    char *encoded = NULL;
    uint32_t encoded_size = 0;
    plist_t info = plist_new_dict();
    airtizen_mdns_identity(receiver, sizeof(receiver), device_id, sizeof(device_id));
    pairing_get_public_key(conn->raop->pairing, public_key);
    plist_dict_set_item(info, "name", plist_new_string(receiver));
    plist_dict_set_item(info, "deviceID", plist_new_string(device_id));
    plist_dict_set_item(info, "macAddress", plist_new_string(device_id));
    plist_dict_set_item(info, "model", plist_new_string("AirTizen1,1"));
    plist_dict_set_item(info, "sourceVersion", plist_new_string("220.68"));
    plist_dict_set_item(info, "features", plist_new_uint(AIRTIZEN_FEATURES));
    plist_dict_set_item(info, "statusFlags", plist_new_uint(4));
    {
        unsigned i;plist_t formats=plist_new_array();
        for(i=100;i<=101;++i){plist_t item=plist_new_dict();plist_dict_set_item(item,"type",plist_new_uint(i));plist_dict_set_item(item,"audioInputFormats",plist_new_uint(0x1d40000));plist_dict_set_item(item,"audioOutputFormats",plist_new_uint(0x1d40000));plist_array_append_item(formats,item);}
        plist_dict_set_item(info,"audioFormats",formats);
    }
    plist_dict_set_item(info, "vv", plist_new_uint(2));
    plist_dict_set_item(info, "pk", plist_new_data((const char *)public_key, sizeof(public_key)));
    plist_to_bin(info, &encoded, &encoded_size);
    plist_free(info);
    if (encoded && encoded_size <= 65536) {
        *response_data = encoded;
        *response_datalen = (int)encoded_size;
        http_response_add_header(response, "Content-Type", "application/x-apple-binary-plist");
    } else { free(encoded); http_response_set_disconnect(response, 1); }
""" + handlers[end:]
handlers = '#include "airtizen_mdns.h"\n#include "airtizen_capabilities.h"\n' + handlers
handlers = handlers.replace('raop_rtp_start_audio(conn->raop_rtp, use_udp, remote_cport, remote_tport, &cport, &tport, &dport);', """unsigned ct=8,frames=480,rate=44100,channels=2;
            plist_t value;
            if(airtizen_audio_parameters(stream_note,&ct,&frames,&rate,&channels)!=0){plist_free(root_node);http_response_set_disconnect(response,1);return;}
            value = plist_dict_get_item(stream_note, "controlPort");
            if (value) { uint64_t control = 0; plist_get_uint_val(value, &control); remote_cport = (unsigned short)control; }
            if (raop_rtp_configure_audio(conn->raop_rtp, (unsigned)ct, (unsigned)frames, (unsigned)rate, (unsigned)channels) != 0) {
                plist_free(root_node); http_response_set_disconnect(response, 1); return;
            }
            raop_rtp_start_audio(conn->raop_rtp, 1, remote_cport, remote_tport, &cport, &tport, &dport);""")
write("raop_handlers.generated.h", handlers)

raop = read("raop.c").replace('"raop_handlers.h"', '"raop_handlers.generated.h"')
raop = raop.replace('struct raop_conn_s {', 'struct raop_conn_s {\n    int reverse, event_waiting, event_state;\n    unsigned event_generation;\n    uint64_t event_time;\n    char event_session[65];')
raop = raop.replace('#include "raop_handlers.generated.h"', '#include "raop_handlers.generated.h"\n#include "airtizen_raop_guard.h"\n#include "airtizen_airplay_http.h"\n#include "airtizen_reverse.h"')
raop = raop.replace('if (!method || !cseq) {', 'if (conn->reverse) { if (!url) conn->event_waiting=0; else conn->reverse=-1; return; }\n    if (airtizen_reverse_upgrade(conn, request, response)) return;\n    if (airtizen_airplay_http(request, response)) return;\n    if (method && url && !cseq) { airtizen_http_reply(response, 404, NULL, NULL, 0); return; }\n\tif (!method || !url || !cseq) {')
raop = raop.replace('*response = http_response_init("RTSP/1.0", 200, "OK");', """int validation = airtizen_validate_request(conn, request);
    if (validation) {
        *response = http_response_init("RTSP/1.0", validation,
            validation == 501 ? "Not Implemented" : validation == 404 ? "Not Found" : "Bad Request");
        http_response_add_header(*response, "CSeq", cseq);
        http_response_set_disconnect(*response, 1);
        http_response_finish(*response, NULL, 0);
        return;
    }
    *response = http_response_init("RTSP/1.0", 200, "OK");""")
raop = raop.replace("httpd_cbs.conn_destroy = &conn_destroy;", "httpd_cbs.conn_destroy = &conn_destroy;\n    httpd_cbs.conn_poll = &airtizen_reverse_poll;")
write("raop.generated.c", raop)

buffer = read("raop_buffer.c").replace("#include <sha512.h>", '#include "ed25519/sha512.h"')
buffer = '#include "airtizen_alac.h"\n#include "airtizen_aac.h"\n' + buffer
start = buffer.index('#include "fdk-aac/libAACdec/include/aacdecoder_lib.h"')
end = buffer.index('#include "stream.h"', start) + len('#include "stream.h"')
buffer = buffer[:start] + buffer[end:]
buffer = buffer.replace('HANDLE_AACDECODER phandle;', 'airtizen_aac *phandle;\n    airtizen_alac *alac;\n    unsigned codec_type, configured_rate, configured_channels;')
start = buffer.index('static int fdk_flags')
end = buffer.index('void\nraop_buffer_init_key_iv', start)
buffer = buffer[:start] + buffer[end:]
buffer = buffer.replace('raop_buffer->logger = logger;', 'raop_buffer->logger = logger;\n    raop_buffer->codec_type=8;raop_buffer->configured_rate=44100;raop_buffer->configured_channels=2;')
buffer = buffer.replace('create_fdk_aac_decoder(logger)', 'airtizen_aac_create(8,44100,2,480)')
buffer = buffer.replace('free(raop_buffer->phandle);', 'airtizen_aac_destroy(raop_buffer->phandle);')
buffer = buffer.replace('aacDecoder_Close(raop_buffer->phandle);', 'airtizen_aac_destroy(raop_buffer->phandle);')
buffer = buffer.replace('airtizen_aac_destroy(raop_buffer->phandle);\n\t\tfree(raop_buffer->buffer);', 'airtizen_aac_destroy(raop_buffer->phandle);\n        airtizen_alac_destroy(raop_buffer->alac);\n\t\tfree(raop_buffer->buffer);')
buffer = buffer.replace('audio_buffer_size = 480 * 2 * 2;', 'audio_buffer_size = 4096 * 2 * 2;')
buffer = buffer.replace('if (datalen < 12 ||', 'if (!data || datalen <= 12 ||')
buffer = buffer.replace('return -1;\n    }\n    unsigned short seqnum', 'return 0;\n    }\n    unsigned short seqnum')
buffer = buffer.replace('memset(packetbuf, 0, payloadsize);', 'if (!packetbuf) { entry->available=0; return 0; }\n    memset(packetbuf,0,payloadsize);')
for index in (4, 8):
    buffer = buffer.replace(f'(data[{index}] << 24)', f'((uint32_t)data[{index}] << 24)')
start = buffer.index('\t// aac decode to pcm')
end = buffer.index('\n#ifdef DUMP_AUDIO', start)
buffer = buffer[:start] + """    entry->audio_buffer_len=(int)(raop_buffer->codec_type==2 ?
        airtizen_alac_decode(raop_buffer->alac,packetbuf,(size_t)payloadsize,entry->audio_buffer,(size_t)entry->audio_buffer_size) :
        airtizen_aac_decode(raop_buffer->phandle,packetbuf,(size_t)payloadsize,entry->audio_buffer,(size_t)entry->audio_buffer_size));
    entry->sample_rate=raop_buffer->configured_rate;
    entry->channels=(uint16_t)raop_buffer->configured_channels;entry->bits_per_sample=16;
""" + buffer[end:]
buffer = buffer.replace('/* Update the raop_buffer seqnums */', 'if (!entry->audio_buffer_len) { free(packetbuf);entry->available=0;return 0; }\n    /* Update the raop_buffer seqnums */')
buffer += """
int raop_buffer_configure_audio(raop_buffer_t *buffer,unsigned ct,unsigned frames,unsigned rate,unsigned channels) {
    airtizen_alac *alac=NULL;airtizen_aac *aac=NULL;
    if(!buffer)return -1;
    if(ct==2){alac=airtizen_alac_create(rate,channels,frames);if(!alac)return -1;}
    else if(ct==4 || ct==8){aac=airtizen_aac_create(ct,rate,channels,frames);if(!aac)return -1;}
    else return -1;
    raop_buffer_flush(buffer,-1);airtizen_alac_destroy(buffer->alac);airtizen_aac_destroy(buffer->phandle);
    buffer->alac=alac;buffer->phandle=aac;buffer->codec_type=ct;
    buffer->configured_rate=rate;buffer->configured_channels=channels;return 0;
}
"""
write("raop_buffer.generated.c", buffer)

mirror = read("raop_rtp_mirror.c")
mirror = mirror.replace("const char remoteName[128];", "char remoteName[128];")
mirror = mirror.replace("const char remoteDeviceId[128];", "char remoteDeviceId[128];")
mirror = mirror.replace(
    "strncpy(raop_rtp_mirror->remoteName, remoteName, min(128, strlen(remoteName)));",
    'snprintf(raop_rtp_mirror->remoteName, sizeof(raop_rtp_mirror->remoteName), "%s", remoteName);',
)
mirror = mirror.replace(
    "strncpy(raop_rtp_mirror->remoteDeviceId, remoteDeviceId, min(128, strlen(remoteDeviceId)));",
    'snprintf(raop_rtp_mirror->remoteDeviceId, sizeof(raop_rtp_mirror->remoteDeviceId), "%s", remoteDeviceId);',
)
mirror = mirror.replace("thread_exit_exception != NULL", "thread_exit_exception != 0")
mirror = mirror.replace("thread_exit_exception = NULL", "thread_exit_exception = 0")
start = mirror.index('static THREAD_RETVAL\nraop_rtp_mirror_thread(void *arg)')
end = mirror.index('\nvoid\nraop_rtp_start_mirror(', start)
mirror = mirror[:start] + '#include "airtizen_mirror_receive.inc"\n' + mirror[end:]
mirror = mirror.replace('if (raop_rtp_mirror->callbacks.disconnected != NULL) {', 'if (raop_rtp_mirror->callbacks.video_flush) raop_rtp_mirror->callbacks.video_flush(raop_rtp_mirror->callbacks.cls);\n    if (raop_rtp_mirror->callbacks.disconnected != NULL) {')
write("raop_rtp_mirror.generated.c", mirror)
