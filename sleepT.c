#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char **argv) {
    // takes exactly 1 argument
    if(argc != 2){
        printf(2, "We need exactly 1 arguments.\n");
        exit();
    }

    int sleepT = atoi(argv[1]);
    sleep(sleepT);

    int i = 0, j = 0;
    while (i < 800000000) {
        j += i * j + 1;
        i++;
    }
    
    exit();
}
