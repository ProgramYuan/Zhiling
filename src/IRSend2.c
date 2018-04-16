#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
//#include "../libMatrix/includes/libfahw.h"

#define DRIVER_MODULE "matrix_pwm"
#define PWM_IOCTL_STOP          (0x0)
#define PWM_IOCTL_SET_FREQ      (0x1)
static int pwm;

static int pwmGPIO[3];
static int pwmNum = -1;
int devFD = -1;
int gpio;


int openHW(const char *devName,int flags) {
    int fd = -1;
    const char *strDevName = devName;
    fd = open(strDevName, flags);
    if (fd < 0) {
        return -1;
    }
    return fd;
}

void intHandler(int signNum)
{
    if (signNum == SIGINT) {
        printf("Clean up\n");
        
        if (ioctl(devFD, PWM_IOCTL_STOP, &gpio) == -1) {
            close(devFD);
            return -1;
        }
        //system("rmmod "DRIVER_MODULE);
    }
    exit(0);
}


int main(int argc, char ** argv)
{
    int Hz, duty;
    
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
    //PWMStop(pwm);
    //初始化
    memset(pwmGPIO, -1, sizeof(pwmGPIO));
    int buf[2] = {5, 6};
    memcpy(pwmGPIO, buf, sizeof(buf));
    pwmNum = 2;

    if (pwm<0 || pwm>=pwmNum || pwmGPIO[pwm]==-1) {
        return -1;
    }
    gpio = pwmGPIO[pwm];
    
    if ((devFD = openHW("/dev/pwm", O_RDONLY)) == -1) {
        return -1;
    }

    if (ioctl(devFD, PWM_IOCTL_STOP, &gpio) == -1) {
        close(devFD);
        return -1;
    }
    close(devFD);


    usleep(20000);
    for(tt=0;tt<tm-1;tt++)
    {
        if (tt % 2) {
            gpio = pwmGPIO[pwm];
            if ((devFD = openHW("/dev/pwm", O_RDONLY)) == -1) {
                return -1;
            }
            if (ioctl(devFD, PWM_IOCTL_STOP, &gpio) == -1) {
                close(devFD);
                return -1;
            }
            close(devFD);
        } else {

            int arg[3];
            gpio = pwmGPIO[pwm];
            arg[0] = gpio;
            arg[1] = Hz;
            arg[2] = duty;

            if ((devFD = openHW("/dev/pwm", O_RDONLY)) == -1) {
                return -1;
            }
            
            if (ioctl(devFD, PWM_IOCTL_SET_FREQ, arg) == -1) {
                close(devFD);
                return -1;
            }
            close(devFD);
        }
        usleep(timeIR[tt]);
    }    
    usleep(10000);
    //PWMStop(pwm);
    close(devFD);
    //system("rmmod "DRIVER_MODULE);
    
    return 0;
}