#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <string.h>

#include "../inc/qisr.h"
#include "../inc/msp_cmn.h"
#include "../inc/msp_errors.h"
//#include "../inc/postData.h"

//#define Conditional 1

#define SAMPLE_RATE_16K (16000)
#define SAMPLE_RATE_8K (8000)
#define MAX_GRAMMARID_LEN (32)
#define MAX_PARAMS_LEN (1024)

//void logWrite(char* i);
//int MSPAPI MSPLogin(const char* usr, const char* pwd, const char* params);
//int postCURL(char *POSTFIELDS);
//const char *ASR_RES_PATH = "fo|res/asr/common.jet"; //离线语法识别资源路径
//const char *GRM_BUILD_PATH = "res/asr/GrmBuilld";   //构建离线语法识别网络生成数据保存路径

static char GrmBuildPath[30];
char AudioFile[16];
static char Grammarid[20];

//读取消息队列，有新消息进入把消息添加到列表中
/*用于消息收发的结构体--my_msg_type：消息类型，some_text：消息正文*/
struct my_msg_st
{
    long int my_msg_type;
    char some_text[BUFSIZ];
};

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

int run_asr()
{
    char asr_params[MAX_PARAMS_LEN] = {};
    const char *rec_rslt = NULL;
    const char *session_id = NULL;
    FILE *f_pcm = NULL;
    char *pcm_data = NULL;
    long pcm_count = 0;
    long pcm_size = 0;
    int last_audio = 0;
    int aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;
    int ep_status = MSP_EP_LOOKING_FOR_SPEECH;
    int rec_status = MSP_REC_STATUS_INCOMPLETE;
    int rss_status = MSP_REC_STATUS_INCOMPLETE;
    int errcode = -1;

    f_pcm = fopen(AudioFile, "rb");

    #ifdef Conditional
        printf("%s\t进入语音识别\n",AudioFile);
    #endif

    if (NULL == f_pcm)
    {
        printf("open\"%s\"failed！[%s]\n", f_pcm, strerror(errno));
        goto run_error;
    }
    fseek(f_pcm, 0, SEEK_END);
    pcm_size = ftell(f_pcm);
    fseek(f_pcm, 0, SEEK_SET);
    pcm_data = (char *)malloc(pcm_size);
    if (NULL == pcm_data)
        goto run_error;
    fread((void *)pcm_data, pcm_size, 1, f_pcm);
    fclose(f_pcm);
    f_pcm = NULL;

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

    session_id = QISRSessionBegin(NULL, asr_params, &errcode);
    if (NULL == session_id)
        goto run_error;

    while (1)
    {
        unsigned int len = 6400;

        if (pcm_size < 12800)
        {
            len = pcm_size;
            last_audio = 1;
        }

        aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;

        if (0 == pcm_count)
            aud_stat = MSP_AUDIO_SAMPLE_FIRST;

        if (len <= 0)
            break;

        fflush(stdout);
        errcode = QISRAudioWrite(session_id, (const void *)&pcm_data[pcm_count], len, aud_stat, &ep_status, &rec_status);
        if (MSP_SUCCESS != errcode)
            goto run_error;

        pcm_count += (long)len;
        pcm_size -= (long)len;

        if (MSP_EP_AFTER_SPEECH == ep_status)
            break;
    }
    
    QISRAudioWrite(session_id, (const void *)NULL, 0, MSP_AUDIO_SAMPLE_LAST, &ep_status, &rec_status);

    free(pcm_data);
    pcm_data = NULL;

    //获取识别结果
    while (MSP_REC_STATUS_COMPLETE != rss_status && MSP_SUCCESS == errcode)
    {
        rec_rslt = QISRGetResult(session_id, &rss_status, 0, &errcode);
        //usleep(100 * 1000);
        //sleep(150);
    }
    
    if (NULL != rec_rslt)
    {
        //pthread_t idPost;
        //int ret;
        //char rec_result[2014];
        //strcpy(rec_result, rec_rslt);
        #ifdef Conditional
            printf("%s\n",rec_rslt);
        #endif
        //logWrite("POST数据");
        //创建FIFO通道将数据额传递给node
        int fd;
        if((fd=open("postFIFO",O_WRONLY))<0)
        {
            perror("open");
        }
        if(write(fd, rec_rslt, strlen(rec_rslt)+1)<0)
        {
            perror("write");
            close(fd);
        }
        //ret = pthread_create(&idPost, NULL, (void *)postCURL, (void *)&rec_result);
        goto run_exit;
    }

    goto run_exit;

run_error:
    if (NULL != pcm_data)
    {
        free(pcm_data);
        pcm_data = NULL;
    }
    if (NULL != f_pcm)
    {
        fclose(f_pcm);
        f_pcm = NULL;
    }
run_exit:
    QISRSessionEnd(session_id, NULL);
    remove(AudioFile);

    return errcode;
}

void main(int argc, char *argv[])
{

    //先从文件中读取通道key，用来建立通道
	int sKey;
	FILE *sKey_stream;
	sKey_stream=fopen("s.key","r");
	if(sKey_stream==NULL)
	{
		printf("s.Key was not opened\n");
		return;
	}
	else
	{
		//fseek(sKey_stream,0L,SEEK_SET);
		fscanf(sKey_stream,"%d",&sKey);
		fclose(sKey_stream);
	}
	#ifdef Conditional
		printf("skey is %d \n",sKey);
	#endif


    const char *login_config = "appid = 585b756e"; //登录参数
    int ret = 0;
    char c;

    //消息队列定义
    int running = 1; //程序运行标识符
    int msgid;       //消息队列标识符
    struct my_msg_st some_data;
    long int msg_to_receive = 0; //接收消息的类型--0表示msgid队列上的第一个消息

    strcpy(Grammarid,argv[1]);
    
    ret = MSPLogin(NULL, NULL, login_config); //第一个参数为用户名，第二个参数为密码，传NULL即可，第三个参数是登录参数
    if (MSP_SUCCESS != ret)
    {
        printf("登录失败：%d\n", ret);
        MSPLogout();
    }

    /*创建消息队列*/
    msgid = msgget((key_t)sKey, 0666 | IPC_CREAT);
    if (msgid == -1)
    {
        fprintf(stderr, "msgget failed with error: %d/n", errno);
        exit(EXIT_FAILURE);
    }
    int i;
    for(i=0;i<100;i++)
    {
        if (msgrcv(msgid, (void *)&some_data, BUFSIZ, msg_to_receive, IPC_NOWAIT) == -1)
        {
            ;
        }
    }

    strcpy(GrmBuildPath,str_contact("res/asr/GrmBuilld", Grammarid));
    #ifdef Conditional
        printf("%s\n",GrmBuildPath);
    #endif
    /*接收消息*/
    while (running)
    {
        if (msgrcv(msgid, (void *)&some_data, BUFSIZ, msg_to_receive, 0) == -1)
        {
            fprintf(stderr, "msgrcv failed with error: %d/n", errno);
            exit(EXIT_FAILURE);
        }
        //此处调用识别模块
        //从配置文件中读取ID
        
        memset(AudioFile,0,16);
        strcpy(AudioFile,some_data.some_text);
        
        #ifdef Conditional
            printf("%s\n",AudioFile);
        #endif

        ret = run_asr();
        if (MSP_SUCCESS != ret)
        {
            printf("离线语法识别出错: %d \n", ret);
            //MSPLogout();
        }
    }
    /*删除消息队列*/
    if (msgctl(msgid, IPC_RMID, 0) == -1)
    {
        fprintf(stderr, "msgctl(IPC_RMID) failed/n");
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}
