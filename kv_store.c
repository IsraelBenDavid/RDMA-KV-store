#include "constants.h"
#include "RDMA_api.h"

#include "kv_store.h"


/*  Key-Value get functions  */

int kv_get_eager(char **value, Packet * response_pack) {
    *value = malloc(strlen(response_pack->response_value) + 1);
    strncpy(*value, response_pack->response_value, strlen(response_pack->response_value) + 1);
    return 0;
}

int kv_get_rendezvous(PingpongContext *ctx, char **value) {
    Packet *pack = (Packet*)ctx->buf[ctx->currBuffer];
    *value = malloc(pack->rendezvous_get.size);

    if (send_RDMA_read(ctx, *value,
                       pack->rendezvous_get.size,
                       pack->rendezvous_get.remote_addr,
                       pack->rendezvous_get.rkey)) return 1;

    return 0;
}

int kv_get(void *kv_handle, const char *key, char **value) {
    PingpongContext *ctx = (PingpongContext*) kv_handle;
    Packet *pack = (Packet*)ctx->buf[ctx->currBuffer];
    pack->request_type = GET;
    strncpy(pack->key, key, sizeof(pack->key));
    if (send_packet(ctx)) return 1;
    if(receive_packet(ctx)) return 1;

    Packet *response_pack = (Packet*)ctx->buf[ctx->currBuffer];
    int response = 1;

    switch (response_pack->protocol_type){
            case EAGER:
                response = kv_get_eager(value, response_pack);
            break;
            case RENDEZVOUS:
                response = kv_get_rendezvous(ctx, value);
            break;
        }

    ctx->currBuffer = (ctx->currBuffer + 1) % MAXIMUM_HANDLE_REQUESTS_BUFFERS;
    return response;
}

/*  Key-Value set functions  */

int kv_set_eager(PingpongContext *ctx, const char *key, const char *value) {
    Packet *pack = (Packet*)ctx->buf[ctx->currBuffer];
    pack->protocol_type = EAGER;
    pack->request_type = SET;
    strncpy(pack->key, key, sizeof(pack->key));
    strncpy(pack->value, value, sizeof(pack->value));
    return send_packet(ctx);
}

int kv_set_rendezvous (PingpongContext *ctx, const char *key, const char *value) {
    // phase 1: send RENDEZVOUS SET request
    Packet *pack = (Packet*)ctx->buf[ctx->currBuffer];
    pack->protocol_type = RENDEZVOUS;
    pack->request_type = SET;
    size_t size_value = strlen(value) + 1;
    pack->rendezvous_set.size = size_value;
    strcpy(pack->key,key);
    if (send_packet(ctx)) return 1;

    // phase 2: receive RDMA address details
    if(receive_packet(ctx)) return 1;
    Packet *pack_response = (Packet*)ctx->buf[ctx->currBuffer];

    // phase 3: send RDMA write to server
    if (send_RDMA_write(ctx, value,
                        pack_response->rendezvous_set.remote_addr,
                        pack_response->rendezvous_set.rkey)) return 1;

    // phase 4: send fin
    if(send_fin(ctx)) return 1;
    return 0;
}


ProtocolType get_protocol_type(const char *key, const char *value){
    if (strlen(key) + strlen(value) < BITS_4_KB) return EAGER;
    return RENDEZVOUS;
}

int kv_set(void *kv_handle, const char *key, const char *value) {
    PingpongContext *ctx = (PingpongContext*) kv_handle;
    int response = 1;

    switch (get_protocol_type (key, value)){
        case EAGER:
            response = kv_set_eager(ctx, key, value);
            break;
        case RENDEZVOUS:
            response = kv_set_rendezvous(ctx, key, value);
            break;
    }

    ctx->currBuffer = (ctx->currBuffer + 1) % MAXIMUM_HANDLE_REQUESTS_BUFFERS;
    return response;
}

/* Other Key-Value functions */

// Connect to server
int kv_open(char *servername, void **kv_handle) {
    return RDMA_connect(servername,
                        (PingpongContext **)kv_handle);
}

// Called after get() on value pointer
void kv_release(char *value) {
    free(value);
}

// Destroys the QP
int kv_close(void *kv_handle) {
    return RDMA_disconnect(kv_handle);
}
