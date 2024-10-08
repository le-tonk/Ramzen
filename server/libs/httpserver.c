/* 
 * Copyright (c) 2024, The PerformanC Organization
 * License available at LICENSE file (BSD 2-Clause)
*/

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#include "httpparser.h"
#include "cthreads.h"
#include "csocket-server.h"
#include "tcplimits.h"
#include "libtstr.h"

#include "types.h"

#include "httpserver.h"

#ifndef PORT
#define PORT 8888
#endif

void httpserver_start_server(struct httpserver *server) {
  server->server = (struct csocket_server) {
    .port = PORT
  };

  if (csocket_server_init(&server->server) == 1) {
    perror("[httpserver]: Failed to initialize server");

    exit(1);
  }

  server->sockets = malloc(sizeof(struct httpserver_client));
  server->sockets[0].thread = (struct cthreads_thread) { 0 };
  server->sockets[0].thread_data = NULL;
  server->sockets[0].socket = -1;
  server->sockets[0].upgraded = false;
  server->sockets[0].data = NULL;

  server->available_sockets = malloc(sizeof(int));
  server->available_sockets[0] = 1;
  server->sockets_capacity = 1;
  server->sockets_length = 0;
}

void httpserver_stop_server(struct httpserver *server) {
  for (int i = 0; i < server->sockets_capacity; i++) {
    if (server->sockets[i].socket == -1) continue;

    cthreads_thread_cancel(server->sockets[i].thread);
    free(server->sockets[i].thread_data);
  }

  csocket_server_close(&server->server);
  free(server->sockets);
  free(server->available_sockets);
}

static void _httpserver_add_available_socket(struct httpserver *server, int socket_index) {
  for (int i = 0; i < server->sockets_capacity; i++) {
    if (server->available_sockets[i] != 0) continue;

    server->available_sockets[i] = socket_index + 1;
    server->sockets_length--;

    break;
  }
}

void httpserver_disconnect_client(struct httpserver *server, struct csocket_server_client *client, int socket_index) {
  csocket_close_client(client);

  cthreads_thread_cancel(server->sockets[socket_index].thread);
  free(server->sockets[socket_index].thread_data);
  server->sockets[socket_index].socket = -1;
  server->sockets[socket_index].upgraded = false;

  _httpserver_add_available_socket(server, socket_index);
}

static int _httpserver_select_position(struct httpserver *server) {
  for (int i = 0; i < server->sockets_capacity; i++) {
    if (server->available_sockets[i] == 0) continue;

    int socket_index = server->available_sockets[i] - 1;
    server->available_sockets[i] = 0;

    return socket_index;
  }

  server->sockets_capacity *= 2;
  server->sockets = realloc(server->sockets, (size_t)server->sockets_capacity * sizeof(struct httpserver_client));
  server->available_sockets = realloc(server->available_sockets, (size_t)server->sockets_capacity * sizeof(int));

  for (int i = server->sockets_capacity / 2; i < server->sockets_capacity; i++) {
    server->sockets[i].thread = (struct cthreads_thread) { 0 };
    server->sockets[i].thread_data = NULL;
    server->sockets[i].socket = -1;
    server->sockets[i].upgraded = false;
    server->sockets[i].data = NULL;

    server->available_sockets[i] = i + 1;
  }

  server->available_sockets[server->sockets_length] = 0;

  return server->sockets_length;
}

static void *listen_messages(void *args) {
  struct _httpserver_connection_data *connection_data = (struct _httpserver_connection_data *)args;

  struct csocket_server_client client = connection_data->client;
  int socket_index = connection_data->socket_index;
  struct httpserver *server = connection_data->server;

  char payload[TCPLIMITS_PACKET_SIZE];
  int payload_size = 0;

  while ((payload_size = csocket_server_recv(&client, payload, TCPLIMITS_PACKET_SIZE)) > 0) {
    struct httpparser_request request;
    struct httpparser_header headers[30];
    httpparser_init_request(&request, headers, 30);

    if (httpparser_parse_request(&request, payload, payload_size) != 0) {
      httpparser_free_request(&request);

      goto invalid_request;
    }

    connection_data->callback(&client, socket_index, &request);

    httpparser_free_request(&request);
  }

  if (payload_size == -1)
    perror("[httpserver]: recv failed");

  disconnect: {
    connection_data->disconnect_callback(&client, socket_index);

    httpserver_disconnect_client(server, &client, socket_index);

    return NULL;
  }

  invalid_request: {
    struct httpserver_response response = {
      .client = &client,
      .status = 400,
      .headers = (struct httpserver_header *) &(struct httpserver_header []) {
        {
          .key = "Content-Length",
          .value = "0"
        }
      },
      .headers_length = 1
    };

    httpserver_send_response(&response);

    goto disconnect;
  }
}

