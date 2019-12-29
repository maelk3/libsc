/*
  This file is part of the SC Library, version 3.
  The SC Library provides support for parallel scientific applications.

  Copyright (C) 2019 individual authors

  The SC Library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  The SC Library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with the SC Library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.
*/

/** \file sc3_base.h
 */

#ifndef SC3_BASE_H
#define SC3_BASE_H

#include <sc3_config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SC3_BUFSIZE 160
#define SC3_BUFZERO(b) do { memset (b, 0, SC3_BUFSIZE); } while (0)
#define SC3_BUFCOPY(b,s) \
          do { (void) snprintf (b, SC3_BUFSIZE, "%s", s); } while (0)

#define SC3_ISPOWOF2(a) ((a) > 0 && ((a) & ((a) - 1)) == 0)

#define SC3_STRDUP(src) (strdup (src))
#define SC3_MALLOC(typ,nmemb) ((typ *) malloc ((nmemb) * sizeof (typ)))
#define SC3_CALLOC(typ,nmemb) ((typ *) calloc (nmemb, sizeof (typ)))
#define SC3_FREE(ptr) do { free (ptr); } while (0)

#define SC3_MIN(out,in) \
  do { (out) = (in) < (out) ? (in) : (out); } while (0)
#define SC3_MAX(out,in) \
  do { (out) = (in) > (out) ? (in) : (out); } while (0)

#ifdef __cplusplus
extern              "C"
{
#if 0
}
#endif
#endif

char               *sc3_basename (char *path);

#ifdef __cplusplus
#if 0
{
#endif
}
#endif

#endif /* !SC3_BASE_H */