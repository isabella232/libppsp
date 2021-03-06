/*
 * Copyright (c) 2020 Conclusive Engineering Sp. z o.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <pthread.h>
#include <signal.h>

extern int debug;

#define DEBUG 1
#define __FILENAME__ strrchr("/" __FILE__, '/') + 1

#if DEBUG
#define _assert(cond, format, ...)                                             \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("*** %s:%d %s() [%#lx] Assertion failed: " format, __FILE__,      \
             __LINE__, __func__, pthread_self(), __VA_ARGS__);                 \
      abort();                                                                 \
    }                                                                          \
  } while (0)

#define d_printf(format, ...)                                                  \
  do {                                                                         \
    if (debug > 0) {                                                           \
      printf("%s:%d %s():", __FILENAME__, __LINE__, __func__);                 \
      printf(format, __VA_ARGS__);                                             \
    }                                                                          \
  } while (0)

#else

#define _assert(cond, format, ...)                                             \
  do {                                                                         \
  } while (0)
#define d_printf(format, ...)                                                  \
  do {                                                                         \
  } while (0)

#endif
#endif /* _DEBUG_H_ */
