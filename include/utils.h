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

#endif // UTILS_H 