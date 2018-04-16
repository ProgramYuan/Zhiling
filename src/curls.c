#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#define POSTURL    "http://127.0.0.1/speech"
//#define FILENAME   "curlposttest.log"

// size_t write_data(void* buffer,size_t size,size_t nmemb,void *stream)
// {
// 	FILE *fptr = (FILE*)stream;
// 	fwrite(buffer,size,nmemb,fptr);
// 	return size*nmemb;
// }

void postCURL(void* POSTFIELDS)
{
	//printf("%s",(char *)POSTFIELDS);

	CURL *curl;
	struct curl_slist *headers = NULL; // init to NULL is important 
	headers = curl_slist_append( headers, "Accept: application/json");    
    headers = curl_slist_append( headers, "Content-Type: application/json");  
    headers = curl_slist_append( headers, "charsets: utf-8"); 
	CURLcode res;
	//FILE* fptr;
	struct curl_slist *http_header = NULL;

	// if ((fptr = fopen(FILENAME,"w")) == NULL)
	// {
	// 	pthread_exit("Error,Pthread Exit");
	// }

	curl = curl_easy_init();
	if (!curl)
	{
		pthread_exit("Error,Pthread Exit");
	}

	curl_easy_setopt(curl,CURLOPT_URL,POSTURL); //url地址
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); 
	curl_easy_setopt(curl,CURLOPT_POSTFIELDS,POSTFIELDS); //post参数
	//curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_data); //对返回的数据进行操作的函数地址
	//curl_easy_setopt(curl,CURLOPT_WRITEDATA,fptr); //这是write_data的第四个参数值
	curl_easy_setopt(curl,CURLOPT_POST,1); //设置问非0表示本次操作为post
	//curl_easy_setopt(curl,CURLOPT_VERBOSE,1); //打印调试信息
	//curl_easy_setopt(curl,CURLOPT_HEADER,1); //将响应头信息和相应体一起传给write_data
	curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1); //设置为非0,响应头信息location
	//curl_easy_setopt(curl,CURLOPT_COOKIEFILE,"/Users/zhu/CProjects/curlposttest.cookie");

	res = curl_easy_perform(curl);

	if (res != CURLE_OK)
	{
		pthread_exit("Error,Pthread Exit");
	}

	curl_easy_cleanup(curl);
}

