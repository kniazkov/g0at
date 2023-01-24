#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: functional_testing <list of tests>\n");
        return -1;
    }
    FILE *list = fopen(argv[1], "r");
    if (!list) {
        printf("Could not open '%s'\n", argv[1]);
        return -1;
    }

    

    fclose(list);
    return 0;
}
