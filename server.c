#include <stdio.h>
#include <stdlib.h>
#include <uv.h>
#include <memory.h>

#define DEFAULT_PORT      (12345)
#define CONNECTIONS_COUNT (128)

/* TODO: проверять возвращаемые значения malloc */

const char * path;
static int counter;

struct server_ctx_ {
    uv_fs_t * open_req;
    uv_tcp_t * client;
    int64_t offset;
};
typedef struct server_ctx_ server_ctx_t;

struct write_buf_ {
    uv_buf_t buffer;
};
typedef struct write_buf_ write_buf_t;

write_buf_t * init_write_buf(char * buf, ssize_t nread)
{
    write_buf_t * write_buf = malloc(sizeof(write_buf_t));
    /* TODO: fix inline malloc */
    write_buf->buffer = uv_buf_init(malloc(nread), nread);
    memcpy(write_buf->buffer.base, buf, nread);

    return write_buf;
}

void release_write_buf(write_buf_t * write_buf)
{
    free(write_buf->buffer.base);
    free(write_buf);
}

server_ctx_t * init_server_ctx(uv_tcp_t *client)
{
    /* Инициализируем контектс задачи(сохранения файла) */
    server_ctx_t * ctx = malloc(sizeof(server_ctx_t));
    ctx->open_req = malloc(sizeof(uv_fs_t));
    ctx->client = client;
    ctx->client->data = ctx;
    ctx->open_req->data = ctx;
    ctx->offset = 0;

    return ctx;
}

void release_server_ctx(server_ctx_t *ctx)
{
    /* Освобождаем контекст задачи */
    /* Закрываем соединение синхронно */
    uv_close((uv_handle_t*) ctx->client, NULL);
    uv_fs_req_cleanup(ctx->open_req);
    free(ctx->client);
    free(ctx->open_req);
    free(ctx);
}

void alloc_buffer_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    buf->base = (char*) malloc(suggested_size);
    buf->len = suggested_size;
}

void file_write_cb(uv_fs_t *req)
{
    if (req->result < 0) {
        fprintf(stderr, "Write error: %s\n", uv_strerror((int)req->result));
    }

    write_buf_t * buf = req->data;
    release_write_buf(buf);

    uv_fs_req_cleanup(req);
    free(req);
}

void file_close_cb(uv_fs_t *req)
{
    server_ctx_t *ctx = req->data;
    uv_fs_req_cleanup(req);
    free(req);
    /* а теперь освобождаем контекст задачи */
    release_server_ctx(ctx);
}

void socket_read_cb(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf)
{
    server_ctx_t *ctx = client->data;
    if (nread < 0) {
        if (nread != UV_EOF)
            fprintf(stderr, "Read error %s\n", uv_err_name(nread));

        /* Надо закрыть файл */
        uv_fs_t *close_req = (uv_fs_t *) malloc(sizeof(uv_fs_t));
        close_req->data = ctx;
        uv_fs_close(uv_default_loop(), close_req, (uv_file)ctx->open_req->result, file_close_cb);
    } else if (nread > 0) {
        /* Полученные данные пишем в файл */
        uv_fs_t *req = (uv_fs_t *) malloc(sizeof(uv_fs_t));
        /* Что бы потом освободить память сохраняем указатель на буфер */
        write_buf_t * write_buf = init_write_buf(buf->base, nread);
        req->data = (void *)write_buf;

        uv_fs_write(uv_default_loop(), req, (uv_file)ctx->open_req->result, &write_buf->buffer, 1, ctx->offset, file_write_cb);
        /* Увеличиваем смещение в файле, что бы данные не накладывались */
        ctx->offset += nread;
    }

    if(buf->base)
        free(buf->base);
}

void file_open_cb(uv_fs_t * req)
{
    server_ctx_t * ctx = req->data;
    if (req->result >= 0) {
        /* Файл открыт, начинаем принимать данные */
        uv_read_start((uv_stream_t *) ctx->client, alloc_buffer_cb, socket_read_cb);
    } else {
        fprintf(stderr, "error opening file: %s\n", uv_strerror((int)req->result));
        /* Ошибка открытия файла, просто освобождаем контекст задачи */
        release_server_ctx(ctx);
    }
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
        /* Генерируем имя файла */
        char file_path[100];
        sprintf(file_path, "%s/%d.txt", path, counter++);
        /* Создаем контекст задачи */
        server_ctx_t * ctx = init_server_ctx(client);
        /* Открываем файл в который будем сохранять полученные данные */
        uv_fs_open(uv_default_loop(), ctx->open_req, file_path, O_CREAT | O_WRONLY | O_TRUNC, 0, file_open_cb);
    } else {
        uv_close((uv_handle_t*) client, NULL);
        free(client);
    }
}

int main(int argc, const char* argv[])
{
    uint16_t port;
    uv_tcp_t server;
    struct sockaddr_in addr;
    DIR *dir;

    fprintf(stdout, "libuv_example server\n");

    if(argc <= 2) {
        fprintf(stdout, "Usage: server port path_to_files\n");
        fprintf(stdout, "\tport - port to listen\n");
        fprintf(stdout, "\tpath_to_files - path to save accepted files\n");
        exit(EXIT_FAILURE);
    }

    port = atoi(argv[1]);

    dir = opendir(argv[2]);
    if (dir) {
        /* существует */
        closedir(dir);
    } else if (ENOENT == errno) {
        /* не существует, создаем */
        mkdir(argv[2], 0700);
    } else {
        perror("opendir:");
        exit(EXIT_FAILURE);
    }
    path = argv[2];

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