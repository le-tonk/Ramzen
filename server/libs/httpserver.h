/* 
 * Copyright (c) 2024, The PerformanC Organization
 * License available at LICENSE file (BSD 2-Clause)
*/

#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include "csocket-server.h"
#include "cthreads.h"

#include "httpparser.h"
#include "types.h"

struct httpserver {
  struct csocket_server server;
  struct httpserver_client *sockets;
  int *available_sockets;
  int sockets_capacity;
  int sockets_length;
  void (*handler)(int, char*);
};

struct httpserver_header {
  char *key;
  char *value;
};

struct httpserver_response {
  struct csocket_server_client *client;
  int status;
  int headers_length;
  int headers_max_length;
  struct httpserver_header *headers;
  char *body;
  int body_length;
};

struct _httpserver_connection_data {
  struct csocket_server_client client;
  int socket_index;
  struct httpserver *server;
  void (*callback)(struct csocket_server_client *client, int socket_index, struct httpparser_request *request);
  void (*disconnect_callback)(struct csocket_server_client *client, int socket_index);
};

struct httpserver_client {
  struct cthreads_thread thread;
  struct _httpserver_connection_data *thread_data;
  int socket;
  bool upgraded;
  void *data;
};

void httpserver_start_server(struct httpserver *server);

void httpserver_stop_server(struct httpserver *server);

void httpserver_disconnect_client(struct httpserver *server, struct csocket_server_client *client, int socket_index);

void httpserver_handle_request(struct httpserver *server, void (*callback)(struct csocket_server_client *client, int socket_index, struct httpparser_request *request), void (*disconnect_callback)(struct csocket_server_client *client, int socket_index));

void httpserver_set_socket_data(struct httpserver *server, int socket_index, void *data);

void *httpserver_get_socket_data(struct httpserver *server, int socket_index);

void httpserver_upgrade_socket(struct httpserver *server, int socket_index);

void httpserver_send_response(struct httpserver_response *response);

#endif /* HTTPSERVER_H_ */