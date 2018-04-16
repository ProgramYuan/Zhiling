#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <assert.h>
#include <fcntl.h>
#include "shmdata.h"
#include "cJSON.h"

#include "qisr.h"
#include "msp_cmn.h"
#include "msp_errors.h"

static int use_strftime = 0;

#define SAMPLE_RATE_16K (16000)
#define SAMPLE_RATE_8K (8000)
#define MAX_GRAMMARID_LEN (32)
#define MAX_PARAMS_LEN (1024)

#define	BUFFER_SIZE	4096
#define FRAME_LEN	640 
#define HINTS_SIZE  100

static char GrmBuildPath[30];
static char Grammarid[20];

static char XXOO[6666];

const char* session_begin_params	=	"nlp_version =2.0,sch=1,sub=iat,domain = fariat, language = zh_cn, accent = mandarin,aue = speex-wb;10, sample_rate = 16000, result_type = plain, result_encoding = utf8";

struct msg_audio
{
    u_char AudioBuf[4000];
};

typedef struct ssrkeyMatch{
    char device[30];
    int value;
} skeyMatch;


int isGoOn(char *string,char slotStr[32],int scValue)
{
	int a_slot = 0;
	cJSON *json = cJSON_Parse(string);
	cJSON *node = NULL;
	node = cJSON_GetObjectItem(json, "ws");
	if (node == NULL)
	{
		return 0;
	}
	if (1 != cJSON_HasObjectItem(json, "ws"))
	{
		return 0;
	}

	node = cJSON_GetObjectItem(json, "ws");
	if (node->type == cJSON_Array)
	{
		int i;
		for (i = 0; i < cJSON_GetArraySize(node); i++)
		{
			cJSON *slot = NULL;
			cJSON *tnode = cJSON_GetArrayItem(node, i);
			slot = cJSON_GetObjectItem(tnode, "slot");
			if (slot->valuestring != NULL)
			{
				if (strstr(slot->valuestring, slotStr))
					a_slot = 1;
			}

			cJSON *arrayCW = cJSON_GetObjectItem(tnode, "cw");
			cJSON *arraySc = cJSON_GetArrayItem(arrayCW, 0);
			cJSON *sc = slot = cJSON_GetObjectItem(arraySc, "sc");
			
			if (a_slot && sc->valueint > scValue)
			{
				return 1;
			}
		}

	}
	return 0;
}


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

char *str_contact(const char *str1, const char *str2)
{
    char *result;
    result = (char *)malloc(strlen(str1) + strlen(str2) + 1); 
    if (!result)
    { 
        printf("Error: malloc failed in concat! \n");
        exit(EXIT_FAILURE);
    }
    strcpy(result, str1);
    strcat(result, str2); 
    return result;
}



