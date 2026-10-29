#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <time.h>
int main(int c,char**v){struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);pid_t p=fork();
 if(!p){execl("/bin/sh","sh","-c",v[1],(char*)0);_exit(127);}
 int st;struct rusage ru;wait4(p,&st,0,&ru);clock_gettime(CLOCK_MONOTONIC,&b);
 fprintf(stderr,"MEMRUN %.6f %.6f %ld %d\n",(b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)/1e9,ru.ru_utime.tv_sec+ru.ru_utime.tv_usec/1e6+ru.ru_stime.tv_sec+ru.ru_stime.tv_usec/1e6,ru.ru_maxrss,WEXITSTATUS(st));
 return WEXITSTATUS(st);}
