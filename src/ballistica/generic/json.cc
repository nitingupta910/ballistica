// Copyright (c) 2011-2020 Eric Froemling
// Derived from code licensed as follows:

/*
  Copyright (c) 2009 Dave Gamble

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

/* cJSON */
/* JSON parser in C. */

#include "ballistica/generic/json.h"

#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ballistica {

// Should tidy this up but don't want to risk breaking it at the moment.
#pragma clang diagnostic push
#pragma ide diagnostic ignored "hicpp-signed-bitwise"
#pragma ide diagnostic ignored "bugprone-narrowing-conversions"
#pragma ide diagnostic ignored "cppcoreguidelines-narrowing-conversions"

static const char* ep;

auto cJSON_GetErrorPtr() -> const char* { return ep; }

static auto cJSON_strcasecmp(const char* s1, const char* s2) -> int {
  if (!s1) return (s1 == s2) ? 0 : 1;
  if (!s2) return 1;
  for (; tolower(*s1) == tolower(*s2); ++s1, ++s2)
    if (*s1 == 0) return 0;
  return tolower(*(const unsigned char*)s1)
         - tolower(*(const unsigned char*)s2);
}

static void* (*cJSON_malloc)(size_t sz) = malloc;
static void (*cJSON_free)(void* ptr) = free;

static auto cJSON_strdup(const char* str) -> char* {
  size_t len;
  char* copy;

  len = strlen(str) + 1;
  if (!(copy = static_cast<char*>(cJSON_malloc(len)))) {
    return nullptr;
  }
  memcpy(copy, str, len);
  return copy;
}

void cJSON_InitHooks(cJSON_Hooks* hooks) {
  if (!hooks) { /* Reset hooks */
    cJSON_malloc = malloc;
    cJSON_free = free;
    return;
  }

  cJSON_malloc = (hooks->malloc_fn) ? hooks->malloc_fn : malloc;
  cJSON_free = (hooks->free_fn) ? hooks->free_fn : free;
}

/* Internal constructor. */
static auto cJSON_New_Item() -> cJSON* {
  auto* node = static_cast<cJSON*>(cJSON_malloc(sizeof(cJSON)));
  if (node) memset(node, 0, sizeof(cJSON));
  return node;
}

/* Delete a cJSON structure. */
void cJSON_Delete(cJSON* c) {
  cJSON* next;
  while (c) {
    next = c->next;
    if (!(c->type & cJSON_IsReference) && c->child) cJSON_Delete(c->child);
    if (!(c->type & cJSON_IsReference) && c->valuestring)
      cJSON_free(c->valuestring);
    if (c->string) cJSON_free(c->string);
    cJSON_free(c);
    c = next;
  }
}

/* Parse the input text to generate a number, and populate the result into item.
 */
static auto parse_number(cJSON* item, const char* num) -> const char* {
  double n = 0, sign = 1, scale = 0;
  int subscale = 0, signsubscale = 1;

  if (*num == '-') {
    sign = -1;
    num++;
  }                       /* Has sign? */
  if (*num == '0') num++; /* is zero */
  if (*num >= '1' && *num <= '9') do
      n = (n * 10.0f) + (*num++ - '0');
    while (*num >= '0' && *num <= '9'); /* Number? */
  if (*num == '.' && num[1] >= '0' && num[1] <= '9') {
    num++;
    do {
      n = (n * 10.0f) + (*num++ - '0');
      scale--;
    } while (*num >= '0' && *num <= '9');
  }                               /* Fractional part? */
  if (*num == 'e' || *num == 'E') /* Exponent? */
  {
    num++;
    if (*num == '+')
      num++;
    else if (*num == '-') {
      signsubscale = -1;
      num++; /* With sign? */
    }
    while (*num >= '0' && *num <= '9')
      subscale = (subscale * 10) + (*num++ - '0'); /* Number? */
  }

  n = sign * n
      * pow(10.0f,
            (scale + subscale * signsubscale)); /* number = +/- number.fraction
                                                 * 10^+/- exponent */

  item->valuedouble = n;
  item->valueint = (int)n;
  item->type = cJSON_Number;
  return num;
}

