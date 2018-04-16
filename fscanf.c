#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

typedef struct data
{
    char name[32];
    int no;
} stu;

int main()
{
    FILE *r=fopen("a.txt","r");
    assert(r!=NULL);

    stu a[10];
    int i=0;
    while(fscanf(r,"%s%d",a[i].name,&a[i].no)!=EOF)
    {
        printf("%s\t%d",a[i].name,a[i].no);
        i++;
    }
    fclose(r);

    return 0;
}