#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <fcntl.h>
#include "shmdata.h"

#include "qisr.h"
#include "msp_cmn.h"
#include "msp_errors.h"

#define SAMPLE_RATE_16K (16000)
#define SAMPLE_RATE_8K (8000)
#define MAX_GRAMMARID_LEN (32)
#define MAX_PARAMS_LEN (1024)

#define	BUFFER_SIZE	4096
#define HINTS_SIZE  100

static int use_strftime = 0;

static char GrmBuildPath[30];
static char Grammarid[20];

struct msg_audio
{
    u_char *AudioBuf;
};


int create_path(const char *path) {
	char *start;
	mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

	if (path[0] == '/')
		start = strchr(path + 1, '/');
	else
		start = strchr(path, '/');

	while (start) {
		char *buffer = strdup(path);
		buffer[start - path] = 0x00;

		if (mkdir(buffer, mode) == -1 && errno != EEXIST) {
			fprintf(stderr, "Problem creating directory %s", buffer);
			perror(" ");
			free(buffer);
			return -1;
		}
		free(buffer);
		start = strchr(start + 1, '/');
	}
	return 0;
}

static int safe_open(const char *name) {
	int fd;

	fd = open(name, O_WRONLY | O_CREAT, 0644);
	if (fd == -1) {
		if (errno != ENOENT || !use_strftime)
			return -1;
		if (create_path(name) == 0)
			fd = open(name, O_WRONLY | O_CREAT, 0644);
	}
	return fd;
}

/**
 ** 字符串拼接方法
 ** Coded by Allen Yuan
 **/
char *str_contact(const char *str1, const char *str2)
{
    char *result;
    result = (char *)malloc(strlen(str1) + strlen(str2) + 1); //str1的长度 + str2的长度 + \0;
    if (!result)
    { //如果内存动态分配失败
        printf("Error: malloc failed in concat! \n");
        exit(EXIT_FAILURE);
    }
    strcpy(result, str1);
    strcat(result, str2); //字符串拼接
    return result;
}

