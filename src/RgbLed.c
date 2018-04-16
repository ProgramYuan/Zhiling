#include <wiringPi.h>
#include <softPwm.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <errno.h>
#define BUFES PIPE_BUF
#define FIFONAME "ledFIFO"
mode_t mode = 0666; /*新创建的FIFO模式*/

#define uchar unsigned char

#define LedPinRed    0
#define LedPinGreen  1
#define LedPinBlue   2

int _red=0;
int _green=0;
int _blue=0;
int _frequency=0;

void ledInit(void)
{
	softPwmCreate(LedPinRed,  0, 100);
	softPwmCreate(LedPinGreen,0, 100);
	softPwmCreate(LedPinBlue, 0, 100);
}

void ledColorSet(uchar r_val, uchar g_val, uchar b_val)
{
	softPwmWrite(LedPinRed,   r_val);
	softPwmWrite(LedPinGreen, g_val);
	softPwmWrite(LedPinBlue,  b_val);
}

int main(void)
{
	int fd;
	int len;
	char buf[BUFES];

	//读数据
	if((fd=open(FIFONAME,O_RDONLY))<0)
	{
		perror("open");
		exit(1);
	}

	if(wiringPiSetup() == -1){
		printf("setup wiringPi failed !");
		return 1; 
	}

	ledInit();

	int da=0;
	while(1)
	{
		if(len=read(fd,buf,BUFES)>0)
		{
			int len = strlen(buf);  
			if(buf[len-1] == '\n')  
			{  
				len--;
				buf[len] = 0; 
			} 

			char *ptr;
			char *p;

			float f_red=0;
			float f_green=0;
			float f_blue=0;
			ptr = strtok_r(buf, ",", &p);
			f_red=atoi(ptr);
			ptr = strtok_r(NULL, ",", &p);
			f_green=atoi(ptr);
			ptr = strtok_r(NULL, ",", &p);
			f_blue=atoi(ptr);
			ptr = strtok_r(NULL, ",", &p);
			_frequency=atoi(ptr);
			ptr = strtok_r(NULL, ",", &p);

			_red=(int)(f_red);
			_green=(int)(f_green);
			_blue=(int)(f_blue);

			printf("%d %d %d %d \n",_red,_green,_blue,_frequency);
			
		}
		else
		{
			if(_frequency>0)
			{
				da++;
				if(da<=127){
					ledColorSet( ((float)(_red)/127*da), ((float)(_green)/127*da) , ((float)(_blue)/127*da) );
					delay(_frequency);
				}else{
					 ledColorSet( ((float)(_red)/127*(255-da)), ((float)(_green)/127*(255-da)), ((float)(_blue)/127*(255-da)) ); 
					delay(_frequency);
				}
				if(da>=255){
					da=0;
				}
			}
			else
			{
				//默认
				ledColorSet( ((float)(_red)/127*da), ((float)(_green)/127*da) , ((float)(_blue)/127*da) );
				delay(100);
				ledColorSet(0,0,0);
				delay(100);

			}
		}
	}

	return 0;
}