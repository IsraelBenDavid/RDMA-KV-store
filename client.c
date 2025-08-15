#include "constants.h"
#include "kv_store.h"

#include "client.h"

#define BIG_VALUE_SIZE  ((size_t)(1024 * 1024 * 1024)) // 1 GB
#define MAX_MSG_SIZE    ((size_t)(1024 * 1024 * 16)) // 16 MB
#define GB              ((size_t)(1024 * 1024 * 1024)) // 1 GB
#define ITERS           100
#define WARMUPS         50


/* Basic Tests */
void key_value_test(void *kv_handle, char* key, char* value){
    char *value_holder;
    kv_set(kv_handle, key, value);
    kv_get(kv_handle, key, &value_holder);
    assert(strcmp(value, value_holder) == 0);
    kv_release (value_holder);
}

void get_null_test(void *kv_handle, char* key){
    char *value_holder;
    kv_get(kv_handle, key, &value_holder);
    assert(strcmp("", value_holder) == 0);
    kv_release (value_holder);
}

void eager_basic_test(void *kv_handle){
    key_value_test(kv_handle, "eager_first_key", "First value");
    key_value_test(kv_handle, "eager_second_key", "Second value");
}

void rendezvous_basic_test(void *kv_handle){
    char* first_long_value = malloc(BITS_4_KB*16);
    for (size_t i = 0; i < BITS_4_KB*16; i++) first_long_value[i] = 'a';
    first_long_value[(BITS_4_KB*16)-1] = '\0';
    key_value_test(kv_handle, "rendezvous_first_key", first_long_value);
    free(first_long_value);

    char* second_long_value = malloc(BIG_VALUE_SIZE);
    for (size_t i = 0; i < BIG_VALUE_SIZE; i++) second_long_value[i] = 'a';
    second_long_value[BIG_VALUE_SIZE-1] = '\0';

    key_value_test(kv_handle, "rendezvous_second_key", second_long_value);
    free(second_long_value);

}

void look_for_values(void *kv_handle){
    printf("looking for values....\n");
    fflush(stdout);
    // set keys
    char* keys[4] = {"eager_first_key",
                     "eager_second_key",
                     "rendezvous_first_key",
                     "rendezvous_second_key"};
    // set values
    char* values[4];
    values[0] = "First value";
    values[1] = "Second value";

    values[2] = malloc(BITS_4_KB*16);
    for (size_t i = 0; i < BITS_4_KB*16; i++) values[2][i] = 'a';
    values[2][(BITS_4_KB*16)-1] = '\0';

    values[3] = malloc(BIG_VALUE_SIZE);
    for (size_t i = 0; i < BIG_VALUE_SIZE; i++) values[3][i] = 'a';
    values[3][BIG_VALUE_SIZE-1] = '\0';

    int found_sum = 0;
    int founds[] = {0, 0, 0, 0};
    while (found_sum != 4){
        for (int i = 0; i < 4; ++i){
            if (founds[i] == 0){
                char *value_holder;
                kv_get (kv_handle, keys[i], &value_holder);
                if (strcmp (values[i], value_holder) == 0){
                    printf ("found key: %s\n", keys[i]);
                    fflush (stdout);
                    founds[i] = 1;
                    found_sum++;
                }
                kv_release (value_holder);
            }
        }
    }
    printf(GREEN "Found All Values!!!\n" RESET);
    fflush(stdout);
}

void run_basic_tests(void *kv_handle) {
    eager_basic_test(kv_handle);
    rendezvous_basic_test (kv_handle);
    get_null_test (kv_handle, "not_key");
    printf(GREEN "Passed All Tests!\n" RESET);
    fflush(stdout);
}

/* Throughput Measure */

void print_throughput(clock_t start_time, clock_t end_time, size_t msg_size){
    long double diff_time = (long double)(end_time - start_time) / CLOCKS_PER_SEC;
    long double gb_unit = ITERS * (long double)msg_size / GB;
    long double throughput = gb_unit / diff_time;
    printf("%zu\t%Lf\t%s\n", msg_size, throughput, "GB/Sec");
    fflush(stdout);
}

void measure_throughput(void *kv_handle, RequestType request_type, size_t msg_size) {
    char key[20];
    snprintf(key, sizeof(key), "%zu", msg_size);

    char *value = malloc(msg_size);
    memset(value, 'a', msg_size - 1);
    value[msg_size - 1] = '\0';

    char *get_value = NULL;

    clock_t start_time;
    for (size_t i = 0; i < ITERS + WARMUPS; i++) {
        if (i == WARMUPS) start_time = clock();
        if (request_type == GET){
            kv_get(kv_handle, key, &get_value);
            if (strcmp(get_value, value)) {
                    printf(RED "GET request failed.\n" RESET);
                    exit(1);
                }
        }
        else kv_set(kv_handle, key, value);
    }
    clock_t end_time = clock();
    print_throughput (start_time, end_time, msg_size);
    free(value);
}

void print_protocol_header(size_t msg_size) {
    if (msg_size >= BITS_4_KB) {
        printf("Rendezvous: ");
    } else {
        printf("Eager: ");
    }
}

void test_throughput(void *kv_handle) {
    // SET requests throughput measure
    printf("--- Throughput Measure | SET Requests --- \n");
    for (size_t msg_size = 1; msg_size <= MAX_MSG_SIZE; msg_size *= 2) {
        print_protocol_header(msg_size);
        measure_throughput(kv_handle, SET, msg_size);
    }
    // GET requests throughput measure
    printf("--- Throughput Measure | GET Requests --- \n");
    for (size_t msg_size = 1; msg_size <= MAX_MSG_SIZE; msg_size *= 2) {
        print_protocol_header(msg_size);
        measure_throughput(kv_handle, GET, msg_size);
    }
}


int run_client(char *servername) {
    int choice;

    printf("Enter 1 to run tests or 2 to look for values "
           "3 to measure throughput: ");
    scanf("%d", &choice);

    if (choice != 1 && choice != 2 && choice != 3) {
        printf("Invalid choice. Exiting...\n");
        exit(1);
    }

    void *kv_handle;
    kv_open (servername, &kv_handle);
    switch (choice){
        case 1:
            run_basic_tests(kv_handle);
            break;
        case 2:
            look_for_values(kv_handle);
            break;
        case 3:
            test_throughput(kv_handle);
            break;
    }

    kv_close(kv_handle);
    return 0;
}
