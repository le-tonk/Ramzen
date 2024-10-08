/* 
 * Copyright (c) 2024, The PerformanC Organization
 * License available at LICENSE file (BSD 2-Clause)
*/

#include <stdlib.h> // size_t, NULL

#include "libtstr.h"

void tstr_find_between(struct tstr_string_token *result, const char *str, const char *start, int start_index, const char *end, int len) {
  int i = 0;
  int j = 0;

  result->start = 0;
  result->end = 0;

  if (start_index != 0) i = start_index;

  do {
    if (start == NULL) {
      result->start = i;

      goto loop;
    }

    if (str[i] == start[0]) {
      while (1) {
        if (str[i] != start[j]) {
          j = 0;
          result->start = 0;

          break;
        }

        if (start[j + 1] == '\0') {
          result->start = i + 1;
          j = 0;

          loop:

          while (str[i] != '\0') {
            if (end == NULL) {
              result->end = ++i;

              continue;
            }

            if (str[i] == end[j]) {
              if (result->end == 0) result->end = i;
              j++;

              if (end[j] == '\0') return;
            } else {
              result->end = 0;
              j = 0;
            }

            i++;
          }

          break;
        }

        i++;
        j++;
      }
    }

    i++;
  } while (len == 0 ? str[i - 1] != '\0' : i < len);

  if (result->end == 0 && str[i] == '\0') result->end = i;
}

void tstr_append(char *str, const char *append, size_t *length, int limiter) {
  int i = 0;

  while (limiter == 0 ? append[i] != '\0' : i < limiter) {
    str[*length] = append[i];

    (*length)++;
    i++;
  }
}

int tstr_find_amount(const char *str, const char *find) {
  int i = 0;
  int j = 0;
  int amount = 0;

  while (str[i] != '\0') {
    if (str[i] == find[j]) {
      j++;

      if (find[j] == '\0') {
        amount++;
        j = 0;
      }
    } else {
      j = 0;
    }

    i++;
  }

  return amount;
}

void tstr_free(struct tstr_string *str) {
  if (!str->allocated || str->string == NULL) return;

  free(str->string);
}