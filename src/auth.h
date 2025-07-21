#ifndef AUTH_H
#define AUTH_H

#include "http.h"
#include "config.h"

/**
 * @brief Authenticates an incoming request based on the Authorization header.
 * 
 * This function encapsulates the logic for both real JWT validation and mock token validation,
 * based on the server's configuration (jwt_enabled).
 * 
 * It checks for a "Bearer <token>" in the Authorization header.
 * If validation is successful, it returns the username (e.g., from the 'sub' claim).
 * 
 * @param conn The connection object, containing the parsed request.
 * @param config The server configuration, containing JWT settings.
 * @return A dynamically allocated string with the username on success (caller must free).
 *         Returns NULL on any authentication failure (missing header, invalid token, etc.).
 */
char* authenticate_request(Connection* conn, ServerConfig* config);

#endif // AUTH_H 