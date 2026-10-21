#ifndef AIRTIZEN_CAPABILITIES_H
#define AIRTIZEN_CAPABILITIES_H
#include <stdint.h>
/* Audio, mirroring, rotation, legacy pairing and RAOP. Photo upload/cache. Do not advertise slideshow, DRM,
 * multiroom or unsupported authentication modes. */
#define AIRTIZEN_FEATURES 0x4803a383ULL
#define AIRTIZEN_NATIVE_FEATURES_TXT "0x4803a383,0x0"
#define AIRTIZEN_FEATURES_TXT "features=" AIRTIZEN_NATIVE_FEATURES_TXT
#define AIRTIZEN_SOURCE_VERSION "220.68"
/* Explicit developer experiment requested for the iOS discovery investigation.
 * This changes advertised negotiation data, not the implemented feature set. */
#define AIRTIZEN_UXPLAY_TEST_FEATURES 0x5A7FFEE6ULL
#define AIRTIZEN_UXPLAY_TEST_FEATURES_TXT "0x5A7FFEE6,0x0"
#ifdef __cplusplus
extern "C" {
#endif
int airtizen_discovery_set_profile(const char *profile);
const char *airtizen_discovery_profile(void);
const char *airtizen_discovery_model(void);
const char *airtizen_discovery_features_txt(void);
uint64_t airtizen_discovery_features(void);
#ifdef __cplusplus
}
#endif
#endif
