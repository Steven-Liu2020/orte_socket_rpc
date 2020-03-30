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
#include "orte.h"
#define BUFFER_SIZE 128
#define MAX_CONNECT_NUM 5

#define SOCK_S "sock_server"
#define SOCK_C "sock_client"

#define err_log(errlog) do{perror(errlog);exit(EXIT_FAILURE);}while(0)

#define DOMAIN 5

int regfail=0;
static char i2s[64]={0};
static char i2r[64]={0};
static ORTEDomainAppEvents orteEvents;

void onRegFail(void *param);
int matchpara(char *recv);
void rpc(int domain);
void *publisherCreate(void *arg,char *topic);
void *subscriberCreate(void *arg,char *topic);
void sendCallBack(const ORTESendInfo *info,void *vinstance, void *sendCallBackParam);
void recvCallBack(const ORTERecvInfo *info,void *vinstance,void *recvCallBackParam);

int main(int argc,char *argv[])//Client Synchronous Blocking
{
        int fd,ret,recv_bytes,send_bytes;
        struct sockaddr_un ser_addr;
        char recv_buff[BUFFER_SIZE],send_buff[BUFFER_SIZE];
        memset(&ser_addr,0,sizeof(ser_addr));
        ser_addr.sun_family = AF_UNIX;
        strcpy(ser_addr.sun_path,SOCK_S);

        fd = socket(AF_UNIX,SOCK_STREAM,0);
        if (fd < 0)
                err_log("Client socket() failed");
        ret = connect(fd,(struct sockaddr *)&ser_addr,sizeof(ser_addr));
        if (ret < 0)
                err_log("Client connent() failed");
	printf("Client start\n");
        while (1){
                memset(send_buff,0,sizeof(send_buff));
                printf("Input('end' to exit): ");
                fgets(send_buff,BUFFER_SIZE,stdin);
                if (strncmp(send_buff,"end",3) == 0)
                        break;
                //strcpy(send_buff,"I am synchronous block");
                printf("Client send: %s",send_buff);
                send_bytes = send(fd,send_buff,BUFFER_SIZE,0);
                if (send_bytes < 0)
                        err_log("Client send() failed");
                memset(recv_buff,0,sizeof(recv_buff));
                recv_bytes = recv(fd,recv_buff,BUFFER_SIZE,0);
                if (recv_bytes < 0)
                        err_log("Client recv() failed");
                recv_buff[recv_bytes] = '\0';
                printf("Client recv: %s\n",recv_buff);
                if(!strncmp(recv_buff,"success",7))
                        rpc(matchpara(recv_buff));
        }
        close(fd);
        return 0;
}

void onRegFail(void *param) {
          printf("registration to a manager failed\n");
            regfail=1;
}
int matchpara(char *recv)
{
        int i,j,k=0,domain;
	char a[6]={0};
        for(i=0;recv[i]!=':' && i<strlen(recv);i++);
	i++;
        for(j=i;j<strlen(recv);j++){
		a[k++]=recv[j];
	}
        domain=atoi(a);
        return domain;
}
void rpc(int domain)
{
	ORTEDomain *d;
        ORTEPublication *p;
        ORTESubscription *s;
        char topic[5]={0};
        sprintf(topic,"%d",domain);
        memset(i2s,0,sizeof(i2s));
        ORTEInit();
	ORTEDomainInitEvents(&orteEvents);
        orteEvents.onRegFail=onRegFail;
        d=ORTEDomainAppCreate(DOMAIN,NULL,&orteEvents,ORTE_FALSE);
        p=publisherCreate(d,topic);
	while(1){
		regfail=0;
		printf("RPC request(eg myadd 1 1):");
        	fgets(i2s,64,stdin);
		//ORTESleepMs(100);
		ORTEPublicationSend(p);
		s=subscriberCreate(d,topic);
		while(!regfail) ORTESleepMs(200);
		ORTESubscriptionDestroy(s);
		if(!strncmp(i2s,"exit",4)){
			ORTEDomainAppDestroy(d);
			return;
		}
        }
        return;
}
void *publisherCreate(void *arg,char *topic)
{
        ORTEPublication *p;
        NtpTime pers,delay;
        ORTETypeRegisterAdd(arg,"question",NULL,NULL,NULL,sizeof(i2s));
        NTPTIME_BUILD(pers,3);
        NTPTIME_BUILD(delay,1);
        p=ORTEPublicationCreate(arg,topic,"question",i2s,&pers,1,NULL,NULL,NULL);
        if(p==NULL){
                printf("ORTEPubilcationCreate failed\n");
        }
        return p;
}
void *subscriberCreate(void *arg,char *topic)
{
        ORTESubscription *s;
        NtpTime deadline,miniSepara;
        ORTETypeRegisterAdd(arg,"answer",NULL,NULL,NULL,sizeof(i2r));
        NTPTIME_BUILD(deadline,5);
        NTPTIME_BUILD(miniSepara,0);
        s=ORTESubscriptionCreate(arg,IMMEDIATE,BEST_EFFORTS,topic,
                        "answer",i2r,&deadline,&miniSepara,recvCallBack,
                        NULL,IPADDRESS_INVALID);
        if(s==NULL){
                printf("ORTESubscriptionCreate failed\n");
        }
        return s;
}
void sendCallBack(const ORTESendInfo *info,void *vinstance, void *sendCallBackParam) {
	char *instance=(char*)vinstance;
	char *num;
	switch (info->status) {
		case NEED_DATA:
			instance="hello";
			printf("send issue,%s\n",instance);			
			break;
		case CQL:  //criticalQueueLevel
			break;
	}
}
void recvCallBack(const ORTERecvInfo *info,void *vinstance,void *recvCallBackParam){
        char *instance=(char*)vinstance;
        switch(info->status) {
                case NEW_DATA:
                        /*if(strncmp(instance,"unvalid",7))
                                printf("RPC respond: %s\n",instance);
			else
				printf("receive issue: %s\n",instance);*/
			if(!strlen(instance))
				strcat(instance,"unvalid");
			printf("RPC respond: %s\n",instance);
			regfail=1;
                        break;
                case DEADLINE:
                        //printf("deadline occurred\n");
                        break;
        }
}
