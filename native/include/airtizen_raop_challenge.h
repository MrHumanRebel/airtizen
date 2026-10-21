/* Included after the private RAOP connection types. */
#ifndef AIRTIZEN_RAOP_CHALLENGE_H
#define AIRTIZEN_RAOP_CHALLENGE_H
#include "airtizen_apple_challenge.h"

static int airtizen_raop_challenge(raop_conn_t *conn, http_request_t *request,
                                   http_response_t **response) {
    const char *challenge = http_request_get_header(request, "Apple-Challenge");
    char receiver[96], device_id[32], text[AIRTIZEN_APPLE_RESPONSE_SIZE];
    int result;
    if (!challenge) {
        if (!strcmp(http_request_get_method(request), "OPTIONS"))
            logger_log(conn->raop->logger, LOGGER_INFO, "OPTIONS probe: Apple-Challenge absent");
        return 0;
    }
    airtizen_mdns_identity(receiver, sizeof(receiver), device_id, sizeof(device_id));
    result = airtizen_apple_response(challenge, conn->local, (size_t)conn->locallen, device_id, text);
    if (result) {
        const char *cseq = http_request_get_header(request, "CSeq");
        logger_log(conn->raop->logger, LOGGER_WARNING,
            result == -1 ? "Apple-Challenge rejected: malformed input" : "Apple-Challenge signing failed");
        http_response_destroy(*response);
        *response = http_response_init(http_request_get_protocol(request), result == -1 ? 400 : 503,
            result == -1 ? "Bad Request" : "Service Unavailable");
        if (cseq) http_response_add_header(*response, "CSeq", cseq);
        http_response_set_disconnect(*response, 1);
        http_response_finish(*response, NULL, 0);
        return 1;
    }
    http_response_add_header(*response, "Apple-Response", text);
    logger_log(conn->raop->logger, LOGGER_INFO, "Apple-Challenge answered: RSA-2048 response signed");
    return 0;
}
#endif