void run_iat(const char* audio_file, const char* session_begin_params)
{
	const char*		session_id					=	NULL;
	char			rec_result[BUFFER_SIZE]		=	{NULL};	
	char			hints[HINTS_SIZE]			=	{NULL}; 
	unsigned int	total_len					=	0; 
	int				aud_stat					=	MSP_AUDIO_SAMPLE_CONTINUE ;		//音频状态
	int				ep_stat						=	MSP_EP_LOOKING_FOR_SPEECH;		//端点检测
	int				rec_stat					=	MSP_REC_STATUS_SUCCESS ;			//识别状态
	int				errcode						=	MSP_SUCCESS ;

	FILE*			f_pcm						=	NULL;
	char*			p_pcm						=	NULL;
	long			pcm_count					=	0;
	long			pcm_size					=	0;
	long			read_size					=	0;

	
	if (NULL == audio_file)
		goto iat_exit;

	f_pcm = fopen(audio_file, "rb");
	if (NULL == f_pcm) 
	{
		printf("\nopen [%s] failed! \n", audio_file);
		goto iat_exit;
	}
	
	fseek(f_pcm, 0, SEEK_END);
	pcm_size = ftell(f_pcm); 
	fseek(f_pcm, 0, SEEK_SET);		

	p_pcm = (char *)malloc(pcm_size);
	if (NULL == p_pcm)
	{
		printf("\nout of memory! \n");
		goto iat_exit;
	}

	read_size = fread((void *)p_pcm, 1, pcm_size, f_pcm); 
	if (read_size != pcm_size)
	{
		printf("\nread [%s] error!\n", audio_file);
		goto iat_exit;
	}
	
	session_id = QISRSessionBegin(NULL, session_begin_params, &errcode); 
	if (MSP_SUCCESS != errcode)
	{
		printf("\nQISRSessionBegin failed! error code:%d\n", errcode);
		goto iat_exit;
	}
	
	while (1) 
	{
		unsigned int len = 10 * FRAME_LEN; 
		int ret = 0;

		if (pcm_size < 2 * len) 
			len = pcm_size;
		if (len <= 0)
			break;

		aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;
		if (0 == pcm_count)
			aud_stat = MSP_AUDIO_SAMPLE_FIRST;

		ret = QISRAudioWrite(session_id, (const void *)&p_pcm[pcm_count], len, aud_stat, &ep_stat, &rec_stat);
		if (MSP_SUCCESS != ret)
		{
			printf("\nQISRAudioWrite failed! error code:%d\n", ret);
			goto iat_exit;
		}
			
		pcm_count += (long)len;
		pcm_size  -= (long)len;
		
		if (MSP_REC_STATUS_SUCCESS == rec_stat)
		{
			const char *rslt = QISRGetResult(session_id, &rec_stat, 0, &errcode);
			if (MSP_SUCCESS != errcode)
			{
				printf("\nQISRGetResult failed! error code: %d\n", errcode);
				goto iat_exit;
			}
			if (NULL != rslt)
			{
				unsigned int rslt_len = strlen(rslt);
				total_len += rslt_len;
				if (total_len >= BUFFER_SIZE)
				{
					printf("\nno enough buffer for rec_result !\n");
					goto iat_exit;
				}
				strncat(rec_result, rslt, rslt_len);
			}
		}

		if (MSP_EP_AFTER_SPEECH == ep_stat)
			break;
		usleep(1000); 
	}
	errcode = QISRAudioWrite(session_id, NULL, 0, MSP_AUDIO_SAMPLE_LAST, &ep_stat, &rec_stat);
	if (MSP_SUCCESS != errcode)
	{
		printf("\nQISRAudioWrite failed! error code:%d \n", errcode);
		goto iat_exit;	
	}

	while (MSP_REC_STATUS_COMPLETE != rec_stat) 
	{
		const char *rslt = QISRGetResult(session_id, &rec_stat, 0, &errcode);
		if (MSP_SUCCESS != errcode)
		{
			printf("\nQISRGetResult failed, error code: %d\n", errcode);
			goto iat_exit;
		}
		if (NULL != rslt)
		{
			unsigned int rslt_len = strlen(rslt);
			total_len += rslt_len;
			if (total_len >= BUFFER_SIZE)
			{
				printf("\nno enough buffer for rec_result !\n");
				goto iat_exit;
			}
			strncat(rec_result, rslt, rslt_len);
		}
		usleep(1000);
	}
	
    strncat(XXOO,"####",strlen("####"));
    strncat(XXOO,rec_result,strlen(rec_result));
    goto iat_exit;
    //remove("zaixian.pcm");

iat_exit:
    //remove("zaixian.pcm");
	if (NULL != f_pcm)
	{
		fclose(f_pcm);
		f_pcm = NULL;
	}
	if (NULL != p_pcm)
	{	free(p_pcm);
		p_pcm = NULL;
	}

	QISRSessionEnd(session_id, hints);
}