/* Render the number nicely from the given item into a string. */
static auto print_number(cJSON* item) -> char* {
  char* str;
  double d = item->valuedouble;
  if (fabs(((double)item->valueint) - d) <= DBL_EPSILON && d <= INT_MAX
      && d >= INT_MIN) {
    str = (char*)cJSON_malloc(21); /* 2^64+1 can be represented in 21 chars. */
    if (str) sprintf(str, "%d", item->valueint);
  } else {
    str = (char*)cJSON_malloc(64); /* This is a nice tradeoff. */
    if (str) {
      if (fabs(floor(d) - d) <= DBL_EPSILON && fabs(d) < 1.0e60)
        sprintf(str, "%.0f", d);
      else if (fabs(d) < 1.0e-6 || fabs(d) > 1.0e9)
        sprintf(str, "%e", d);
      else
        sprintf(str, "%f", d);
    }
  }
  return str;
}

static auto parse_hex4(const char* str) -> unsigned {
  unsigned h = 0;
  if (*str >= '0' && *str <= '9')
    h += (*str) - '0';
  else if (*str >= 'A' && *str <= 'F')
    h += 10 + (*str) - 'A';
  else if (*str >= 'a' && *str <= 'f')
    h += 10 + (*str) - 'a';
  else
    return 0;
  h = h << 4;
  str++;
  if (*str >= '0' && *str <= '9')
    h += (*str) - '0';
  else if (*str >= 'A' && *str <= 'F')
    h += 10 + (*str) - 'A';
  else if (*str >= 'a' && *str <= 'f')
    h += 10 + (*str) - 'a';
  else
    return 0;
  h = h << 4;
  str++;
  if (*str >= '0' && *str <= '9')
    h += (*str) - '0';
  else if (*str >= 'A' && *str <= 'F')
    h += 10 + (*str) - 'A';
  else if (*str >= 'a' && *str <= 'f')
    h += 10 + (*str) - 'a';
  else
    return 0;
  h = h << 4;
  str++;
  if (*str >= '0' && *str <= '9')
    h += (*str) - '0';
  else if (*str >= 'A' && *str <= 'F')
    h += 10 + (*str) - 'A';
  else if (*str >= 'a' && *str <= 'f')
    h += 10 + (*str) - 'a';
  else
    return 0;
  return h;
}

/* Parse the input text into an unescaped cstring, and populate item. */
static const unsigned char firstByteMark[7] = {0x00, 0x00, 0xC0, 0xE0,
                                               0xF0, 0xF8, 0xFC};
static auto parse_string(cJSON* item, const char* str) -> const char* {
  const char* ptr = str + 1;
  char* ptr2;
  char* out;
  size_t len = 0;
  unsigned uc, uc2;
  if (*str != '\"') {
    ep = str;
    return nullptr;
  } /* not a string! */

  while (*ptr != '\"' && *ptr && ++len) {
    if (*ptr++ == '\\') {
      ptr++; /* Skip escaped quotes. */
    }
  }

  // This is how long we need for the string, roughly.
  out = (char*)cJSON_malloc(len + 1);
  if (!out) return nullptr;

  ptr = str + 1;
  ptr2 = out;
  while (*ptr != '\"' && *ptr) {
    if (*ptr != '\\') {
      *ptr2++ = *ptr++;
    } else {
      ptr++;
      switch (*ptr) {
        case 'b':
          *ptr2++ = '\b';
          break;
        case 'f':
          *ptr2++ = '\f';
          break;
        case 'n':
          *ptr2++ = '\n';
          break;
        case 'r':
          *ptr2++ = '\r';
          break;
        case 't':
          *ptr2++ = '\t';
          break;
        case 'u': /* transcode utf16 to utf8. */
          uc = parse_hex4(ptr + 1);
          ptr += 4; /* get the unicode char. */

          if ((uc >= 0xDC00 && uc <= 0xDFFF) || uc == 0) {
            break;  // check for invalid.
          }

          // UTF16 surrogate pairs.
          if (uc >= 0xD800 && uc <= 0xDBFF) {
            if (ptr[1] != '\\' || ptr[2] != 'u')
              break; /* missing second-half of surrogate.	*/
            uc2 = parse_hex4(ptr + 3);
            ptr += 6;
            if (uc2 < 0xDC00 || uc2 > 0xDFFF)
              break; /* invalid second-half of surrogate.	*/
            uc = 0x10000 + (((uc & 0x3FF) << 10) | (uc2 & 0x3FF));
          }

          len = 4;
          if (uc < 0x80) {
            len = 1;
          } else if (uc < 0x800) {
            len = 2;
          } else if (uc < 0x10000) {
            len = 3;
          }
          ptr2 += len;

          switch (len) {
            case 4:  // NOLINT(bugprone-branch-clone)
              *--ptr2 = static_cast<char>((uc | 0x80) & 0xBF);
              uc >>= 6;
            case 3:
              *--ptr2 = static_cast<char>((uc | 0x80) & 0xBF);
              uc >>= 6;
            case 2:
              *--ptr2 = static_cast<char>((uc | 0x80) & 0xBF);
              uc >>= 6;
            case 1:
              *--ptr2 = static_cast<char>(uc | firstByteMark[len]);
            default:
              break;
          }
          ptr2 += len;
          break;
        default:
          *ptr2++ = *ptr;
          break;
      }
      ptr++;
    }
  }
  *ptr2 = 0;
  if (*ptr == '\"') {
    ptr++;
  }
  item->valuestring = out;
  item->type = cJSON_String;
  return ptr;
}

