#include "airtizen_apple_challenge.h"
#include "airport_key.h"
#include "rsakey.h"
#include <string.h>

/* Reject invalid encodings before entering the donor's permissive decoder.
 * A challenge contains 1..16 bytes, with optional canonical base64 padding. */
static int valid_challenge(const char *text) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t length, encoded, i, padding;
    unsigned residue = 0, bits = 0;
    if (!text) return 0;
    length = strlen(text);
    if (length < 2 || length > 24) return 0;
    encoded = length;
    while (encoded && text[encoded - 1] == '=') --encoded;
    padding = length - encoded;
    if (encoded < 2 || encoded > 22 || encoded % 4 == 1 || padding > 2) return 0;
    if (padding && (length % 4 || padding != (4 - encoded % 4) % 4)) return 0;
    for (i = 0; i < encoded; ++i) {
        const char *digit = strchr(alphabet, text[i]);
        if (!digit) return 0;
        residue = (residue << 6) | (unsigned)(digit - alphabet);
        bits += 6;
        if (bits >= 8) { bits -= 8; residue &= (1U << bits) - 1; }
    }
    return residue == 0;
}

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

int airtizen_apple_response(const char *challenge, const unsigned char *local_ip,
    size_t ip_length, const char *device_id, char response[AIRTIZEN_APPLE_RESPONSE_SIZE]) {
    unsigned char hardware[6];
    rsakey_t *key;
    unsigned i;
    int result;
    if (!response) return -1;
    response[0] = 0;
    if (!valid_challenge(challenge) || !local_ip || (ip_length != 4 && ip_length != 16) ||
        !device_id || strlen(device_id) != 17) return -1;
    for (i = 0; i < 6; ++i) {
        int high = hex_digit(device_id[3*i]), low = hex_digit(device_id[3*i + 1]);
        if (high < 0 || low < 0 || (i < 5 && device_id[3*i + 2] != ':')) return -1;
        hardware[i] = (unsigned char)((high << 4) | low);
    }
    key = rsakey_init_pem(airtizen_airport_key);
    if (!key) return -2;
    result = rsakey_sign(key, response, AIRTIZEN_APPLE_RESPONSE_SIZE, challenge,
        (unsigned char *)local_ip, (int)ip_length, hardware, sizeof(hardware));
    rsakey_destroy(key);
    if (result) { response[0] = 0; return -2; }
    return 0;
}
