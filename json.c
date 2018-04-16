#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include"cJSON.h"

int main(void)
{
    char *string = "{\"family\":[\"father\",\"mother\",\"brother\",\"sister\",\"somebody\"]}";
    //从缓冲区中解析出JSON结构
    cJSON *json = cJSON_Parse(string);
    cJSON *node = NULL;
    //cJOSN_GetObjectItem 根据key来查找json节点 若果有返回非空
    node = cJSON_GetObjectItem(json,"family");
    if(node == NULL)
    {
        printf("family node == NULL\n");
    }
    else
    {
        printf("found family node\n");
    }
    node = cJSON_GetObjectItem(json,"famil");
    if(node == NULL)
    {
                printf("famil node == NULL\n");
        }
    else
        {
                printf("found famil node\n");
        }
    //判断是否有key是string的项 如果有返回1 否则返回0
    if(1 == cJSON_HasObjectItem(json,"family"))
    {
        printf("found family node\n");
    }
    else
    {
        printf("not found family node\n");
    }
    if(1 == cJSON_HasObjectItem(json,"famil"))
        {
                printf("found famil node\n");
        }
        else
        {
                printf("not found famil node\n");
        }
    
    node = cJSON_GetObjectItem(json,"family");
    if(node->type == cJSON_Array)
    {
        printf("array size is %d",cJSON_GetArraySize(node));
    }
    //非array类型的node 被当做array获取size的大小是未定义的行为 不要使用
    cJSON *tnode = NULL;
    int size = cJSON_GetArraySize(node);
    int i;
    for(i=0;i<size;i++)
    {
        tnode = cJSON_GetArrayItem(node,i);
        if(tnode->type == cJSON_String)
        {
            printf("value[%d]:%s\n",i,tnode->valuestring);
        }
        else
        {
            printf("node' type is not string\n");
        }
    }

    cJSON_ArrayForEach(tnode,node)
    {
        if(tnode->type == cJSON_String)
        {
            printf("int forEach: vale:%s\n",tnode->valuestring);    
        }
        else
        {
            printf("node's type is not string\n");
        }
    }
    return 0;
}