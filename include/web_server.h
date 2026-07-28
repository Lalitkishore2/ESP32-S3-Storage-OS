#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>

/**
 * @brief Initialize WebServer, register API handlers, and start listening on HTTP_PORT.
 */
void web_server_init();

/**
 * @brief Main loop worker to service HTTP client requests.
 */
void web_server_handle();

#endif // WEB_SERVER_H
