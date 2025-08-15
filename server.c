#include "constants.h"
#include "RDMA_api.h"
#include "kv_store.h"

#include "server.h"

/* Helpers */

void extend_database_if_needed(Database *database){
    if (database->curr_size == database->capacity) {
            database->data_arr = realloc (database->data_arr,
                                     sizeof (Packet) * DATABASE_FACTOR_CAPACITY
                                     * database->capacity);
            database->capacity *= DATABASE_FACTOR_CAPACITY;
        }
}

int find_key_index(Database *database, char key[BITS_4_KB]){
    for (int i = 0; i < database->curr_size; i++)
        if(strcmp(database->data_arr[i].key, key) == 0) return i;
    return KEY_NOT_FOUND;
}

/* Get Requests Handle */

int get_request_not_found(PingpongContext *ctx){
    Packet *pack_response = (Packet*)ctx->buf[ctx->currBuffer];
    pack_response->protocol_type = EAGER;
    strncpy(pack_response->response_value,
            "",
            sizeof(pack_response->response_value));
    return send_packet (ctx);
}

int get_request_eager(PingpongContext *ctx, DataNode* db_data){
    Packet *pack_response = (Packet*)ctx->buf[ctx->currBuffer];
    pack_response->protocol_type = EAGER;
    strncpy(pack_response->response_value,
            db_data->value,
            sizeof(pack_response->value));
    return send_packet(ctx);
}

int get_request_rendezvous(PingpongContext *ctx, DataNode* db_data){
    Packet *pack_response = (Packet*)ctx->buf[ctx->currBuffer];
    size_t value_size = strlen(db_data->dynamic_value) + 1;

    pack_response->protocol_type = RENDEZVOUS;
    struct ibv_mr* mr_create = ibv_reg_mr(ctx->pd,
                                          db_data->dynamic_value,
                                          value_size,
                                          IBV_ACCESS_REMOTE_WRITE |
                                          IBV_ACCESS_LOCAL_WRITE |
                                          IBV_ACCESS_REMOTE_READ);
    pack_response->rendezvous_get.rkey = mr_create->rkey;
    pack_response->rendezvous_get.remote_addr = mr_create->addr;
    pack_response->rendezvous_get.size = value_size;
    return send_packet(ctx);
}

int handle_get_request(PingpongContext *ctx, Packet *pack, Database *database) {

    int key_idx = find_key_index (database, pack->key);
    if (key_idx == KEY_NOT_FOUND) return get_request_not_found(ctx);

    switch (database->data_arr[key_idx].protocol_type){
            case EAGER:
                return get_request_eager(ctx, &(database->data_arr[key_idx]));
            case RENDEZVOUS:
                return get_request_rendezvous(ctx, &(database->data_arr[key_idx]));
        }
    return 1;
}

/* Set Requests Handle */

int set_request_rendezvous(PingpongContext* ctx,
                                          Packet* pack, DataNode* db_data) {
    // overwrite memory
    if (db_data->dynamic_value != NULL) free(db_data->dynamic_value);

    // allocate memory for the client to write in
    db_data->dynamic_value = calloc(pack->rendezvous_set.size, 1);
    db_data->protocol_type = RENDEZVOUS;

    // set address and memory key to the client
    struct ibv_mr* mr_create = ibv_reg_mr(ctx->pd,
                                          db_data->dynamic_value,
                                          pack->rendezvous_set.size,
                                          IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);
    Packet * pack_response = (Packet*) ctx->buf[ctx->currBuffer];
    pack_response->rendezvous_set.rkey = mr_create->rkey;
    pack_response->rendezvous_set.remote_addr = mr_create->addr;

    // send
    if (send_packet(ctx)) return 1;

    // receive fin
    return receive_fin(ctx);
}

void set_request_eager(Packet * pack, DataNode* db_data){
    strncpy(db_data->value, pack->value, sizeof(pack->value));
}


int handle_set_request(PingpongContext *ctx, Packet *pack, Database *database) {
    extend_database_if_needed(database);
    int key_idx = find_key_index (database, pack->key);
    if (key_idx == KEY_NOT_FOUND){
        // Add the key to the database
        strncpy(database->data_arr[database->curr_size].key, pack->key, sizeof(pack->key));
        key_idx = database->curr_size;
        database->curr_size++;
    }
    switch (pack->protocol_type){
        case EAGER:
            set_request_eager(pack, &(database->data_arr[key_idx]));
            break;
        case RENDEZVOUS:
            set_request_rendezvous(ctx, pack, &(database->data_arr[key_idx]));
            break;
    }

    return 0;
}

/* Server Initialization */



int handle_request(PingpongContext *client_ctx, Database *database) {
    Packet * received_packet = (Packet *) client_ctx->buf[client_ctx->currBuffer];

    switch (received_packet->request_type) {
        case SET:
            if (received_packet->protocol_type == EAGER)
                printf("Received new Eager set request:\n\tKEY: %s || VALUE: %s\n",
                       received_packet->key, received_packet->value);
            else
                printf("Received new Rendezvous set request:\n\tKEY: %s\n",
                       received_packet->key);

            fflush( stdout);
            return handle_set_request(client_ctx, received_packet, database);

        case GET:
            printf("Received new get request:\n\tKEY: %s\n", received_packet->key);
            fflush( stdout);
            return handle_get_request(client_ctx, received_packet, database);
    }
    return 0;
}

int init_post_receive(PingpongContext *ctx_list[NUMBER_OF_CLIENTS]){
    for (int curr_client = 0; curr_client < NUMBER_OF_CLIENTS; curr_client++) {
        ctx_list[curr_client]->currBuffer = 0;
        if(receive_packet_async(ctx_list[curr_client])) return 1;
    }
    return 0;
}

void server_loop(PingpongContext *ctx_list[NUMBER_OF_CLIENTS], Database *database) {
    if (init_post_receive(ctx_list)) return;
    printf("Server is online.\n");
    fflush(stdout);
    while(1) {
        for (int curr_client = 0; curr_client < NUMBER_OF_CLIENTS; curr_client++) {
            struct ibv_wc wc[WC_BATCH];
            int ne = ibv_poll_cq(ctx_list[curr_client]->cq, WC_BATCH, wc);
            if (ne < 0) {
                fprintf(stderr, "poll CQ failed %d\n", ne);
                return;
            }
            if (ne >= 1) {
                printf("got request from client %d\n", curr_client);
                fflush(stdout);
                fflush(stdout);
                handle_request(ctx_list[curr_client], database);
                ctx_list[curr_client]->currBuffer++;
                ctx_list[curr_client]->currBuffer %= MAXIMUM_HANDLE_REQUESTS_BUFFERS;
                receive_packet_async(ctx_list[curr_client]);
            }
        }
    }
}

int run_server() {
    printf("Initializing server...\n");
    fflush(stdout);
    Database *database;
    void *kv_handle[NUMBER_OF_CLIENTS];
    database = malloc(sizeof (Database));
    database->data_arr = malloc(sizeof(Packet) * DATABASE_INITIAL_CAPACITY);
    database->capacity = DATABASE_INITIAL_CAPACITY;

    // Connect all clients to server
    for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        kv_open(NULL, &kv_handle[i]);
        printf ("client %d connected \n", i);
        fflush(stdout);
    }

    // Handle clients requests
    server_loop((PingpongContext **) kv_handle, database);
    free(database->data_arr);
    free(database);
    return 0;
}

