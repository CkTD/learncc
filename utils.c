#include <execinfo.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void error(char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  fprintf(stderr, "error: ");
  vfprintf(stderr, msg, ap);
  fprintf(stderr, "\n");
  *((int*)0) = 1;
  exit(1);
}

void warn(char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  fprintf(stderr, "warning: ");
  vfprintf(stderr, msg, ap);
  fprintf(stderr, "\n");
}

/*****************************
 * string management         *
 *****************************/
static unsigned long hash(unsigned char* str, unsigned n) {
  unsigned long hash = 5381;

  for (int i = 0; i < n; i++)
    hash = ((hash << 5) + hash) + str[i]; /* hash * 33 + c */

  return hash;
}

#define STSIZE 128
static struct string {
  char* s;
  struct string* next;
} * string_table[STSIZE];

char* stringn(char* s, int n) {
  unsigned h = hash((unsigned char*)s, n) % STSIZE;
  for (struct string* i = string_table[h]; i; i = i->next) {
    if (strlen(i->s) == n && strncmp(i->s, s, n) == 0)
      return i->s;
  }

  struct string* new = malloc(sizeof(struct string));
  new->s = calloc(n + 1, sizeof(char));
  strncpy(new->s, s, n);
  new->next = string_table[h];
  string_table[h] = new;

  return new->s;
}

const char* string(char* s) {
  return stringn(s, strlen(s));
}