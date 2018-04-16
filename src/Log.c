#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define FILE_MAX_SIZE (1024*1024)

void sysUsecTime(char* buffer)  
{  
    struct timeval tv;  
    struct timezone tz;
    struct tm *p;  
      
    gettimeofday(&tv, &tz);  
    //printf("tv_sec:%ld\n",tv.tv_sec);  
    //printf("tv_usec:%ld\n",tv.tv_usec);  
    //printf("tz_minuteswest:%d\n",tz.tz_minuteswest);  
    //printf("tz_dsttime:%d\n",tz.tz_dsttime);  
    p = localtime(&tv.tv_sec);  
    //printf("time_now:%d%d%d.%ld\n", p->tm_hour, p->tm_min, p->tm_sec, tv.tv_usec);  
    sprintf(buffer, "%d-%d-%d.%ld", 
		p->tm_hour, p->tm_min, p->tm_sec,tv.tv_usec);
}  

/*
获得文件大小
@param filename [in]: 文件名
@return 文件大小
*/
long get_file_size(char* filename)
{
	long length = 0;
	FILE *fp = NULL;

	fp = fopen(filename, "rb");
	if (fp != NULL)
	{
		fseek(fp, 0, SEEK_END);
		length = ftell(fp);
	}

	if (fp != NULL)
	{
		fclose(fp);
		fp = NULL;
	}

	return length;
}

/*
写入日志文件
@param filename [in]: 日志文件名
@param max_size [in]: 日志文件大小限制
@param buffer [in]: 日志内容
@param buf_size [in]: 日志内容大小
@return 空
*/
void write_log_file(char* filename, long max_size, char* buffer, unsigned buf_size)
{
	if (filename != NULL && buffer != NULL)
	{
		// 文件超过最大限制, 删除
		long length = get_file_size(filename);

		if (length > max_size)
		{
			unlink(filename); // 删除文件
		}

		// 写日志
		{
			FILE *fp;
			fp = fopen(filename, "at+");
			if (fp != NULL)
			{
				char now[32];
				memset(now, 0, sizeof(now));
				sysUsecTime(now);
				fwrite(now, strlen(now)+1, 1, fp);
				fwrite(buffer, buf_size, 1, fp);

				fclose(fp);
				fp = NULL;
			}
		}
	}
}

void logWrite(char* i)
{
    char buffer[50];
	memset(buffer, 0, sizeof(buffer));
	sprintf(buffer, "====> %s\n", i);
	write_log_file("log.txt", FILE_MAX_SIZE, buffer, strlen(buffer));
}