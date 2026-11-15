#include "linux_io.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    struct schcvx_config config;
    char error[256];
    if (argc != 3 || strcmp(argv[1], "-c") != 0) {
        fprintf(stderr, "ERROR: usage: schcvxd -c <config file>\n");
        return 1;
    }
    if (schcvx_config_load(argv[2], &config, error, sizeof error) != 0) {
        fprintf(stderr, "ERROR: configuration: %s\n", error);
        return 1;
    }
    return schcvx_linux_run(&config);
}