/* Render the cstring provided to an escaped version that can be printed. */
static auto print_string_ptr(const char* str) -> char* {
  const char* ptr;
  char *ptr2, *out;
  size_t len = 0;
  unsigned char token;

  if (!str) {
    return cJSON_strdup("");
  }
  ptr = str;
  while ((token = static_cast<unsigned char>(*ptr)) && ++len) {
    if (strchr("\"\\\b\f\n\r\t", token)) {
      len++;
    } else if (token < 32) {
      len += 5;
    }
    ptr++;
  }

  out = (char*)cJSON_malloc(len + 3);
  if (!out) {
    return nullptr;
  }

  ptr2 = out;
  ptr = str;
  *ptr2++ = '\"';
  while (*ptr) {
    if ((unsigned char)*ptr > 31 && *ptr != '\"' && *ptr != '\\') {
      *ptr2++ = *ptr++;
    } else {
      *ptr2++ = '\\';
      switch (token = static_cast<unsigned char>(*ptr++)) {
        case '\\':
          *ptr2++ = '\\';
          break;
        case '\"':
          *ptr2++ = '\"';
          break;
        case '\b':
          *ptr2++ = 'b';
          break;
        case '\f':
          *ptr2++ = 'f';
          break;
        case '\n':
          *ptr2++ = 'n';
          break;
        case '\r':
          *ptr2++ = 'r';
          break;
        case '\t':
          *ptr2++ = 't';
          break;
        default:
          sprintf(ptr2, "u%04x", token);
          ptr2 += 5;
          break; /* escape and print */
      }
    }
  }
  *ptr2++ = '\"';
  *ptr2 = 0;
  return out;
}
/* Invote print_string_ptr (which is useful) on an item. */
static auto print_string(cJSON* item) -> char* {
  return print_string_ptr(item->valuestring);
}

/* Predeclare these prototypes. */
static auto parse_value(cJSON* item, const char* value) -> const char*;
static auto print_value(cJSON* item, int depth, int fmt) -> char*;
static auto parse_array(cJSON* item, const char* value) -> const char*;
static auto print_array(cJSON* item, int depth, int fmt) -> char*;
static auto parse_object(cJSON* item, const char* value) -> const char*;
static auto print_object(cJSON* item, int depth, int fmt) -> char*;

/* Utility to jump whitespace and cr/lf */
static auto skip(const char* in) -> const char* {
  while (in && *in && (unsigned char)*in <= 32) in++;
  return in;
}

