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
    
    char* argStr=argv[1];
    char str[1000];  
    char *ptr;  
    char *p;  

    int timeIR[1000];  //时间间隔

    memcpy(str,argStr,strlen(argStr));  

    ptr = strtok_r(str, ",", &p);  
    int tm=0;
    while(ptr != NULL){   
        timeIR[tm]= atoi(ptr);
        tm++;
        ptr = strtok_r(NULL, ",", &p);  
    }  

    int tt=0;
    PWMStop(pwm);
    usleep(20000);
    for(tt=0;tt<tm-1;tt++)
    {
        if (tt % 2) {
            PWMStop(pwm);
        } else {
            
            if (PWMPlay(pwm, Hz, duty) == -1) {
                printf("Fail to output PWM\n");
            }
        }
        usleep(timeIR[tt]);
    }    
    usleep(10000);
    PWMStop(pwm);
    //system("rmmod "DRIVER_MODULE);
    
    return 0;
}