void httpserver_handle_request(struct httpserver *server, void (*callback)(struct csocket_server_client *client, int socket_index, struct httpparser_request *request), void (*disconnect_callback)(struct csocket_server_client *client, int socket_index)) {
  int socket = 0;

  struct csocket_server_client client = { 0 };
  while ((socket = csocket_server_accept(server->server, &client)) == 0) {
    int socket_index = _httpserver_select_position(server);
    server->sockets_length++;

    struct httpserver_client http_client = {
      .thread = { 0 },
      .thread_data = malloc(sizeof(struct _httpserver_connection_data)),
      .socket = socket,
      .upgraded = false,
      .data = NULL
    };

    http_client.thread_data->server = server;
    http_client.thread_data->client = client;
    http_client.thread_data->socket_index = socket_index;
    http_client.thread_data->callback = callback;
    http_client.thread_data->disconnect_callback = disconnect_callback;

    struct cthreads_args cargs;
    cthreads_thread_create(&http_client.thread, NULL, listen_messages, http_client.thread_data, &cargs);
    cthreads_thread_detach(http_client.thread);

    server->sockets[socket_index] = http_client;
  }
}

void httpserver_set_socket_data(struct httpserver *server, int socket_index, void *data) {
  server->sockets[socket_index].data = data;
}

void *httpserver_get_socket_data(struct httpserver *server, int socket_index) {
  return server->sockets[socket_index].data;
}

void httpserver_upgrade_socket(struct httpserver *server, int socket_index) {
  server->sockets[socket_index].upgraded = true;
}

static char *_httpserver_get_status_text(int status) {
  switch (status) {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 102: return "Processing";
    case 103: return "Early Hints";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoratative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 207: return "Multi-Status";
    case 208: return "Already Reported";
    case 226: return "IM Used";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad request";
    case 401: return "Unathorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Payload Too Large";
    case 414: return "Request-URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Requested Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 418: return "I'm a teapot";
    case 421: return "Misdirected Request";
    case 422: return "Unprocessable Entity";
    case 423: return "Locked";
    case 424: return "Failed Dependency";
    case 425: return "Too Early";
    case 426: return "Upgrade Required";
    case 428: return "Precondition Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 450: return "Blocked by Windows Parental Controls";
    case 451: return "Unavailable For Legal Reasons";
    case 497: return "HTTP Request Sent to HTTPS Port";
    case 498: return "Invalid Token";
    case 499: return "Client Closed Request";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 506: return "Variant Also Negotiates";
    case 507: return "Insufficient Storage";
    case 508: return "Loop Detected";
    case 509: return "Bandwidth Limit Exceeded";
    case 510: return "Not Extended";
    case 511: return "Network Authentication Required";
    case 521: return "Web Server Is Down";
    case 522: return "Connection Timed Out";
    case 523: return "Origin Is Unreachable";
    case 524: return "A Timeout Occurred";
    case 525: return "SSL Handshake Failed";
    case 530: return "Site Frozen";
    case 599: return "Network Connect Timeout Error";
  }

  return "Unknown";
}

static int _calculate_response_length(struct httpserver_response *response) {
  int length = 0;

  length += sizeof("HTTP/1.1 ");
  length += 3;
  length += (int)strlen(_httpserver_get_status_text(response->status));
  length += 2;

  for (int i = 0; i < response->headers_length; i++) {
    length += (int)strlen(response->headers[i].key);
    length += (int)strlen(response->headers[i].value);
    length += 4;
  }

  length += 2;

  if (response->body != NULL) {
    length += (int)response->body_length;
  }

  return length;
}

void httpserver_send_response(struct httpserver_response *response) {
  size_t length = (size_t)_calculate_response_length(response);
  char *response_string = malloc(length);
  size_t response_position = 0;

  tstr_append(response_string, "HTTP/1.1 ", &response_position, 0);
  
  char status_text[3 + 1];
  snprintf(status_text, sizeof(status_text), "%d", response->status);
  tstr_append(response_string, status_text, &response_position, 0);
  tstr_append(response_string, " ", &response_position, 0);
  tstr_append(response_string, _httpserver_get_status_text(response->status), &response_position, 0);
  tstr_append(response_string, "\r\n", &response_position, 0);

  for (int i = 0; i < response->headers_length; i++) {
    tstr_append(response_string, response->headers[i].key, &response_position, 0);
    tstr_append(response_string, ": ", &response_position, 0);
    tstr_append(response_string, response->headers[i].value, &response_position, 0);
    tstr_append(response_string, "\r\n", &response_position, 0);
  }

  tstr_append(response_string, "\r\n", &response_position, 0);

  if (response->body != NULL) {
    tstr_append(response_string, response->body, &response_position, (int)response->body_length);
  }

  csocket_server_send(response->client, response_string, length);

  free(response_string);
}