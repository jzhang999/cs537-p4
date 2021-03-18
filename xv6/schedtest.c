#include "user.h"
#include "pstat.h"

struct pstat* cur_stat; // process statistics

static void fork_child(int slice, char *sleepT) {
    int pid = fork2(slice);

    if (pid != -1) {
        if (pid == 0) {  // child
            // args to exec
            char *args[2];
            args[0] = "loop";
            args[1] = sleepT;
            exec(args[0], args);

            // exec failed
            printf(2, "exec failed\n");
            exit();
        } else {  // parent
            wait();
        }

    } else {  // fork failed
        printf(2, "fork failed\n");
    }
}

int main(int argc, char **argv) {
    // takes exactly 5 arguments
    if(argc != 6){
        printf(2, "We need exactly 6 arguments.\n");
        exit();
    }

    // get the args
    int sliceA = atoi(argv[1]);
    char* sleepA = argv[2];
    int sliceB = atoi(argv[3]);
    char* sleepB = argv[4];
    int sleepParent = atoi(argv[5]);

    // not sure where to put ORZ...................................
    sleep(sleepParent);
    
    // create 2 child processes
    fork_child(sliceA, sleepA);
    fork_child(sliceB, sleepB);

    // int compticksA;
    // int compticksB;
    cur_stat = (struct pstat*) malloc(sizeof(struct pstat*));
    int pidA = 0;
    int pidB = 0;
    if (getpinfo(cur_stat) == 0) {  // success
        // compticksA = cur_stat->compticks[1];
        // compticksB = cur_stat->compticks[2];
        pidA = cur_stat->pid[1];
        pidB = cur_stat->pid[2];
    }
    else {
        printf(2, "get info failed.\n");
    }

    printf(1, "%d %d\n", pidA, pidB);
    
    exit();
}