/* Parse an object - create a new root, and populate. */
auto cJSON_ParseWithOpts(const char* value, const char** return_parse_end,
                         int require_null_terminated) -> cJSON* {
  cJSON* c = cJSON_New_Item();
  ep = nullptr;
  if (!c) {
    return nullptr; /* memory fail */
  }

  const char* end = parse_value(c, skip(value));
  if (!end) {
    cJSON_Delete(c);
    return nullptr;
  } /* parse failure. ep is set. */

  /* if we require null-terminated JSON without appended garbage, skip and then
   * check for a null terminator */
  if (require_null_terminated) {
    end = skip(end);
    if (*end) {
      cJSON_Delete(c);
      ep = end;
      return nullptr;
    }
  }
  if (return_parse_end) *return_parse_end = end;
  return c;
}
/* Default options for cJSON_Parse */
auto cJSON_Parse(const char* value) -> cJSON* {
  return cJSON_ParseWithOpts(value, nullptr, 0);
}

/* Render a cJSON item/entity/structure to text. */
auto cJSON_Print(cJSON* item) -> char* { return print_value(item, 0, 1); }
auto cJSON_PrintUnformatted(cJSON* item) -> char* {
  return print_value(item, 0, 0);
}

/* Parser core - when encountering text, process appropriately. */
static auto parse_value(cJSON* item, const char* value) -> const char* {
  if (!value) {
    return nullptr; /* Fail on null. */
  }
  if (!strncmp(value, "null", 4)) {
    item->type = cJSON_NULL;
    return value + 4;
  }
  if (!strncmp(value, "false", 5)) {
    item->type = cJSON_False;
    return value + 5;
  }
  if (!strncmp(value, "true", 4)) {
    item->type = cJSON_True;
    item->valueint = 1;
    return value + 4;
  }
  if (*value == '\"') {
    return parse_string(item, value);
  }
  if (*value == '-' || (*value >= '0' && *value <= '9')) {
    return parse_number(item, value);
  }
  if (*value == '[') {
    return parse_array(item, value);
  }
  if (*value == '{') {
    return parse_object(item, value);
  }

  ep = value;
  return nullptr; /* failure. */
}

/* Render a value to text. */
static auto print_value(cJSON* item, int depth, int fmt) -> char* {
  char* out = nullptr;
  if (!item) {
    return nullptr;
  }
  switch ((item->type) & 255) {
    case cJSON_NULL:
      out = cJSON_strdup("null");
      break;
    case cJSON_False:
      out = cJSON_strdup("false");
      break;
    case cJSON_True:
      out = cJSON_strdup("true");
      break;
    case cJSON_Number:
      out = print_number(item);
      break;
    case cJSON_String:
      out = print_string(item);
      break;
    case cJSON_Array:
      out = print_array(item, depth, fmt);
      break;
    case cJSON_Object:
      out = print_object(item, depth, fmt);
      break;
    default:
      break;
  }
  return out;
}

/* Build an array from input text. */
static auto parse_array(cJSON* item, const char* value) -> const char* {
  cJSON* child;
  if (*value != '[') {
    ep = value;
    return nullptr;
  } /* not an array! */

  item->type = cJSON_Array;
  value = skip(value + 1);
  if (*value == ']') {
    return value + 1; /* empty array. */
  }

  item->child = child = cJSON_New_Item();
  if (!item->child) {
    return nullptr; /* memory fail */
  }
  value = skip(
      parse_value(child, skip(value))); /* skip any spacing, get the value. */
  if (!value) return nullptr;

  while (*value == ',') {
    cJSON* new_item;
    if (!(new_item = cJSON_New_Item())) {
      return nullptr; /* memory fail */
    }
    child->next = new_item;
    new_item->prev = child;
    child = new_item;
    value = skip(parse_value(child, skip(value + 1)));
    if (!value) {
      return nullptr; /* memory fail */
    }
  }

  if (*value == ']') {
    return value + 1; /* end of array */
  }
  ep = value;
  return nullptr; /* malformed. */
}

