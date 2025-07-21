#define _DEFAULT_SOURCE // For strndup, strcasecmp
#include "auth.h"
#include <string.h>
#include <stdlib.h>
#include <l8w8jwt/decode.h>
#include <l8w8jwt/claim.h> // This is the missing header
#include <l8w8jwt/encode.h> // Required for encoding functions
#include "logger.h"
#include <time.h> // Required for time()

char* authenticate_request(Connection* conn, ServerConfig* config) {
    // 1. Find the Authorization header
    const char* auth_header = NULL;
    for (int i = 0; i < conn->request.header_count; i++) {
        if (strcasecmp(conn->request.headers[i].key, "Authorization") == 0) {
            auth_header = conn->request.headers[i].value;
            break;
        }
    }

    if (!auth_header || strncasecmp(auth_header, "Bearer ", 7) != 0) {
        log_system(LOG_DEBUG, "Auth failed: Missing or malformed Authorization header.");
        return NULL;
    }

    const char* token = auth_header + 7;
    char* username = NULL;

    if (config->jwt_enabled) {
        // --- Validate real JWT ---
        struct l8w8jwt_decoding_params params;
        l8w8jwt_decoding_params_init(&params);
        params.alg = L8W8JWT_ALG_HS256;
        params.jwt = (char*)token;
        params.jwt_length = strlen(token);
        params.verification_key = (unsigned char*)config->jwt_secret;
        params.verification_key_length = strlen(config->jwt_secret);
        params.validate_exp = 1;

        enum l8w8jwt_validation_result validation_result;
        struct l8w8jwt_claim* claims = NULL;
        size_t claims_length = 0;
        int decode_result = l8w8jwt_decode(&params, &validation_result, &claims, &claims_length);

        if (decode_result == L8W8JWT_SUCCESS && validation_result == L8W8JWT_VALID) {
            struct l8w8jwt_claim* sub_claim = l8w8jwt_get_claim(claims, claims_length, "sub", 3);
            if (sub_claim && sub_claim->type == L8W8JWT_CLAIM_TYPE_STRING) {
                // CORRECTED ACCESS: The 'value' field is a direct char*, not a union.
                username = strdup(sub_claim->value);
            } else {
                 log_system(LOG_WARNING, "JWT is valid, but 'sub' claim is missing or not a string.");
            }
        } else {
            log_system(LOG_INFO, "JWT validation failed. Decode status: %d, Validation result: %d", decode_result, validation_result);
        }
        
        l8w8jwt_free_claims(claims, claims_length);

    } else {
        // --- "Validate" mock token ---
        if (strlen(token) > 0) {
            username = strdup(token);
        } else {
            log_system(LOG_DEBUG, "Auth failed: Mock token is empty.");
        }
    }

    return username;
}

char* generate_token_for_user(const char* username, ServerConfig* config) {
    if (!username) return NULL;

    if (config->jwt_enabled) {
        // --- Generate a real JWT ---
        char* jwt = NULL;
        size_t jwt_length;
        struct l8w8jwt_encoding_params params;
        l8w8jwt_encoding_params_init(&params);

        params.alg = L8W8JWT_ALG_HS256;
        params.sub = (char*)username;
        params.iss = "my-web-server";
        params.iat = time(NULL);
        params.exp = time(NULL) + (15 * 60);
        params.secret_key = (unsigned char*)config->jwt_secret;
        params.secret_key_length = strlen(config->jwt_secret);
        params.out = &jwt;
        params.out_length = &jwt_length;
        
        int r = l8w8jwt_encode(&params);
        if (r == L8W8JWT_SUCCESS && jwt) {
            // The library allocates memory for jwt, which we pass on to the caller.
            // The caller (api.c) is responsible for building the JSON and freeing this.
            return jwt; 
        } else {
            log_system(LOG_ERROR, "Failed to create JWT token: %d", r);
            l8w8jwt_free(jwt); // Free any partial allocation
            return NULL;
        }
    } else {
        // --- Generate a mock token (just the username) ---
        return strdup(username);
    }
} 