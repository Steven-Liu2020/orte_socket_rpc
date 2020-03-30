#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "pthread.h"
#include "unistd.h"
#include "sys/socket.h"
#include "sys/un.h"
#include "stddef.h"
#include "fcntl.h"
#include "sys/select.h"
#include "aio.h"
#include "errno.h"
#include "signal.h"
#include "sys/epoll.h"
#include "orte.h"

#define BUFFER_SIZE 128
#define MAX_CONNECT_NUM 10
#define FDSIZE 6000
#define EPOLLEVENTS 6000

#define SOCK_S "sock_server"
#define SOCK_C "sock_client"

#define err_log(errlog) do{perror(errlog);exit(EXIT_FAILURE);}while(0)
#define DOMAIN 5
#define MAX_APP 100
struct DomainInformation {
	//ORTEDomain *dm,*da;
        ORTEPublication *p;
        ORTESubscription *s;
} domainInfo[MAX_APP]; 

int regfail=0;
static char i2s[64]={0};
static char i2r[64]={0};
static ORTEDomain *dm,*d;
static ORTEDomainAppEvents orteEvents;

static void handle_events(int ep_fd,struct epoll_event *events,int n,
        int ser_fd,char *recv_buff,char *send_buff,unsigned int* counts);

void onRegFail(void *param);
int myadd(int a,int b);
void matchadd(char *instance);
void sendCallBack(const ORTESendInfo *info,void *vinstance, void *sendCallBackParam);
void recvCallBack(const ORTERecvInfo *info,void *vinstance,void *recvCallBackParam);
void *publisherCreate(void *arg,char *topic);
void *subscriberCreate(void *arg,char *topic);
void rpc(int cli_fd); 

int main(int agrc,char *argv[])
{
        int ser_fd,cli_fd,ret;
        struct sockaddr_un un;
        char recv_buff[BUFFER_SIZE],send_buff[BUFFER_SIZE];
        int reuse_val = 1;

        struct epoll_event events[EPOLLEVENTS],ev;
        int ep_fd,i;
        unsigned int conn_counts = 0;

        memset(&un, 0, sizeof(un));
        un.sun_family = AF_UNIX;
        unlink(SOCK_S);
        strcpy(un.sun_path,SOCK_S);

        ser_fd = socket(AF_UNIX,SOCK_STREAM,0);
        if (ser_fd < 0)
                err_log("Server socket() failed");
        //Set addr reuse
        setsockopt(ser_fd,SOL_SOCKET,SO_REUSEADDR,(void *)&reuse_val,sizeof(reuse_val));
        ret = bind(ser_fd,(struct sockaddr *)&un,sizeof(un));
        if (ret < 0)
                err_log("Server bind() failed");
        ret = listen(ser_fd,MAX_CONNECT_NUM);
        if (ret < 0)
                err_log("Srevet listen() failed");
        printf("Server start\n");
        
        ep_fd = epoll_create(FDSIZE);
        memset(events,0,EPOLLEVENTS);
        ev.events = EPOLLIN;
        ev.data.fd = ser_fd;
        epoll_ctl(ep_fd,EPOLL_CTL_ADD,ser_fd,&ev);
	//ORTE domain init
	ORTEInit();
	ORTEDomainInitEvents(&orteEvents);
        orteEvents.onRegFail=onRegFail;
        dm=ORTEDomainMgrCreate(DOMAIN,NULL,&orteEvents,ORTE_FALSE);
	d=ORTEDomainAppCreate(DOMAIN,NULL,&orteEvents,ORTE_FALSE);
        while (1){
                ret = epoll_wait(ep_fd,events,EPOLLEVENTS,-1);
                if (ret < 0)
                        err_log("Server epoll_wait() failed");
                handle_events(ep_fd,events,ret,ser_fd,recv_buff,
                                         send_buff,&conn_counts);
                printf("Current connect counts: [%d]\n",conn_counts);
        }
	ORTEDomainMgrDestroy(dm);
	ORTEDomainAppDestroy(d);
        close(ser_fd);
        close(ep_fd);
        return 0;
}