/* Render an array to text */
static auto print_array(cJSON* item, int depth, int fmt) -> char* {
  char** entries;
  char *out = nullptr, *ptr, *ret;
  size_t len = 5;
  cJSON* child = item->child;
  int numentries = 0, i = 0, fail = 0;

  /* How many entries in the array? */
  while (child) {
    numentries++;
    child = child->next;
  }
  /* Explicitly handle numentries==0 */
  if (!numentries) {
    out = (char*)cJSON_malloc(3);
    if (out) {
      strcpy(out, "[]");  // NOLINT
    }
    return out;
  }
  /* Allocate an array to hold the values for each */
  entries = (char**)cJSON_malloc(numentries * sizeof(char*));
  if (!entries) {
    return nullptr;
  }
  memset(entries, 0, numentries * sizeof(char*));
  /* Retrieve all the results: */
  child = item->child;
  while (child && !fail) {
    ret = print_value(child, depth + 1, fmt);
    entries[i++] = ret;
    if (ret)
      len += strlen(ret) + 2 + (fmt ? 1 : 0);
    else
      fail = 1;
    child = child->next;
  }

  /* If we didn't fail, try to malloc the output string */
  if (!fail) {
    out = (char*)cJSON_malloc(len);
  }
  /* If that fails, we fail. */
  if (!out) {
    fail = 1;
  }

  /* Handle failure. */
  if (fail) {
    for (i = 0; i < numentries; i++)
      if (entries[i]) cJSON_free(entries[i]);
    cJSON_free(entries);
    return nullptr;
  }

  /* Compose the output array. */
  *out = '[';
  ptr = out + 1;
  *ptr = 0;
  for (i = 0; i < numentries; i++) {
    strcpy(ptr, entries[i]);  // NOLINT
    ptr += strlen(entries[i]);
    if (i != numentries - 1) {
      *ptr++ = ',';
      if (fmt) *ptr++ = ' ';
      *ptr = 0;
    }
    cJSON_free(entries[i]);
  }
  cJSON_free(entries);
  *ptr++ = ']';
  *ptr = 0;
  return out;
}

/* Build an object from the text. */
static auto parse_object(cJSON* item, const char* value) -> const char* {
  cJSON* child;
  if (*value != '{') {
    ep = value;
    return nullptr;
  } /* not an object! */

  item->type = cJSON_Object;
  value = skip(value + 1);
  if (*value == '}') return value + 1; /* empty array. */

  item->child = child = cJSON_New_Item();
  if (!item->child) return nullptr;
  value = skip(parse_string(child, skip(value)));
  if (!value) return nullptr;
  child->string = child->valuestring;
  child->valuestring = nullptr;
  if (*value != ':') {
    ep = value;
    return nullptr;
  } /* fail! */
  value = skip(parse_value(
      child, skip(value + 1))); /* skip any spacing, get the value. */
  if (!value) return nullptr;

  while (*value == ',') {
    cJSON* new_item;
    if (!(new_item = cJSON_New_Item())) return nullptr; /* memory fail */
    child->next = new_item;
    new_item->prev = child;
    child = new_item;
    value = skip(parse_string(child, skip(value + 1)));
    if (!value) return nullptr;
    child->string = child->valuestring;
    child->valuestring = nullptr;
    if (*value != ':') {
      ep = value;
      return nullptr;
    } /* fail! */
    value = skip(parse_value(
        child, skip(value + 1))); /* skip any spacing, get the value. */
    if (!value) return nullptr;
  }

  if (*value == '}') return value + 1; /* end of array */
  ep = value;
  return nullptr; /* malformed. */
}

