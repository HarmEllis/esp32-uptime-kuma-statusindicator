#include "auth.h"
#include "storage.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/md.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "auth";

typedef struct {
    char token[AUTH_TOKEN_LEN * 2 + 1];
    int64_t created_at;  /* microseconds */
    bool active;
} session_t;

static session_t s_sessions[AUTH_MAX_SESSIONS];
static char s_last_challenge[AUTH_CHALLENGE_LEN * 2 + 1];
static int64_t s_challenge_time;

/* Rate limiting */
static int s_failure_count = 0;
static int64_t s_first_failure_time = 0;
static int64_t s_lockout_until = 0;

static int64_t now_us(void) { return esp_timer_get_time(); }
static int64_t now_s(void) { return esp_timer_get_time() / 1000000; }

static void bytes_to_hex(const uint8_t *bytes, size_t len, char *hex)
{
    for (size_t i = 0; i < len; i++) {
        sprintf(hex + i * 2, "%02x", bytes[i]);
    }
    hex[len * 2] = '\0';
}

static int hex_to_bytes(const char *hex, uint8_t *bytes, size_t max_len)
{
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0 || hex_len / 2 > max_len) return -1;
    for (size_t i = 0; i < hex_len / 2; i++) {
        unsigned int val;
        if (sscanf(hex + i * 2, "%02x", &val) != 1) return -1;
        bytes[i] = (uint8_t)val;
    }
    return hex_len / 2;
}

esp_err_t auth_init(void)
{
    memset(s_sessions, 0, sizeof(s_sessions));
    memset(s_last_challenge, 0, sizeof(s_last_challenge));
    return ESP_OK;
}

esp_err_t auth_create_challenge(char *challenge_hex, size_t len)
{
    if (len < AUTH_CHALLENGE_LEN * 2 + 1) return ESP_ERR_INVALID_ARG;

    uint8_t nonce[AUTH_CHALLENGE_LEN];
    esp_fill_random(nonce, sizeof(nonce));
    bytes_to_hex(nonce, AUTH_CHALLENGE_LEN, challenge_hex);

    strncpy(s_last_challenge, challenge_hex, sizeof(s_last_challenge));
    s_challenge_time = now_s();

    return ESP_OK;
}

esp_err_t auth_login(const char *challenge_hex, const char *response_hex, char *token_out, size_t token_len)
{
    /* Check lockout */
    if (auth_is_locked_out()) {
        ESP_LOGW(TAG, "Login attempt while locked out");
        return ESP_ERR_INVALID_STATE;
    }

    /* Verify challenge matches last issued */
    if (strcmp(challenge_hex, s_last_challenge) != 0) {
        ESP_LOGW(TAG, "Challenge mismatch");
        goto fail;
    }

    /* Challenge expires after 60 seconds */
    if (now_s() - s_challenge_time > 60) {
        ESP_LOGW(TAG, "Challenge expired");
        goto fail;
    }

    /* Get PSK */
    char psk[PSK_MAX_LEN] = {0};
    if (storage_get_psk(psk, sizeof(psk)) != ESP_OK || psk[0] == '\0') {
        /* No PSK set — allow login with any response (initial setup) */
        ESP_LOGW(TAG, "No PSK set, allowing login");
        goto create_session;
    }

    /* Compute expected HMAC-SHA256(psk, challenge) */
    uint8_t challenge_bytes[AUTH_CHALLENGE_LEN];
    int clen = hex_to_bytes(challenge_hex, challenge_bytes, AUTH_CHALLENGE_LEN);
    if (clen < 0) goto fail;

    uint8_t expected[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (uint8_t *)psk, strlen(psk));
    mbedtls_md_hmac_update(&ctx, challenge_bytes, clen);
    mbedtls_md_hmac_finish(&ctx, expected);
    mbedtls_md_free(&ctx);

    char expected_hex[65];
    bytes_to_hex(expected, 32, expected_hex);

    if (strcmp(expected_hex, response_hex) != 0) {
        ESP_LOGW(TAG, "HMAC mismatch");
        goto fail;
    }

create_session:
    /* Invalidate used challenge */
    s_last_challenge[0] = '\0';
    s_failure_count = 0;

    /* Create session token */
    {
        uint8_t token_bytes[AUTH_TOKEN_LEN];
        esp_fill_random(token_bytes, AUTH_TOKEN_LEN);

        if (token_len < AUTH_TOKEN_LEN * 2 + 1) return ESP_ERR_INVALID_ARG;
        bytes_to_hex(token_bytes, AUTH_TOKEN_LEN, token_out);

        /* Find free or oldest slot (LRU eviction) */
        int slot = -1;
        int64_t oldest = INT64_MAX;
        for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
            if (!s_sessions[i].active) {
                slot = i;
                break;
            }
            if (s_sessions[i].created_at < oldest) {
                oldest = s_sessions[i].created_at;
                slot = i;
            }
        }
        if (slot < 0) slot = 0;

        strncpy(s_sessions[slot].token, token_out, sizeof(s_sessions[slot].token));
        s_sessions[slot].created_at = now_us();
        s_sessions[slot].active = true;

        ESP_LOGI(TAG, "Session created in slot %d", slot);
        return ESP_OK;
    }

fail:
    s_failure_count++;
    if (s_failure_count == 1) {
        s_first_failure_time = now_s();
    } else if (now_s() - s_first_failure_time > AUTH_FAILURE_WINDOW_S) {
        /* Reset window */
        s_failure_count = 1;
        s_first_failure_time = now_s();
    }

    if (s_failure_count >= AUTH_MAX_FAILURES) {
        s_lockout_until = now_s() + AUTH_LOCKOUT_S;
        ESP_LOGW(TAG, "Too many failures, locked out for %d seconds", AUTH_LOCKOUT_S);
        s_failure_count = 0;
    }

    return ESP_ERR_INVALID_RESPONSE;
}

bool auth_validate_token(const char *token)
{
    if (!token || token[0] == '\0') return false;

    int64_t now = now_us();
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (!s_sessions[i].active) continue;

        /* Check expiry */
        if ((now - s_sessions[i].created_at) > (int64_t)AUTH_TOKEN_EXPIRY_S * 1000000) {
            s_sessions[i].active = false;
            continue;
        }

        if (strcmp(s_sessions[i].token, token) == 0) {
            return true;
        }
    }
    return false;
}

void auth_invalidate_all(void)
{
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        s_sessions[i].active = false;
    }
    ESP_LOGI(TAG, "All sessions invalidated");
}

bool auth_check_request(httpd_req_t *req)
{
    char auth_header[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK) {
        return false;
    }

    /* Expect "Bearer <token>" */
    if (strncmp(auth_header, "Bearer ", 7) != 0) {
        return false;
    }

    return auth_validate_token(auth_header + 7);
}

bool auth_is_locked_out(void)
{
    if (s_lockout_until > 0 && now_s() < s_lockout_until) {
        return true;
    }
    if (s_lockout_until > 0 && now_s() >= s_lockout_until) {
        s_lockout_until = 0;
    }
    return false;
}