static void handle_events(int ep_fd,struct epoll_event *events,int n,
      int ser_fd,char *recv_buff,char *send_buff,unsigned int *counts)
{
        int i,cli_fd;
        int recv_bytes,send_bytes;
        struct epoll_event ev;
        for (i = 0; i < n; i++){
                if (events[i].data.fd == ser_fd){
                        cli_fd = accept(ser_fd,NULL,NULL);
                        if (cli_fd < 0)
                                continue;
                        ev.events = EPOLLIN;
                        ev.data.fd = cli_fd;
                        epoll_ctl(ep_fd,EPOLL_CTL_ADD,cli_fd,&ev);
                        printf("connect client: %d\n",cli_fd);
                        (*counts)++;
                }
                else{
                        memset(recv_buff,0,BUFFER_SIZE);
                        recv_bytes = recv(events[i].data.fd,recv_buff,
                                        BUFFER_SIZE,0);
                        if (recv_bytes < 0)
                                err_log("Server recv() failed");
                        else if (recv_bytes == 0){
                                epoll_ctl(ep_fd,EPOLL_CTL_DEL,
                                                events[i].data.fd,NULL);
                                close(events[i].data.fd);
                                printf("closed client: %d\n",
                                                events[i].data.fd);
                                (*counts)--;
                        }
                        else{
                                recv_buff[recv_bytes] = '\0';
                                printf("Server recv: %s",recv_buff);
				if(!strncmp(recv_buff,"domain",6)&&strlen(recv_buff)==7){
			                sprintf(send_buff,"success:%d",events[i].data.fd);
                                        send(events[i].data.fd,
                                         send_buff,strlen(send_buff),0);
					//printf("%d\n",events[i].data.fd);
                                        rpc(events[i].data.fd);
					continue;
				}
                                sprintf(send_buff,"Welcome client: %d",
                                                events[i].data.fd);
                                printf("Server send: %s\n",send_buff);
                                send_bytes = send(events[i].data.fd,
                                         send_buff,strlen(send_buff),0);
                                if (send_bytes < 0)
                                        err_log("Server send() failed");
                        }
                }
        }
}
//event system
void onRegFail(void *param) {
        printf("registration to a manager failed\n");
        regfail=1;
}
void rpc(int cli_fd)
{
        ORTEPublication *p;
        ORTESubscription *s;
        char topic[5]={0};
	if(cli_fd>=MAX_APP)
		return;
        itoa(cli_fd,topic);
        memset(i2s,0,sizeof(i2s));
	regfail=0;
	p=publisherCreate(d,topic);
        s=subscriberCreate(d,topic);
	domainInfo[cli_fd].p=p;
	domainInfo[cli_fd].s=s;
	//printf("%d,%x\n",cli_fd,p);
	//ORTESleepMs(1000);
	//ORTEPublicationSend(p);
	//ORTESleepMs(1000);
        return;
}
void *publisherCreate(void *arg,char *topic)
{
        ORTEPublication *p;
        NtpTime pers,delay;
        ORTETypeRegisterAdd(arg,"answer",NULL,NULL,NULL,sizeof(i2s));
        NTPTIME_BUILD(pers,3);
        NTPTIME_BUILD(delay,1);
        p=ORTEPublicationCreate(arg,topic,"answer",i2s,&pers,1,sendCallBack,NULL,&delay);
        if(p==NULL){
                printf("ORTEPubilcationCreate failed\n");
        }
        return p;
}
void *subscriberCreate(void *arg,char *topic)
{
        ORTESubscription *s;
        NtpTime deadline,miniSepara;
        ORTETypeRegisterAdd(arg,"question",NULL,NULL,NULL,sizeof(i2r));
        NTPTIME_BUILD(deadline,5);
        NTPTIME_BUILD(miniSepara,0);
        s=ORTESubscriptionCreate(arg,IMMEDIATE,BEST_EFFORTS,topic,
                        "question",i2r,&deadline,&miniSepara,recvCallBack,
                        NULL,IPADDRESS_INVALID);
        if(s==NULL){
                printf("ORTESubscriptionCreate failed\n");
        }
        return s;
}
void sendCallBack(const ORTESendInfo *info,void *vinstance, void *sendCallBackParam) {
	char *instance=(char*)vinstance;
	switch (info->status) {
		case NEED_DATA:
			if(!strlen(instance)){
				strcat(instance,"unvalid");
			}
			printf("send issue,%s\n",instance);		
			break;
		case CQL:  //criticalQueueLevel
			break;
	}
}
void recvCallBack(const ORTERecvInfo *info,void *vinstance,void *recvCallBackParam){
        char *instance=(char*)vinstance;
	//int n=atoi(info->topic);
        switch(info->status) {
                case NEW_DATA:
                        if(!strncmp(instance,"myadd",5)){
                                matchadd(instance);
				printf("receive issue: %s",instance);
			}else {
				memset(i2s,0,sizeof(i2s));
                                printf("receive issue: %s",instance);
			}
			//printf("%x\n",info->senderGUID);
			//regfail=1;
                        break;
                case DEADLINE:
                        printf("deadline occurred\n");
                        break;
        }
}
void matchadd(char *instance)
{
        char a[7]={0},b[7]={0};
        int i=5,k1=0,k2=0;
	int len=strlen(instance)-1,ans;
	char sec=0;
	memset(i2s,0,sizeof(i2s));
	while(i<len && instance[i]==' ') i++;
	while(i<len && instance[i]!=' ') {
		if(k1>5) return;
		a[k1++]=instance[i++];
	}
	while(i<len && instance[i]==' ') i++;
	while(i<len && instance[i]!=' ') {
		if(k2>5) return;
		b[k2++]=instance[i++];
	}
	//printf("%s,%s\n",a,b);
        ans=myadd(atoi(a),atoi(b));
        itoa(ans,i2s);
        return;
}
void itoa(int n,char *str)
{
	char tmp;
	int i=0,j,k;
	unsigned int unum;
	if(n<0){
		unum=-n;
		str[i++]='-';
	}
	else
		unum=n;
	if(unum>199998)
		return;
	do{
		str[i++]=(unum%10+'0');
		unum/=10;
	}while(unum);
	if(str[0]=='-')
		k=1;
	else
		k=0;
	for(j=k;j<=(i-1)/2;++j){
		tmp=str[j];
		str[j]=str[i-j+k-1];
		str[i-j+k-1]=tmp;
	}
	return;
}
int myadd(int a,int b)
{
	return a+b;
}