/* Render an object to text. */
static auto print_object(cJSON* item, int depth, int fmt) -> char* {
  char *out = nullptr, *ptr, *ret, *str;
  size_t len = 7;
  int i = 0;
  int j;
  cJSON* child = item->child;
  int numentries = 0, fail = 0;
  /* Count the number of entries. */
  while (child) {
    numentries++;
    child = child->next;
  }
  // Explicitly handle empty object case.
  if (!numentries) {
    out = (char*)cJSON_malloc(static_cast<size_t>(fmt ? depth + 4 : 3));
    if (!out) {
      return nullptr;
    }
    ptr = out;
    *ptr++ = '{';
    if (fmt) {
      *ptr++ = '\n';
      for (i = 0; i < depth - 1; i++) *ptr++ = '\t';
    }
    *ptr++ = '}';
    *ptr = 0;
    return out;
  }

  // Allocate space for the names and the objects.
  char** entries = (char**)cJSON_malloc(numentries * sizeof(char*));
  if (!entries) return nullptr;
  char** names = (char**)cJSON_malloc(numentries * sizeof(char*));
  if (!names) {
    cJSON_free(entries);
    return nullptr;
  }
  memset(entries, 0, sizeof(char*) * numentries);
  memset(names, 0, sizeof(char*) * numentries);

  // Collect all the results into our arrays.
  child = item->child;
  depth++;
  if (fmt) len += depth;
  while (child) {
    names[i] = str = print_string_ptr(child->string);
    entries[i++] = ret = print_value(child, depth, fmt);
    if (str && ret)
      len += strlen(ret) + strlen(str) + 2 + (fmt ? 2 + depth : 0);
    else
      fail = 1;
    child = child->next;
  }

  // Try to allocate the output string.
  if (!fail) out = (char*)cJSON_malloc(len);
  if (!out) fail = 1;

  // Handle failure.
  if (fail) {
    for (i = 0; i < numentries; i++) {
      if (names[i]) cJSON_free(names[i]);
      if (entries[i]) cJSON_free(entries[i]);
    }
    cJSON_free(names);
    cJSON_free(entries);
    return nullptr;
  }

  // Compose the output.
  *out = '{';
  ptr = out + 1;
  if (fmt) *ptr++ = '\n';
  *ptr = 0;
  for (i = 0; i < numentries; i++) {
    if (fmt)
      for (j = 0; j < depth; j++) *ptr++ = '\t';
    strcpy(ptr, names[i]);  // NOLINT
    ptr += strlen(names[i]);
    *ptr++ = ':';
    if (fmt) *ptr++ = '\t';
    strcpy(ptr, entries[i]);  // NOLINT
    ptr += strlen(entries[i]);
    if (i != numentries - 1) *ptr++ = ',';
    if (fmt) *ptr++ = '\n';
    *ptr = 0;
    cJSON_free(names[i]);
    cJSON_free(entries[i]);
  }

  cJSON_free(names);
  cJSON_free(entries);
  if (fmt)
    for (i = 0; i < depth - 1; i++) *ptr++ = '\t';
  *ptr++ = '}';
  *ptr = 0;
  return out;
}

// Get Array size/item / object item.
auto cJSON_GetArraySize(cJSON* array) -> int {
  cJSON* c = array->child;
  int i = 0;
  while (c) {
    i++;
    c = c->next;
  }
  return i;
}
auto cJSON_GetArrayItem(cJSON* array, int item) -> cJSON* {
  cJSON* c = array->child;
  while (c && item > 0) {
    item--;
    c = c->next;
  }
  return c;
}
auto cJSON_GetObjectItem(cJSON* object, const char* string) -> cJSON* {
  cJSON* c = object->child;
  while (c && cJSON_strcasecmp(c->string, string)) c = c->next;
  return c;
}

// Utility for array list handling.
static void suffix_object(cJSON* prev, cJSON* item) {
  prev->next = item;
  item->prev = prev;
}
// Utility for handling references.
static auto create_reference(cJSON* item) -> cJSON* {
  cJSON* ref = cJSON_New_Item();
  if (!ref) return nullptr;
  memcpy(ref, item, sizeof(cJSON));
  ref->string = nullptr;
  ref->type |= cJSON_IsReference;
  ref->next = ref->prev = nullptr;
  return ref;
}

// Add item to array/object.
void cJSON_AddItemToArray(cJSON* array, cJSON* item) {
  cJSON* c = array->child;
  if (!item) return;
  if (!c) {
    array->child = item;
  } else {
    while (c && c->next) c = c->next;
    suffix_object(c, item);
  }
}
void cJSON_AddItemToObject(cJSON* object, const char* string, cJSON* item) {
  if (!item) return;
  if (item->string) cJSON_free(item->string);
  item->string = cJSON_strdup(string);
  cJSON_AddItemToArray(object, item);
}
void cJSON_AddItemReferenceToArray(cJSON* array, cJSON* item) {
  cJSON_AddItemToArray(array, create_reference(item));
}
void cJSON_AddItemReferenceToObject(cJSON* object, const char* string,
                                    cJSON* item) {
  cJSON_AddItemToObject(object, string, create_reference(item));
}

