#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include "../libMatrix/includes/libfahw.h"

#define STATUS_CHANGE_TIMES     (5)

int main(int argc, char ** argv) 
{
    int pin = GPIO_PIN(7); 
    int value, board;
    int ret = -1;
    
    if ((board = boardInit()) < 0) {
        printf("Fail to init board\n");
        return -1;
    }
    if (board == BOARD_NANOPC_T2 || board == BOARD_NANOPC_T3)
        pin = GPIO_PIN(15);
    
    if (argc == 2)
        pin = GPIO_PIN(atoi(argv[1]));
    if ((ret = exportGPIOPin(pin)) == -1) {   
        printf("exportGPIOPin(%d) failed\n", pin);
    }
    if ((ret = setGPIODirection(pin, GPIO_IN)) == -1) {
        printf("setGPIODirection(%d) failed\n", pin);
    }

    int startFlag=2;  //用来判断用户是否进行红外录入,当1时为录入状态
    int stopFlag=0;  //用来判断是否停止
    int tmpValue=1;
    struct gpioArray{
        int value;  //数值
        long microSeconds;  //微秒
    };

    struct timeval tv;

    static int k=0;
    static struct gpioArray _gpioArray[500];
    //long l_gpioArray[500];
    while(1)
    {
        value=getGPIOValue(pin);
        if(tmpValue!=value)
        {
            gettimeofday(&tv,NULL);
            _gpioArray[k].value=value;
            _gpioArray[k].microSeconds=tv.tv_usec;
            
            tmpValue=value;
            stopFlag=0;
            startFlag=1;
            k++;
        }
        if(value==1 && startFlag==1)
        {
            stopFlag+=1;
        }

        if(stopFlag>200)
            break;
    }
    int j=0;
    for(j=0;j<k;j++)
    {
        printf("%ld,",_gpioArray[j+1].microSeconds-_gpioArray[j].microSeconds);
        //l_gpioArray[j]=_gpioArray[j+1].microSeconds-_gpioArray[j].microSeconds;
    }
    printf("\n");
    
    unexportGPIOPin(pin);
    
    return 0;
}
