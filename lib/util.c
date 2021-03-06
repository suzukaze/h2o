/*
 * Copyright (c) 2014 DeNA Co., Ltd.
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
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "h2o.h"
#include "h2o/http1.h"
#include "h2o/http2.h"

void h2o_send_inline(h2o_req_t *req, const char *body, size_t len)
{
    static h2o_generator_t generator = { NULL, NULL };

    h2o_buf_t buf = h2o_strdup(&req->pool, body, len);
    /* the function intentionally does not set the content length, since it may be used for generating 304 response, etc. */
    /* req->res.content_length = buf.len; */

    h2o_start_response(req, &generator);
    h2o_send(req, &buf, 1, 1);
}

void h2o_send_error(h2o_req_t *req, int status, const char *reason, const char *body)
{
    req->http1_is_persistent = 0;

    req->res.status = status;
    req->res.reason = reason;
    memset(&req->res.headers, 0, sizeof(req->res.headers));
    h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, H2O_STRLIT("text/plain; charset=utf-8"));

    h2o_send_inline(req, body, SIZE_MAX);
}

static void on_ssl_handshake_complete(h2o_socket_t *sock, int status)
{
    const h2o_buf_t *ident;
    h2o_context_t *ctx = sock->data;
    sock->data = NULL;

    h2o_buf_t proto;
    if (status != 0) {
        h2o_socket_close(sock);
        return;
    }

    proto = h2o_socket_ssl_get_selected_protocol(sock);
    for (ident = h2o_http2_alpn_protocols; ident->len != 0; ++ident) {
        if (proto.len == ident->len && memcmp(proto.base, ident->base, proto.len) == 0) {
            goto Is_Http2;
        }
    }
    /* connect as http1 */
    h2o_http1_accept(ctx, sock);
    return;

Is_Http2:
    /* connect as http2 */
    h2o_http2_accept(ctx, sock);
}

void h2o_accept_ssl(h2o_context_t *ctx, h2o_socket_t *sock, SSL_CTX *ssl_ctx)
{
    sock->data = ctx;
    h2o_socket_ssl_server_handshake(sock, ssl_ctx, on_ssl_handshake_complete);
}

#ifdef H2O_UNITTEST

#include "t/test.h"

void test_lib__util_c(void)
{
    h2o_mempool_t pool;

    h2o_mempool_init(&pool);

    note("base64");
    {
        char buf[256];
        h2o_buf_t src = { H2O_STRLIT("The quick brown fox jumps over the lazy dog.") }, decoded;
        h2o_base64_encode(buf, (const uint8_t*)src.base, src.len, 1);
        ok(strcmp(buf, "VGhlIHF1aWNrIGJyb3duIGZveCBqdW1wcyBvdmVyIHRoZSBsYXp5IGRvZy4") == 0);
        decoded = h2o_decode_base64url(&pool, buf, strlen(buf));
        ok(src.len == decoded.len);
        ok(strcmp(decoded.base, src.base) == 0);
    }
    h2o_mempool_clear(&pool);

    note("h2o_normalize_path");
    {
        h2o_buf_t b = h2o_normalize_path(&pool, H2O_STRLIT("/"));
        ok(b.len == 1);
        ok(memcmp(b.base, H2O_STRLIT("/")) == 0);

        b = h2o_normalize_path(&pool, H2O_STRLIT("/abc"));
        ok(b.len == 4);
        ok(memcmp(b.base, H2O_STRLIT("/abc")) == 0);

        b = h2o_normalize_path(&pool, H2O_STRLIT("/abc"));
        ok(b.len == 4);
        ok(memcmp(b.base, H2O_STRLIT("/abc")) == 0);

        b = h2o_normalize_path(&pool, H2O_STRLIT("/abc/../def"));
        ok(b.len == 4);
        ok(memcmp(b.base, H2O_STRLIT("/def")) == 0);

        b = h2o_normalize_path(&pool, H2O_STRLIT("/abc/../../def"));
        ok(b.len == 4);
        ok(memcmp(b.base, H2O_STRLIT("/def")) == 0);

        b = h2o_normalize_path(&pool, H2O_STRLIT("/abc/./def"));
        ok(b.len == 8);
        ok(memcmp(b.base, H2O_STRLIT("/abc/def")) == 0);

        b = h2o_normalize_path(&pool, H2O_STRLIT("/abc/def/.."));
        ok(b.len == 5);
        ok(memcmp(b.base, H2O_STRLIT("/abc/")) == 0);

        b = h2o_normalize_path(&pool, H2O_STRLIT("/abc/def/."));
        ok(b.len == 9);
        ok(memcmp(b.base, H2O_STRLIT("/abc/def/")) == 0);

        b = h2o_normalize_path(&pool, H2O_STRLIT("/abc?xx"));
        ok(b.len == 4);
        ok(memcmp(b.base, H2O_STRLIT("/abc")) == 0);

        b = h2o_normalize_path(&pool, H2O_STRLIT("/abc/../def?xx"));
        ok(b.len == 4);
        ok(memcmp(b.base, H2O_STRLIT("/def")) == 0);

        b = h2o_normalize_path(&pool, H2O_STRLIT("/a%62c"));
        ok(b.len == 4);
        ok(memcmp(b.base, H2O_STRLIT("/abc")) == 0);

        b = h2o_normalize_path(&pool, H2O_STRLIT("/a%6"));
        ok(b.len == 4);
        ok(memcmp(b.base, H2O_STRLIT("/a%6")) == 0);

        b = h2o_normalize_path(&pool, H2O_STRLIT("/a%6?"));
        ok(b.len == 4);
        ok(memcmp(b.base, H2O_STRLIT("/a%6")) == 0);
    }
    h2o_mempool_clear(&pool);
}

#endif
