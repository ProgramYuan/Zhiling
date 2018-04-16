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
#define HINTS_SIZE  100

static char GrmBuildPath[30];
static char Grammarid[20];
static char asr_params[MAX_PARAMS_LEN] = {};

static int ArousalValue=0;  
static char ArousalWord[10];

const char* session_begin_params	=	"nlp_version =2.0,sch=1,sub=iat,domain = fariat, language = zh_cn, accent = mandarin,aue = speex-wb;10, sample_rate = 16000, result_type = plain, result_encoding = utf8";

typedef struct msg_audio
{
    u_char AudioBuf[4000];
}msg_audio;

typedef struct ssrkeyMatch{
    char device[30];
    int value;
} skeyMatch;


int fileExist(char *file)
{
	if((access(file,F_OK))!=-1)   
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

void writeFIFO(char *fifo,char *string)
{
	int fd;
	if((fd=open(fifo,O_WRONLY|O_NONBLOCK))<0)
	{
		;
	}
	if( write(fd,string,strlen(string)+1)<0 )
	{
		close(fd);
	}
}

int isArousalWords(char *string)
{
	int a_slot = 0;
	cJSON *json = cJSON_Parse(string);
	cJSON *node = NULL;
	node = cJSON_GetObjectItem(json,"ws");
	if(node==NULL || 1 != cJSON_HasObjectItem(json, "ws"))
	{
		return 0;
	}

	if(node->type == cJSON_Array)
	{
		int i;
		for (i = 0; i < cJSON_GetArraySize(node); i++)
		{
			cJSON *slot = NULL;
			cJSON *tnode = cJSON_GetArrayItem(node, i);
			slot = cJSON_GetObjectItem(tnode, "slot");
			if (slot->valuestring != NULL)
			{
				if (strstr(slot->valuestring, ArousalWord))
					a_slot = 1;
			}
			cJSON *arrayCW = cJSON_GetObjectItem(tnode, "cw");
			cJSON *arraySc = cJSON_GetArrayItem(arrayCW, 0);
			cJSON *sc = slot = cJSON_GetObjectItem(arraySc, "sc");
			if (a_slot && sc->valueint > ArousalValue)
			{
				return 1;
			}
		}
	}
	return 0;
}


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


void run_online(msg_audio *t,int size,char* str)
{
	char     		XXOO[6666]					=	{NULL};
	const char*		session_id					=	NULL;
	char			rec_result[BUFFER_SIZE]		=	{NULL};	
	char			hints[HINTS_SIZE]			=	{NULL}; 
	unsigned int	total_len					=	0; 
	int				aud_stat					=	MSP_AUDIO_SAMPLE_CONTINUE ;		
	int				ep_stat						=	MSP_EP_LOOKING_FOR_SPEECH;		
	int				rec_stat					=	MSP_REC_STATUS_SUCCESS ;		
	int				errcode						=	MSP_SUCCESS ;
	int ret = 0;

	session_id = QISRSessionBegin(NULL,session_begin_params,&errcode);
	if(MSP_SUCCESS != errcode)
	{
		QISRSessionEnd(session_id, hints);
	}
	int i;
	for(i=0;i<size;i++)
	{
		aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;
		if(i==0)
			aud_stat = MSP_AUDIO_SAMPLE_FIRST;
		ret = QISRAudioWrite(session_id,(const void *)t[i].AudioBuf,4000,aud_stat,&ep_stat,&rec_stat);
		if(MSP_SUCCESS != ret)
		{
			QISRSessionEnd(session_id, hints);
		}
	}
	errcode = QISRAudioWrite(session_id, NULL, 0, MSP_AUDIO_SAMPLE_LAST, &ep_stat, &rec_stat);
	if(MSP_SUCCESS != errcode)
	{
		QISRSessionEnd(session_id, hints);
	}
	while (MSP_REC_STATUS_COMPLETE != rec_stat) 
	{
		const char *rslt = QISRGetResult(session_id, &rec_stat, 0, &errcode);
		if (MSP_SUCCESS != errcode)
		{
			printf("\nQISRGetResult failed, error code: %d\n", errcode);
			QISRSessionEnd(session_id, hints);
		}
		if (NULL != rslt)
		{
			unsigned int rslt_len = strlen(rslt);
			total_len += rslt_len;
			if (total_len >= BUFFER_SIZE)
			{
				printf("\nno enough buffer for rec_result !\n");
				QISRSessionEnd(session_id, hints);
			}
			strncat(rec_result, rslt, rslt_len);
		}
		usleep(1000); 
	}
	do{
		if(NULL==str)
		{
			writeFIFO("resultFIFO",rec_result);
		}
		else
		{
			strcpy(XXOO,str);
			strncat(XXOO,"####",strlen("####"));
			strncat(XXOO,rec_result,strlen(rec_result));
			writeFIFO("resultFIFO",XXOO);
		}
	}while (MSP_REC_STATUS_COMPLETE != rec_stat);
	QISRSessionEnd(session_id, hints);
}


void run_asr(msg_audio *t,int size,skeyMatch *k,int ki)
{
	const char *rec_rslt               = NULL;
	const char *session_id             = NULL;
	int aud_stat                       = MSP_AUDIO_SAMPLE_CONTINUE;
	int ep_status                      = MSP_EP_LOOKING_FOR_SPEECH;
	int rec_status                     = MSP_REC_STATUS_INCOMPLETE;
	int rss_status                     = MSP_REC_STATUS_INCOMPLETE;
	int errcode                        = -1;
	int switchOnline				   = 0;

    session_id=QISRSessionBegin(NULL,asr_params,&errcode);
	if(NULL == session_id)
		printf("ID null\n");

	int i;
	for(i=0; i<size; i++)
	{
		aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;
		if(i==0)
			aud_stat = MSP_AUDIO_SAMPLE_FIRST;
		errcode = QISRAudioWrite(session_id, (const void *)t[i].AudioBuf, 4000, aud_stat, &ep_status, &rec_status);
		if (MSP_SUCCESS != errcode)
		{
			QISRSessionEnd(session_id, NULL);
			printf("\nQISRAudioWrite error code:%d\n",errcode);
		}
	}
	QISRAudioWrite(session_id, (const void *)NULL, 0, MSP_AUDIO_SAMPLE_LAST, &ep_status, &rec_status);
	while (MSP_REC_STATUS_COMPLETE != rss_status && MSP_SUCCESS == errcode )
	{
		rec_rslt = QISRGetResult(session_id, &rss_status, 0, &errcode);
		usleep(1000);
	}
	char recRslt[1024]={NULL};
	if (NULL != rec_rslt)
	{
		strcpy(recRslt,rec_rslt);
	}
	QISRSessionEnd(session_id, NULL);

	if(isArousalWords(recRslt))
	{
		switchOnline = 1;
		creat("online",0755);
	}
	else
	{
		int i;
		for(i=0;i<ki;i++)
		{
			if(isGoOn(recRslt,k[i].device,k[i].value))
			{
				switchOnline = 1;
				//调用在线
				writeFIFO("postFIFO","1");
				run_online(t,size,recRslt);
				break;
			}
		}
	}
	if(switchOnline==0)
		writeFIFO("resultFIFO",recRslt);

}


void main(int argc,char* argv[])
{
	
	FILE *r=fopen("ssrkeys.dat","r");
    assert(r!=NULL);
    skeyMatch kMatch[30];
    int ki=0;
    while(fscanf(r,"%s%d",kMatch[ki].device,&kMatch[ki].value)!=EOF)
    {
        ki++;
    }
    fclose(r);
	
	FILE *fdArousal=fopen("arousal.dat","r");
	assert(fdArousal!=NULL);
	fscanf(fdArousal,"%s%d",ArousalWord,&ArousalValue);
	fclose(fdArousal);
	
	int ret = 0;
	const char *login_config = "appid = 585b756e, work_dir = .";
	
    void *shm = NULL;
    struct shared_use_st *shared=NULL;
    int shmid;  

	strcpy(Grammarid,argv[1]);
	ret=MSPLogin(NULL,NULL,login_config);
	if(MSP_SUCCESS != ret)
	{
		printf("Login Failed: %d\n",ret);
		MSPLogout();		
	}
	strcpy(GrmBuildPath,str_contact( "res/asr/GrmBuilld",Grammarid ));
	snprintf(asr_params, MAX_PARAMS_LEN - 1,
             "engine_type = local, \
		asr_res_path = %s, sample_rate = %d, \
		grm_build_path = %s, local_grammar = %s, \
		result_type = json, result_encoding = UTF-8, ",
             "fo|res/asr/common.jet",
             SAMPLE_RATE_16K,
             GrmBuildPath,
             Grammarid);

    shmid = shmget((key_t)92919, sizeof(struct shared_use_st), 0666|IPC_CREAT);  
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
    msg_audio audio_data[41];
    int ssr_audio_i=0;
    while (1)
    {
        if(shared->written != 0)  
        {
            QISEnd=0;
            if(shared->flag==1)
            {
                ssr_audio_i=0;
                shared->canwrite=0;
            }
            else if(shared->flag==3)
            {
                QISEnd=1;
            } 

            memcpy(audio_data[ssr_audio_i].AudioBuf,shared->AudioBuf,4000);
            ssr_audio_i++;

            shared->written = 0;  
        }
        if(QISEnd==1) 
        {
			if(fileExist("shOnline"))
			{
				run_online(audio_data,ssr_audio_i,NULL);
				if(unlink("shOnline")<0)
				{
					remove("shOnline");
				}
			}
			else
			{
				run_asr(audio_data,ssr_audio_i,kMatch,ki);
			}

            QISEnd=0;
            shared->canwrite=1;
            
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
