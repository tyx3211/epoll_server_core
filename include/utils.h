#ifndef UTILS_H
#define UTILS_H

#include <sys/types.h> // For ssize_t, size_t

/**
 * @brief Decodes a URL-encoded string.
 * The caller is responsible for freeing the returned string.
 * @param str The URL-encoded string.
 * @return A new, dynamically allocated string with the decoded content,
 *         or NULL on failure.
 */
char* urlDecode(const char* str);

/**
 * @brief Determines the MIME type of a file based on its extension.
 * @param path The path to the file.
 * @return A string literal representing the MIME type. 
 *         Defaults to "application/octet-stream" if the type is unknown.
 */
const char* getMimeType(const char* path);

/**
 * @brief Parses a URL-encoded string (query string or POST body) to find the value of a specific key.
 * 
 * The caller is responsible for freeing the returned string.
 * The input string `str` is not modified.
 * 
 * @param str The URL-encoded string (e.g., "key1=val1&key2=val2").
 * @param key The key to search for.
 * @return A dynamically allocated string containing the decoded value, or NULL if the key is not found.
 */
char* get_query_param(const char* str, const char* key);

// Include http.h for HttpRequest type (Phase 2)
#include "http.h"

/**
 * @brief Parse all parameters from a URL-encoded string into a QueryParam array.
 * 
 * @param str The URL-encoded string (e.g., "key1=val1&key2=val2").
 * @param params Output array of QueryParam structs.
 * @param max_params Maximum number of parameters to parse.
 * @return Number of parameters parsed.
 */
int parse_params(const char* str, void* params, int max_params);

/**
 * @brief Parse query string and body parameters from an HttpRequest.
 * 
 * This should be called after the request is fully parsed.
 * It will populate query_params[] and body_params[] in the request.
 * 
 * @param req Pointer to the HttpRequest.
 */
void http_parse_all_params(HttpRequest* req);

/**
 * @brief Get a parameter value from the request (checks query_params first, then body_params).
 * 
 * Unlike get_query_param(), this does NOT allocate new memory.
 * The returned pointer is valid as long as the HttpRequest is valid.
 * 
 * @param req Pointer to the HttpRequest.
 * @param key The parameter key to look for.
 * @return Pointer to the value string, or NULL if not found.
 */
const char* http_get_param(const HttpRequest* req, const char* key);

/**
 * @brief Get a parameter value from query_params only.
 * 
 * @param req Pointer to the HttpRequest.
 * @param key The parameter key to look for.
 * @return Pointer to the value string, or NULL if not found.
 */
const char* http_get_query_param(const HttpRequest* req, const char* key);

/**
 * @brief Get a parameter value from body_params only.
 * 
 * @param req Pointer to the HttpRequest.
 * @param key The parameter key to look for.
 * @return Pointer to the value string, or NULL if not found.
 */
const char* http_get_body_param(const HttpRequest* req, const char* key);

#endif // UTILS_H 