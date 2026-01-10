/* This file contains the main() loop for the client process.
 * The process is lauched by main.c with init_client
 */

#include <stdio.h>

int
main(
    int    argc,
    char** argv
) {
    // TODO: client logic
    printf("Client args:\n");
    for (int i=0; i<argc;i++)
        printf("%d: %s%s", i, argv[i], i==argc-1 ? ";":", ");
    printf("\n");
}
