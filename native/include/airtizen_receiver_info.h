/* Legacy AirPlay discovery payloads. Included after the private RAOP types. */
#ifndef AIRTIZEN_RECEIVER_INFO_H
#define AIRTIZEN_RECEIVER_INFO_H

static int airtizen_info_path(const char *url) {
    return url && !strncmp(url, "/info", 5) && (!url[5] || url[5] == '?');
}

static int airtizen_info_qualifiers(http_request_t *request, unsigned *flags) {
    int length;
    const char *data = http_request_get_data(request, &length);
    const char *url = http_request_get_url(request);
    plist_t root = NULL, qualifiers;
    unsigned i;
    *flags = 0;
    if (url && url[5] == '?') {
        if (strstr(url + 6, "txtAirPlay")) *flags |= 1;
        if (strstr(url + 6, "txtRAOP")) *flags |= 2;
    }
    if (!length) return 0;
    if (!data || length < 40 || length > 4096 || memcmp(data, "bplist00", 8)) return -1;
    plist_from_bin(data, (uint32_t)length, &root);
    if (!root || plist_get_node_type(root) != PLIST_DICT) { plist_free(root); return -1; }
    qualifiers = plist_dict_get_item(root, "qualifier");
    if (!qualifiers || plist_get_node_type(qualifiers) != PLIST_ARRAY || plist_array_get_size(qualifiers) > 16) {
        plist_free(root); return -1;
    }
    for (i = 0; i < plist_array_get_size(qualifiers); ++i) {
        plist_t item = plist_array_get_item(qualifiers, i); char *value = NULL;
        if (plist_get_node_type(item) != PLIST_STRING) { plist_free(root); return -1; }
        plist_get_string_val(item, &value);
        if (value && !strcmp(value, "txtAirPlay")) *flags |= 1;
        if (value && !strcmp(value, "txtRAOP")) *flags |= 2;
        free(value);
    }
    plist_free(root); return 0;
}

static plist_t airtizen_receiver_info(raop_conn_t *conn, http_request_t *request) {
    unsigned flags = 0, i;
    char receiver[96], device_id[32], uuid[37];
    unsigned char public_key[32], text[512];
    plist_t info = plist_new_dict();
    airtizen_info_qualifiers(request, &flags); /* Shape is checked before dispatch. */
    if (flags) {
        if (flags & 1) { size_t n = airtizen_mdns_txt(1, text, sizeof(text)); plist_dict_set_item(info, "txtAirPlay", plist_new_data((const char *)text, n)); }
        if (flags & 2) { size_t n = airtizen_mdns_txt(0, text, sizeof(text)); plist_dict_set_item(info, "txtRAOP", plist_new_data((const char *)text, n)); }
        return info;
    }
    airtizen_mdns_identity(receiver, sizeof(receiver), device_id, sizeof(device_id));
    airtizen_mdns_pairing_id(uuid);
    pairing_get_public_key(conn->raop->pairing, public_key);
    plist_dict_set_item(info, "name", plist_new_string(receiver));
    plist_dict_set_item(info, "deviceID", plist_new_string(device_id));
    plist_dict_set_item(info, "macAddress", plist_new_string(device_id));
    plist_dict_set_item(info, "model", plist_new_string("AirTizen1,1"));
    plist_dict_set_item(info, "sourceVersion", plist_new_string("220.68"));
    plist_dict_set_item(info, "features", plist_new_uint(AIRTIZEN_FEATURES));
    plist_dict_set_item(info, "statusFlags", plist_new_uint(4));
    plist_dict_set_item(info, "vv", plist_new_uint(2));
    plist_dict_set_item(info, "pi", plist_new_string(uuid));
    plist_dict_set_item(info, "pk", plist_new_data((const char *)public_key, sizeof(public_key)));
    plist_dict_set_item(info, "keepAliveLowPower", plist_new_bool(0));
    plist_dict_set_item(info, "keepAliveSendStatsAsBody", plist_new_bool(0));
    plist_dict_set_item(info, "initialVolume", plist_new_real(0.0));
    {
        plist_t formats = plist_new_array(), latencies = plist_new_array();
        for (i = 100; i <= 101; ++i) {
            plist_t format = plist_new_dict(), latency = plist_new_dict();
            plist_dict_set_item(format, "type", plist_new_uint(i));
            plist_dict_set_item(format, "audioInputFormats", plist_new_uint(0x1d40000));
            plist_dict_set_item(format, "audioOutputFormats", plist_new_uint(0x1d40000));
            plist_array_append_item(formats, format);
            plist_dict_set_item(latency, "type", plist_new_uint(i));
            plist_dict_set_item(latency, "audioType", plist_new_string("default"));
            /* No calibrated latency compensation is implemented. */
            plist_dict_set_item(latency, "inputLatencyMicros", plist_new_uint(0));
            plist_dict_set_item(latency, "outputLatencyMicros", plist_new_uint(0));
            plist_array_append_item(latencies, latency);
        }
        plist_dict_set_item(info, "audioFormats", formats);
        plist_dict_set_item(info, "audioLatencies", latencies);
    }
    {
        plist_t displays = plist_new_array(), display = plist_new_dict();
        /* Conservative negotiation limits for the existing H.264/MSE path;
         * these are not a measurement of the panel or synchronized playback. */
        plist_dict_set_item(display, "uuid", plist_new_string(uuid));
        plist_dict_set_item(display, "width", plist_new_uint(1920));
        plist_dict_set_item(display, "height", plist_new_uint(1080));
        plist_dict_set_item(display, "widthPixels", plist_new_uint(1920));
        plist_dict_set_item(display, "heightPixels", plist_new_uint(1080));
        plist_dict_set_item(display, "widthPhysical", plist_new_uint(0));
        plist_dict_set_item(display, "heightPhysical", plist_new_uint(0));
        plist_dict_set_item(display, "refreshRate", plist_new_real(1.0 / 60.0));
        plist_dict_set_item(display, "maxFPS", plist_new_uint(30));
        plist_dict_set_item(display, "rotation", plist_new_bool(1));
        plist_dict_set_item(display, "overscanned", plist_new_bool(0));
        plist_array_append_item(displays, display);
        plist_dict_set_item(info, "displays", displays);
    }
    return info;
}
#endif