void main(int argc,char* argv[])
{
    //int switcher=1;  //开关 ，1 离线 0 在线
    FILE *r=fopen("ssrkeys.dat","r");
    assert(r!=NULL);
    skeyMatch kMatch[30];
    int ki=0;
    while(fscanf(r,"%s%d",kMatch[ki].device,&kMatch[ki].value)!=EOF)
    {
        ki++;
    }
    fclose(r);

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
    
    ret = MSPLogin(NULL, NULL, login_config); 
    if (MSP_SUCCESS != ret)
    {
        printf("登录失败：%d\n", ret);
        MSPLogout();
    }
    strcpy(GrmBuildPath,str_contact("res/asr/GrmBuilld", Grammarid));

    snprintf(asr_params, MAX_PARAMS_LEN - 1,
             "engine_type = local, \
		asr_res_path = %s, sample_rate = %d, \
		grm_build_path = %s, local_grammar = %s, \
		result_type = json, result_encoding = UTF-8, ",
             "fo|res/asr/common.jet",
             SAMPLE_RATE_16K,
             GrmBuildPath,
             Grammarid);

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
    
    shared = (struct shared_use_st*)shm;  
    shared->written = 0;  
    shared->canwrite=1;

    int QISEnd=0;
    struct msg_audio audio_data[41];
    int ssr_audio_i=0;
    while (running)
    {
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
            //audio_data[ssr_audio_i].AudioBuf=(u_char *)malloc(4000);
            memcpy(audio_data[ssr_audio_i].AudioBuf,shared->AudioBuf,4000);
            ssr_audio_i++;

            if (MSP_SUCCESS != errcode)
            {
                QISRSessionEnd(session_id, NULL);
                printf("error code:%d\n",errcode);
            }
            shared->written = 0;  
        }
        if(QISEnd==1 && errcode==MSP_SUCCESS) 
        {
            rss_status = MSP_REC_STATUS_INCOMPLETE;
            //获取识别结果
            //printf("开始识别\n");
            while (MSP_REC_STATUS_COMPLETE != rss_status && MSP_SUCCESS == errcode)
            {
                rec_rslt = QISRGetResult(session_id, &rss_status, 0, &errcode);
                usleep(1000);
            }
            if(NULL != rec_rslt)
            {
                //switcher=1;
                char result[2014]={NULL};
                strcpy(result,rec_rslt);
                //把值传给全局
                strcpy(XXOO,"");
                strcpy(XXOO,rec_rslt);

                QISRSessionEnd(session_id, NULL);
                rec_rslt=NULL;
                rss_status = MSP_REC_STATUS_INCOMPLETE;
                rec_status = MSP_REC_STATUS_INCOMPLETE;

                int ssr_i=0;
                for(ssr_i=0;ssr_i<ki;ssr_i++)
                {
                    if(isGoOn(result,kMatch[ssr_i].device,kMatch[ssr_i].value))
                    {
                        QISRSessionEnd(session_id, NULL);

                        int fd;
                        if((fd=open("postFIFO",O_WRONLY|O_NONBLOCK))<0)
                        {
                            ;
                        }
                        if(write(fd, "1", 2)<0)
                        {
                            close(fd);
                        }

                        remove("zaixian.pcm");
                        int iat_fd;
                        iat_fd=safe_open("zaixian.pcm");
                        if(iat_fd < 0)
                        {;}
                        for(ssr_i=0;ssr_i<ssr_audio_i;ssr_i++)
                        {
                            if(write(iat_fd,audio_data[ssr_i].AudioBuf,4000)!=4000)
                            {
                                ;
                            }
                        }
                        close(iat_fd);
                        
                        run_iat("zaixian.pcm", session_begin_params); 
                        break;
                    }
                }

                int r_fd;
                if((r_fd=open("resultFIFO",O_WRONLY|O_NONBLOCK))<0)
                {
                    ;
                }
                if(write(r_fd, XXOO, strlen(XXOO)+1)<0)
                {
                    close(r_fd);
                }
                //printf("%s\n",XXOO);
            }
            QISEnd=0;
            //rec_status = MSP_REC_STATUS_INCOMPLETE;
            shared->canwrite=1;
            //printf("识别结束\n");
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
