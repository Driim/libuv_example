#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

#define DEFAULT_PORT      (12345)
#define CONNECTIONS_COUNT (128)

void alloc_buffer_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    buf->base = (char*) malloc(suggested_size);
    buf->len = suggested_size;
}

void socket_write_cb(uv_write_t *req, int status) {
    if (status) {
        fprintf(stderr, "Write error %s\n", uv_strerror(status));
    }

    free(req);
}

void socket_read_cb(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
    if (nread < 0) {
        if (nread != UV_EOF)
            fprintf(stderr, "Read error %s\n", uv_err_name(nread));

        uv_close((uv_handle_t*) client, NULL);
    } else if (nread > 0) {
        uv_write_t *req = (uv_write_t *) malloc(sizeof(uv_write_t));
        uv_buf_t write_buf = uv_buf_init(buf->base, nread);

        uv_write(req, client, &write_buf, 1, socket_write_cb);
    }

    if (buf->base)
        free(buf->base);
}

void accept_connection_cb(uv_stream_t *server, int status)
{
    if (status < 0) {
        fprintf(stderr, "New connection error %s\n", uv_strerror(status));
        return;
    }

    uv_tcp_t *client = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), client);

    if (uv_accept(server, (uv_stream_t*) client) == 0) {
        uv_read_start((uv_stream_t *) client, alloc_buffer_cb, socket_read_cb);
    }
    else {
        uv_close((uv_handle_t*) client, NULL);
    }
}

int main(int argc, const char* argv[])
{
    uint16_t port;
    uv_tcp_t server;
    struct sockaddr_in addr;

    fprintf(stdout, "libuv_example echo-server\n");

    if(argc <= 1) {
        port = DEFAULT_PORT;
    } else {
        port = atoi(argv[1]);
    }

    uv_tcp_init(uv_default_loop(), &server);

    uv_ip4_addr("0.0.0.0", port, &addr);
    uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);

    int ret = uv_listen((uv_stream_t*) &server, CONNECTIONS_COUNT, accept_connection_cb);
    if(ret) {
        fprintf(stderr, "Listen error %s\n", uv_strerror(ret));
        return 1;
    }

    return uv_run(uv_default_loop(), UV_RUN_DEFAULT);
}