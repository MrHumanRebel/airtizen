/* Validation boundary for the legacy donor handlers. Included only by raop.c. */
#ifndef AIRTIZEN_RAOP_GUARD_H
#define AIRTIZEN_RAOP_GUARD_H
static int airtizen_validate_request(raop_conn_t *conn, http_request_t *request) {
    const char *method = http_request_get_method(request);
    const char *url = http_request_get_url(request);
    const char *type = http_request_get_header(request, "Content-Type");
    const unsigned char *data;
    int length;
    data = (const unsigned char *)http_request_get_data(request, &length);
    if (!method || !url || length < 0 || length > 1024 * 1024) return 400;
    if (!strcmp(method, "SET_PARAMETER") && !type) return 400;
    if (!strcmp(method, "POST")) {
        if (!strcmp(url, "/pair-setup")) return length == 32 ? 0 : 400;
        if (!strcmp(url, "/pair-verify"))
            return length == 68 && data && (data[0] == 0 || data[0] == 1) ? 0 : 400;
        if (!strcmp(url, "/fp-setup")) return length == 16 || length == 164 ? 0 : 400;
        if (!strcmp(url, "/feedback")) return 0;
        return 404;
    }
    if (!strcmp(method, "SETUP") || (!strcmp(method, "TEARDOWN") && length)) {
        plist_t root = NULL, iv, key, streams;
        int valid = 0;
        if (!data || length < 40 || memcmp(data, "bplist00", 8)) return 400;
        plist_from_bin((const char *)data, (uint32_t)length, &root);
        if (!root || plist_get_node_type(root) != PLIST_DICT) { plist_free(root); return 400; }
        iv = plist_dict_get_item(root, "eiv");
        key = plist_dict_get_item(root, "ekey");
        streams = plist_dict_get_item(root, "streams");
        if (!strcmp(method, "TEARDOWN")) valid = 1;
        else if (iv && key) {
            char *bytes = NULL;
            uint64_t size = 0;
            if (plist_get_node_type(iv) == PLIST_DATA) plist_get_data_val(iv, &bytes, &size);
            valid = size == 16 && bytes; free(bytes); bytes = NULL; size = 0;
            if (plist_get_node_type(key) == PLIST_DATA) plist_get_data_val(key, &bytes, &size);
            valid = valid && size == 72 && bytes; free(bytes);
            /* Legacy RTP implementations require non-null peer name and ID. */
            valid = valid && plist_get_node_type(plist_dict_get_item(root, "name")) == PLIST_STRING &&
                plist_get_node_type(plist_dict_get_item(root, "deviceID")) == PLIST_STRING &&
                plist_get_node_type(plist_dict_get_item(root, "timingPort")) == PLIST_UINT;
        } else if (streams && plist_get_node_type(streams) == PLIST_ARRAY && plist_array_get_size(streams) == 1) {
            plist_t stream = plist_array_get_item(streams, 0);
            uint64_t stream_type = 0;
            plist_get_uint_val(plist_dict_get_item(stream, "type"), &stream_type);
            if (stream_type == 110) {
                plist_t identifier=plist_dict_get_item(stream,"streamConnectionID");
                valid=conn->raop_rtp_mirror && identifier && plist_get_node_type(identifier)==PLIST_UINT;
                plist_free(root);return valid ? 0 : 400;
            }
            valid = stream_type == 96 && conn->raop_rtp;
            if (valid) {
                unsigned ct,frames,rate,channels;
                valid=airtizen_audio_parameters(stream,&ct,&frames,&rate,&channels)==0;
            }
        }
        plist_free(root);
        return valid ? 0 : 400;
    }
    if (!strcmp(method, "GET")) {
        unsigned flags;
        return airtizen_info_path(url) ? (airtizen_info_qualifiers(request, &flags) ? 400 : 0) : 404;
    }
    if (!strcmp(method, "OPTIONS") || !strcmp(method, "RECORD") ||
        !strcmp(method, "FLUSH") || !strcmp(method, "GET_PARAMETER") ||
        !strcmp(method, "SET_PARAMETER") || !strcmp(method, "TEARDOWN")) return 0;
    return 501;
}
#endif
