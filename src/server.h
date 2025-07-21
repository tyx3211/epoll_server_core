#ifndef SERVER_H
#define SERVER_H

#include <netinet/in.h> // For INET_ADDRSTRLEN
#include <stddef.h> // For size_t

// Forward declaration of Connection struct to avoid circular dependency
struct Connection;

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

/**
 * @brief Queues data to be written to a client connection.
 * This function is the public interface for other modules to send data.
 */
void queue_data_for_writing(struct Connection* conn, const char* data, size_t len, int epollFd);


#endif // SERVER_H 