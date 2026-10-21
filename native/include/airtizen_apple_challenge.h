#ifndef AIRTIZEN_APPLE_CHALLENGE_H
#define AIRTIZEN_APPLE_CHALLENGE_H
#include <stddef.h>

/* RSA-2048 response, including room for base64 padding and a terminator. */
#define AIRTIZEN_APPLE_RESPONSE_SIZE 345

/* Returns 0 on success, -1 for malformed input, -2 on signing failure.
 * The legacy RAOP payload is challenge + local socket IP + advertised ID,
 * zero-padded to at least 32 bytes, then raw PKCS#1 v1.5 type-1 signed.
 * This is a compatibility proof, not device authentication or sender pairing. */
int airtizen_apple_response(const char *challenge, const unsigned char *local_ip,
    size_t ip_length, const char *device_id, char response[AIRTIZEN_APPLE_RESPONSE_SIZE]);
#endif
