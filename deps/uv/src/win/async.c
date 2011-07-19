/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <assert.h>

#include "uv.h"
#include "internal.h"


/* Atomic set operation on char */
#ifdef _MSC_VER /* MSVC */

/* _InterlockedOr8 is supported by MSVC on x32 and x64. It is  slightly less */
/* efficient than InterlockedExchange, but InterlockedExchange8 does not */
/* exist, and interlocked operations on larger targets might require the */
/* target to be aligned. */
#pragma intrinsic(_InterlockedOr8)

static char __declspec(inline) uv_atomic_exchange_set(char volatile* target) {
  return _InterlockedOr8(target, 1);
}

#else /* GCC */

/* Mingw-32 version, hopefully this works for 64-bit gcc as well. */
static inline char uv_atomic_exchange_set(char volatile* target) {
  const char one = 1;
  char old_value;
  __asm__ __volatile__ ("lock xchgb %0, %1\n\t"
                        : "=r"(old_value), "=m"(*target)
                        : "0"(one), "m"(*target)
                        : "memory");
  return old_value;
}

#endif


void uv_async_endgame(uv_async_t* handle) {
  if (handle->flags & UV_HANDLE_CLOSING &&
      !handle->async_sent) {
    assert(!(handle->flags & UV_HANDLE_CLOSED));
    handle->flags |= UV_HANDLE_CLOSED;

    if (handle->close_cb) {
      handle->close_cb((uv_handle_t*)handle);
    }

    uv_unref();
  }
}


int uv_async_init(uv_async_t* handle, uv_async_cb async_cb) {
  uv_req_t* req;

  uv_counters()->handle_init++;
  uv_counters()->async_init++;

  handle->type = UV_ASYNC;
  handle->flags = 0;
  handle->async_sent = 0;
  handle->error = uv_ok_;
  handle->async_cb = async_cb;

  req = &handle->async_req;
  uv_req_init(req);
  req->type = UV_WAKEUP;
  req->data = handle;

  uv_ref();

  return 0;
}


int uv_async_send(uv_async_t* handle) {
  if (handle->type != UV_ASYNC) {
    /* Can't set errno because that's not thread-safe. */
    return -1;
  }

  /* The user should make sure never to call uv_async_send to a closing */
  /* or closed handle. */
  assert(!(handle->flags & UV_HANDLE_CLOSING));

  if (!uv_atomic_exchange_set(&handle->async_sent)) {
    if (!PostQueuedCompletionStatus(LOOP->iocp,
                                    0,
                                    0,
                                    &handle->async_req.overlapped)) {
      uv_fatal_error(GetLastError(), "PostQueuedCompletionStatus");
    }
  }

  return 0;
}


void uv_process_async_wakeup_req(uv_async_t* handle, uv_req_t* req) {
  assert(handle->type == UV_ASYNC);
  assert(req->type == UV_WAKEUP);

  handle->async_sent = 0;
  if (handle->async_cb) {
    handle->async_cb((uv_async_t*) handle, 0);
  }
  if (handle->flags & UV_HANDLE_CLOSING) {
    uv_want_endgame((uv_handle_t*)handle);
  }
}
