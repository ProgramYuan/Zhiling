#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include "../libMatrix/includes/libfahw.h"

#define DRIVER_MODULE "matrix_pwm"
static int pwm;

void intHandler(int signNum)
{
    if (signNum == SIGINT) {
        printf("Clean up\n");
        PWMStop(pwm);
        //system("rmmod "DRIVER_MODULE);
    }
    exit(0);
}


int main(int argc, char ** argv)
{
    int Hz, duty, board;
    
    if ((board = boardInit()) < 0) {
        printf("Fail to init board\n");
        return -1;
    } 
    
    //system("modprobe "DRIVER_MODULE);
    signal(SIGINT, intHandler);
    
    // Usage:matrix-pwm channel freq duty[0~1000]
    pwm=0;
    Hz = 38000;
    duty = 600;
    
    ISend(argv[1],Hz,duty,pwm);
    //system("rmmod "DRIVER_MODULE);
    
    return 0;
}