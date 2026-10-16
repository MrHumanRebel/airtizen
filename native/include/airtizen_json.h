#ifndef AIRTIZEN_JSON_H
#define AIRTIZEN_JSON_H
#include <stddef.h>
/* Quote a bounded string as JSON, always including quotes and NUL. */
int airtizen_json_quote(char *out, size_t capacity, const char *text);
#endif
