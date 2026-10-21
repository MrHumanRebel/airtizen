/* Interoperability vectors computed independently with Python integer RSA,
 * including IPv6's 38-byte payload rather than IPv4's 32-byte minimum.
 * The corresponding public key is tests/fixtures/airport-public.pem. */
#include "airtizen_apple_challenge.h"
#include <assert.h>
#include <string.h>
int main(void) {
    const char *challenge = "AAECAwQFBgcICQoLDA0ODw";
    const unsigned char ip[] = {192, 0, 2, 20};
    char response[AIRTIZEN_APPLE_RESPONSE_SIZE];
    {
        const unsigned char ip[] = {0xc0, 0x00, 0x02, 0x14};
        const char expected[] =
            "aHHIRcElPkObaRHNkdSI4GXqwjxyz9X2LoIBfTYv5jbpk60+VPlK39EhfD2E4KGPm7IXSo7/sCWu"
            "6BOaK88It1XySGYSe6wIYgLipB2gX+OuRTGaUUqadtV+wC2/rol6XxFULRNSFMFtBhRF4y5zeTW8"
            "SYDUJltqcbRHtBdFga5xCWe7yZZEBTqL9JPy0kS7OpLBosz8mheBV22cov8cYP7yOmqm+Y0JqyzS"
            "ciJa09F3RuCzQszlaKJ9SVlDeGeucC0C4+x6l5P7DHA5ZYXeGFHWqRIBt5renSGNOTlvQPS8dbJo"
            "9wTRN5g/aJsAsu2aW3E10giz7MiQ2Bb3R8NOtg"
            ;
        assert(airtizen_apple_response(challenge, ip, sizeof(ip), "02:11:22:33:44:66", response) == 0);
        assert(!strcmp(response, expected));
    }
    {
        const unsigned char ip[] = {0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x34};
        const char expected[] =
            "kWZ3/TWraq+QtCCoF+JH+EL1mS9XQGLtV3/Wyq563fwpAIJ3ejCwWM+Dsa+ohDWLORgpnAwXOroY"
            "AtL/FUCgE/TMeyfQHIymhDCgE6b+i2PxSpJfEUzh2PN76xln7CT5t7/MkR7ypMtF3vuscBLvFrSo"
            "6j2P4jkaJJ7kkbJ4x1hFVci/NmUZlCdi80tRkNvWh5emW3dhPpDagYv9xk3IdnTYtN+7HX7r9A2+"
            "iHpTc2CLr0Sxv4YTbB1nh/RvtE1KeDGSXMsZ9hFic7rrlp3O09SVdXxTbn2KJvPw25bCpt2+spq9"
            "NiEkC63GlbuEcqTnyLBi2WjOklQCcfHxGsp0NQ"
            ;
        assert(airtizen_apple_response(challenge, ip, sizeof(ip), "02:11:22:33:44:66", response) == 0);
        assert(!strcmp(response, expected));
    }

    assert(airtizen_apple_response(challenge, ip, 3, "02:11:22:33:44:66", response) == -1);
    assert(airtizen_apple_response(challenge, NULL, 4, "02:11:22:33:44:66", response) == -1);
    assert(airtizen_apple_response(challenge, ip, 4, "invalid", response) == -1);
    assert(airtizen_apple_response(challenge, ip, 4, "02-11:22:33:44:66", response) == -1);
    assert(airtizen_apple_response(challenge, ip, 4, "02:11:22:33:44:XX", response) == -1);
    assert(airtizen_apple_response(challenge, ip, 4, "02:11:22:33:44:66", NULL) == -1);
    assert(airtizen_apple_response(NULL, ip, 4, "02:11:22:33:44:66", response) == -1);
    assert(!response[0]);
    return 0;
}
