#include "user.h"
#include "pstat.h"

int main(int argc, char **argv) {
    // takes exactly 5 arguments
    if(argc != 6){
        printf(2, "We need exactly 6 arguments.\n");
        exit();
    }

    struct pstat stat;
    // get the args
    int sliceA = atoi(argv[1]);
    char* sleepA = argv[2];
    int sliceB = atoi(argv[3]);
    char* sleepB = argv[4];
    int sleepParent = atoi(argv[5]);

    int pidA = -1;
    int pidB = -1;
    int comptickA = -1;
    int comptickB = -1;
    int pidA_c = -1;
    int pidB_c = -1;

    pidA = fork2(sliceA);
    if (pidA == 0){
        char *args[3];
        args[0] = "loop";
        args[1] = sleepA;
        args[2] = '\0';
        exec("loop", args);
    }
    pidA_c = pidA; // parent
    pidB = fork2(sliceB);
    if (pidB == 0){
        char *args[2];
        args[0] = "loop";
        args[1] = sleepB;
        args[2] = '\0';
        exec("loop", args);
    }
    pidB_c = pidB;
    sleep(sleepParent);

    // wait();
    // wait();
    if (getpinfo(&stat) == 0){
        for (int i = 0; i < NPROC; i++) {
            if (pidA_c == stat.pid[i]) {
                comptickA = stat.compticks[i];
            }
            if (pidB_c == stat.pid[i]) {
                comptickB = stat.compticks[i];
            }
        }
        printf(1, "%d %d\n", comptickA, comptickB); 
    }
    wait();
    wait();
    exit(); 
}
