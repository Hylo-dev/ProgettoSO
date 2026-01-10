#include <stdio.h>

int
main(
    int    argc,
    char** argv
) {
    
    // TODO: here the worker logic
    printf("Worker args:\n");
    for (int i=0; i<argc;i++)
        printf("%d: %s%s", i, argv[i], i==argc-1 ? ";":", ");
    printf("\n");
}