auto cJSON_DetachItemFromArray(cJSON* array, int which) -> cJSON* {
  cJSON* c = array->child;
  while (c && which > 0) {
    c = c->next;
    which--;
  }
  if (!c) return nullptr;
  if (c->prev) c->prev->next = c->next;
  if (c->next) c->next->prev = c->prev;
  if (c == array->child) array->child = c->next;
  c->prev = c->next = nullptr;
  return c;
}
void cJSON_DeleteItemFromArray(cJSON* array, int which) {
  cJSON_Delete(cJSON_DetachItemFromArray(array, which));
}
auto cJSON_DetachItemFromObject(cJSON* object, const char* string) -> cJSON* {
  int i = 0;
  cJSON* c = object->child;
  while (c && cJSON_strcasecmp(c->string, string)) {
    i++;
    c = c->next;
  }
  if (c) return cJSON_DetachItemFromArray(object, i);
  return nullptr;
}
void cJSON_DeleteItemFromObject(cJSON* object, const char* string) {
  cJSON_Delete(cJSON_DetachItemFromObject(object, string));
}

// Replace array/object items with new ones.
void cJSON_ReplaceItemInArray(cJSON* array, int which, cJSON* newitem) {
  cJSON* c = array->child;
  while (c && which > 0) {
    c = c->next;
    which--;
  }
  if (!c) return;
  newitem->next = c->next;
  newitem->prev = c->prev;
  if (newitem->next) newitem->next->prev = newitem;
  if (c == array->child)
    array->child = newitem;
  else
    newitem->prev->next = newitem;
  c->next = c->prev = nullptr;
  cJSON_Delete(c);
}
void cJSON_ReplaceItemInObject(cJSON* object, const char* string,
                               cJSON* newitem) {
  int i = 0;
  cJSON* c = object->child;
  while (c && cJSON_strcasecmp(c->string, string)) {
    i++;
    c = c->next;
  }
  if (c) {
    newitem->string = cJSON_strdup(string);
    cJSON_ReplaceItemInArray(object, i, newitem);
  }
}

// Create basic types.
auto cJSON_CreateNull() -> cJSON* {
  cJSON* item = cJSON_New_Item();
  if (item) item->type = cJSON_NULL;
  return item;
}
auto cJSON_CreateTrue() -> cJSON* {
  cJSON* item = cJSON_New_Item();
  if (item) item->type = cJSON_True;
  return item;
}
auto cJSON_CreateFalse() -> cJSON* {
  cJSON* item = cJSON_New_Item();
  if (item) item->type = cJSON_False;
  return item;
}
auto cJSON_CreateBool(int b) -> cJSON* {
  cJSON* item = cJSON_New_Item();
  if (item) item->type = b ? cJSON_True : cJSON_False;
  return item;
}
auto cJSON_CreateNumber(double num) -> cJSON* {
  cJSON* item = cJSON_New_Item();
  if (item) {
    item->type = cJSON_Number;
    item->valuedouble = num;
    item->valueint = (int)num;
  }
  return item;
}
auto cJSON_CreateString(const char* string) -> cJSON* {
  cJSON* item = cJSON_New_Item();
  if (item) {
    item->type = cJSON_String;
    item->valuestring = cJSON_strdup(string);
  }
  return item;
}
auto cJSON_CreateArray() -> cJSON* {
  cJSON* item = cJSON_New_Item();
  if (item) item->type = cJSON_Array;
  return item;
}
auto cJSON_CreateObject() -> cJSON* {
  cJSON* item = cJSON_New_Item();
  if (item) item->type = cJSON_Object;
  return item;
}

