#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static void print_usage(const char *program)
{
    printf("Usage: %s [--host ADDRESS] [--port PORT]\n", program);
    printf("Defaults: --host 127.0.0.1 --port 5000\n");
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    int port = 5000;
    int i;

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            port = atoi(argv[i]);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port: %d\n", port);
        return 1;
    }

    return server_run(host, port);
}
