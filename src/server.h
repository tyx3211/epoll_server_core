#ifndef SERVER_H
#define SERVER_H

#include <netinet/in.h> // For INET_ADDRSTRLEN

/**
 * @brief Starts the web server.
 *
 * This function initializes and starts the web server. It will block
 * until the server is stopped.
 *
 * @param configFilePath Path to the server configuration file.
 *                       If NULL, default settings will be used.
 */
void startServer(const char* configFilePath);

#endif // SERVER_H 