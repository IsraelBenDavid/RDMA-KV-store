#include "server.h"
#include "client.h"
#include "constants.h"
int main(int argc, char **argv)
{
    char *servername;
    if (argc == 2) servername = strdup(argv[1]);
    return servername ? run_client(servername) : run_server();
}