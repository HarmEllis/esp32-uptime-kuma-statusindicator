#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>
#include <stdint.h>

#define AUTH_CHALLENGE_LEN   32
#define AUTH_TOKEN_LEN       32
#define AUTH_MAX_SESSIONS    4
#define AUTH_TOKEN_EXPIRY_S  3600
#define AUTH_MAX_FAILURES    5
#define AUTH_LOCKOUT_S       300
#define AUTH_FAILURE_WINDOW_S 60

/** Initialize the auth module */
esp_err_t auth_init(void);

/** Generate a challenge nonce (hex string). Caller provides buf of at least AUTH_CHALLENGE_LEN*2+1 */
esp_err_t auth_create_challenge(char *challenge_hex, size_t len);

/**
 * Verify HMAC response and create a session token.
 * challenge_hex: the previously issued challenge
 * response_hex: HMAC-SHA256(psk, challenge) as hex string from client
 * token_out: buffer for the session token (at least AUTH_TOKEN_LEN*2+1)
 * Returns ESP_OK on success, ESP_ERR_INVALID_STATE if locked out.
 */
esp_err_t auth_login(const char *challenge_hex, const char *response_hex, char *token_out, size_t token_len);

/** Validate a Bearer token from an HTTP request. Returns true if valid. */
bool auth_validate_token(const char *token);

/** Invalidate all sessions (e.g., after PSK change) */
void auth_invalidate_all(void);

/** Check if a request has a valid auth token. Returns true if authenticated. */
bool auth_check_request(httpd_req_t *req);

/** Check if rate-limited (locked out) */
bool auth_is_locked_out(void);