void main(int argc,char* argv[])
{
    char *ssr_match[10];
    //从文本中读取内容  空调 天气
    FILE* fp_keys;
    char f_key[30];
    fp_keys=fopen("ssrkeys.dat","r");
    int match_i=0;
    while( (fgets(f_key,28,fp_keys))!=NULL )
    {
        if(strlen(f_key)!=1)
        {
            ssr_match[match_i]=(char*)malloc(30);
            f_key[strlen(f_key)-1]='\0';
            strcpy(ssr_match[match_i],f_key);
            match_i++;
        }
    }

    int ret = 0;
    const char *login_config="appid = 585b756e, work_dir = .";

    char asr_params[MAX_PARAMS_LEN] = {};
    const char *rec_rslt = NULL;
    const char *iat_rec_rslt=NULL;
    
    const char *session_id = NULL;
    int aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;
    int ep_status = MSP_EP_LOOKING_FOR_SPEECH;
    int rec_status = MSP_REC_STATUS_INCOMPLETE;
    int rss_status = MSP_REC_STATUS_INCOMPLETE;
    int errcode = -1;

    int running = 1;
    void *shm = NULL;
    struct shared_use_st *shared=NULL;
    int shmid;
    
    strcpy(Grammarid,argv[1]);
    
    //语音识别此处
    ret = MSPLogin(NULL, NULL, login_config); 
    if (MSP_SUCCESS != ret)
    {
        printf("登录失败：%d\n", ret);
        MSPLogout();
    }
    strcpy(GrmBuildPath,str_contact("res/asr/GrmBuilld", Grammarid));

    //离线语法识别参数设置
    snprintf(asr_params, MAX_PARAMS_LEN - 1,
             "engine_type = local, \
		asr_res_path = %s, sample_rate = %d, \
		grm_build_path = %s, local_grammar = %s, \
		result_type = json, result_encoding = UTF-8, ",
             "fo|res/asr/common.jet",
             SAMPLE_RATE_16K,
             GrmBuildPath,
             Grammarid);

    //内存共享此处
    shmid = shmget((key_t)33225, sizeof(struct shared_use_st), 0666|IPC_CREAT);  
    if(shmid == -1)  
    {  
        fprintf(stderr, "shmget failed\n");  
        exit(EXIT_FAILURE);  
    }  
    
    shm = shmat(shmid, (void*)0, 0);  
    if(shm == (void*)-1)  
    {  
        fprintf(stderr, "shmat failed\n");  
        exit(EXIT_FAILURE);  
    }  
    //printf("\nMemory attached at %X\n", (int)shm);  
    shared = (struct shared_use_st*)shm;  
    shared->written = 0;  
    shared->canwrite=1;

    int QISEnd=0;
    struct msg_audio audio_data[41];
    int ssr_audio_i=0;
    while (running)
    {
        //没有进程向共享内存定数据有数据可读取  
        if(shared->written != 0)  
        {
            rec_status = MSP_REC_STATUS_INCOMPLETE;

            QISEnd=0;
            if(shared->flag==1)
            {
                ssr_audio_i=0;

                shared->canwrite=0;
                QISRSessionEnd(session_id, NULL);
                session_id=NULL;
                rec_rslt=NULL;

                session_id = QISRSessionBegin(NULL, asr_params, &errcode);
                //printf("sessionid:%s\n",session_id);
                if (NULL == session_id)
                    printf("ID null\n");

                aud_stat = MSP_AUDIO_SAMPLE_FIRST;
            }
            else if(shared->flag==2)
            {
                aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;
            }
            else if(shared->flag==3)
            {
                aud_stat = MSP_AUDIO_SAMPLE_LAST;
                QISEnd=1;
            } 
            errcode = QISRAudioWrite(session_id, (const void *)shared->AudioBuf, 4000, aud_stat, &ep_status, &rec_status);
            audio_data[ssr_audio_i].AudioBuf=(u_char *)malloc(4000);
            memcpy(audio_data[ssr_audio_i].AudioBuf,shared->AudioBuf,4000);
            ssr_audio_i++;

            if (MSP_SUCCESS != errcode)
            {
                QISRSessionEnd(session_id, NULL);
                printf("error code:%d\n",errcode);
            }
            shared->written = 0;  
        }
        if(QISEnd==1 && errcode==MSP_SUCCESS) //结束
        {
            rss_status = MSP_REC_STATUS_INCOMPLETE;
            //获取识别结果
            printf("开始识别\n");
            while (MSP_REC_STATUS_COMPLETE != rss_status && MSP_SUCCESS == errcode)
            {
                rec_rslt = QISRGetResult(session_id, &rss_status, 0, &errcode);
                usleep(1000);
            }
            if(NULL != rec_rslt)
            {
                char result[2014]={NULL};
                strcpy(result,rec_rslt);
                QISRSessionEnd(session_id, NULL);
                rec_rslt=NULL;
                rss_status = MSP_REC_STATUS_INCOMPLETE;
                rec_status = MSP_REC_STATUS_INCOMPLETE;

                //写数据到本地
                int ssr_i=0;
                int iat_fd;
                iat_fd=safe_open("zaixian.pcm");
                if(iat_fd < 0)
                {;}
                printf("match_i:%d\n",match_i);
                for(ssr_i=0;ssr_i<ssr_audio_i;ssr_i++)
                {
                    if(write(iat_fd,audio_data[ssr_i].AudioBuf,4000)!=4000)
                    {
                        ;
                    }
                }
                printf("\n离线：%s\n",result);
            }
            QISEnd=0;
            //rec_status = MSP_REC_STATUS_INCOMPLETE;
            shared->canwrite=1;
            printf("识别结束\n");
        }     
    }

    if(shmdt(shm) == -1)  
    {  
        fprintf(stderr, "shmdt failed\n");  
        exit(EXIT_FAILURE);  
    }  
    if(shmctl(shmid, IPC_RMID, 0) == -1)  
    {  
        fprintf(stderr, "shmctl(IPC_RMID) failed\n");  
        exit(EXIT_FAILURE);  
    }  
    exit(EXIT_SUCCESS); 

}
