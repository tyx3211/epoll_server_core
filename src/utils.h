#ifndef UTILS_H
#define UTILS_H

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

#endif // UTILS_H 