// Create Arrays.
auto cJSON_CreateIntArray(const int* numbers, int count) -> cJSON* {
  int i;
  cJSON *n, *p = nullptr, *a = cJSON_CreateArray();
  for (i = 0; a && i < count; i++) {
    n = cJSON_CreateNumber(numbers[i]);
    if (!i)
      a->child = n;
    else
      suffix_object(p, n);
    p = n;
  }
  return a;
}
auto cJSON_CreateFloatArray(const float* numbers, int count) -> cJSON* {
  int i;
  cJSON *n, *p = nullptr, *a = cJSON_CreateArray();
  for (i = 0; a && i < count; i++) {
    n = cJSON_CreateNumber(numbers[i]);
    if (!i)
      a->child = n;
    else
      suffix_object(p, n);
    p = n;
  }
  return a;
}
auto cJSON_CreateDoubleArray(const double* numbers, int count) -> cJSON* {
  int i;
  cJSON *n, *p = nullptr, *a = cJSON_CreateArray();
  for (i = 0; a && i < count; i++) {
    n = cJSON_CreateNumber(numbers[i]);
    if (!i)
      a->child = n;
    else
      suffix_object(p, n);
    p = n;
  }
  return a;
}
auto cJSON_CreateStringArray(const char** strings, int count) -> cJSON* {
  int i;
  cJSON *n, *p = nullptr, *a = cJSON_CreateArray();
  for (i = 0; a && i < count; i++) {
    n = cJSON_CreateString(strings[i]);
    if (!i)
      a->child = n;
    else
      suffix_object(p, n);
    p = n;
  }
  return a;
}

// Duplication.
auto cJSON_Duplicate(cJSON* item, int recurse) -> cJSON* {
  cJSON *newitem, *cptr, *nptr = nullptr, *newchild;
  /* Bail on bad ptr */
  if (!item) return nullptr;
  /* Create new item */
  newitem = cJSON_New_Item();
  if (!newitem) return nullptr;
  /* Copy over all vars */
  newitem->type = item->type & (~cJSON_IsReference);
  newitem->valueint = item->valueint;
  newitem->valuedouble = item->valuedouble;
  if (item->valuestring) {
    newitem->valuestring = cJSON_strdup(item->valuestring);
    if (!newitem->valuestring) {
      cJSON_Delete(newitem);
      return nullptr;
    }
  }
  if (item->string) {
    newitem->string = cJSON_strdup(item->string);
    if (!newitem->string) {
      cJSON_Delete(newitem);
      return nullptr;
    }
  }
  /* If non-recursive, then we're done! */
  if (!recurse) return newitem;
  /* Walk the ->next chain for the child. */
  cptr = item->child;
  while (cptr) {
    newchild = cJSON_Duplicate(
        cptr, 1); /* Duplicate (with recurse) each item in the ->next chain */
    if (!newchild) {
      cJSON_Delete(newitem);
      return nullptr;
    }
    if (nptr) {
      nptr->next = newchild;
      newchild->prev = nptr;
      nptr = newchild;
    } /* If newitem->child already set, then crosswire ->prev and ->next and
         move on */
    else {
      newitem->child = newchild;
      nptr = newchild;
    } /* Set newitem->child and move to it */
    cptr = cptr->next;
  }
  return newitem;
}

void cJSON_Minify(char* json) {
  char* into = json;
  while (*json) {
    if (*json == ' ')
      json++;  // NOLINT(bugprone-branch-clone)
    else if (*json == '\t')
      json++;  // Whitespace characters.
    else if (*json == '\r')
      json++;
    else if (*json == '\n')
      json++;
    else if (*json == '/' && json[1] == '/')
      while (*json && *json != '\n')
        json++;  // double-slash comments, to end of line.
    else if (*json == '/' && json[1] == '*') {
      while (*json && !(*json == '*' && json[1] == '/')) json++;
      json += 2;
    }  // multiline comments.
    else if (*json == '\"') {
      *into++ = *json++;
      while (*json && *json != '\"') {
        if (*json == '\\') *into++ = *json++;
        *into++ = *json++;
      }
      *into++ = *json++;
    }  // string literals, which are \" sensitive.
    else {
      *into++ = *json++;  // All other characters.
    }
  }
  *into = 0;  // and null-terminate.
}

#pragma clang diagnostic pop

}  // namespace ballistica