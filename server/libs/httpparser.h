/* 
 * Copyright (c) 2024, The PerformanC Organization
 * License available at LICENSE file (BSD 2-Clause)
*/

#ifndef HTTPPARSER_H
#define HTTPPARSER_H

#include <stdlib.h>

#include "types.h"

struct httpparser_header {
  char key[64];
  char value[1024];
};

struct httpparser_request {
  char method[7];
  char *path;
  char version[4];
  int headers_length;
  int headers_max_length;
  struct httpparser_header *headers;
  char *body;
  size_t body_length;
  int chunk_length;
  /* TODO: Implement chunk handling */
  bool finished;
};

void httpparser_init_request(struct httpparser_request *httpRequest, struct httpparser_header *buffer, int length);

int httpparser_parse_request(struct httpparser_request *httpRequest, const char *request, int request_length);

void httpparser_free_request(struct httpparser_request *httpRequest);

struct httpparser_header *httpparser_get_header(struct httpparser_request *httpRequest, const char *key);

#endif