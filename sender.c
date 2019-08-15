//Snd1 -> tcp server
//rcv ->tcp recv
#define _MULTI_THREADED
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/net_tstamp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include<pthread.h>
#include <signal.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include<sys/wait.h>

#define RCVPORT 54045 //send ctrl mess to snd1
#define UDPPORT 54001


#define UDPBUFLEN 1500
#define TCPBUFLEN 200

int rcv_tcpsock;

int num=200;//default 500 packets
double rate; // default 100 Mbps
double gap;
int pktsize=1472;
double traffic=0;
/***************/

char *serverIP;



int start_est();
void send_trains(char *serverIP, int number, int pktsize, double duration);

void err(char *s){
    perror(s);
    exit(1);
}
//
void init_tcp(){

	struct sockaddr_in serveraddr;
	int rc;
	struct hostent *hostp;
	//sprintf(buffer, "%lf", rate2);

	if((rcv_tcpsock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("RcvSnd-socket() error");
		exit(-1);
	}
	//else printf("RcvSnd2-socket() OK\n");

	memset(&serveraddr, 0x00, sizeof(struct sockaddr_in));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(RCVPORT);

	if((serveraddr.sin_addr.s_addr = inet_addr(serverIP)) == (unsigned long)INADDR_NONE){
		memcpy(&serveraddr.sin_addr, hostp->h_addr, sizeof(serveraddr.sin_addr));
	}

	if((rc = connect(rcv_tcpsock, (struct sockaddr *)&serveraddr, sizeof(serveraddr))) < 0){
		perror("RcvSnd-connect() error");
		close(rcv_tcpsock);
		exit(-1);
	}
	//else printf("RcvSnd2 Connection established...\n");
}
//

/**********/
int start_est(){
	int n, fleet=0;
	char buffer[TCPBUFLEN];
	double prev_rate=0;
	struct timeval rcvctrl;
	while(1){
		bzero(buffer,sizeof(buffer));
		gettimeofday(&rcvctrl, NULL);
		n = read(rcv_tcpsock, buffer, sizeof(buffer));
		if(n<1){
			printf("ERROR reading from socket");
			return;
		}

		sscanf(buffer, "%lf %d", &rate, &num);
		printf("Received %d data from Rcv: %.2f %d at %ld\n", n, rate, num, rcvctrl.tv_sec*1000000+rcvctrl.tv_usec);
/*		n = write(rcv_tcpsock, buffer, n);
		if(n<0) error("ERROR writing to socket");
*/		if(((int)rate)!= ((int) prev_rate)){
			fleet++;
			printf("Sending fleet############\n");
		}
		prev_rate = rate;
		if(num==0 || rate==0){

			printf("Rcv asked to stop\n");
			return;
		}
		else{
			gap = (pktsize+28)*8/rate;
			printf("send %d pkts of %d to %s at rate %.2f Mbps (interval %.1f us) \n", num, pktsize, serverIP, rate, gap);
			//usleep(8000);
			send_trains(serverIP,  num, pktsize, gap); //sending process
			traffic+=(pktsize+28)*num/1000; //KBytes
			printf("Sent %.2f KB to rcv\n", traffic);
		}
	}

}
/**********/

/**/
void send_trains(char *serverIP, int number, int pktsize, double duration)
{
    struct sockaddr_in serv_addr;
    int sockfd, slen=sizeof(serv_addr);
    int i;
    char buf[UDPBUFLEN];
    struct timeval t0, t1, t2, t3;
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
        err("socket");
    int timestamp_flags=SOF_TIMESTAMPING_TX_SOFTWARE;
    if(setsockopt(sockfd, SOL_SOCKET,SO_TIMESTAMPING,&timestamp_flags,sizeof(timestamp_flags))<0) {
        err("timestamp error");
    }
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(UDPPORT);
    if (inet_aton(serverIP, &serv_addr.sin_addr)==0){
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }
    long time[100002];  // max 100000 packets
    if (number >= 100002){
        number = 100000;
        printf("increase time array size!\n");
    }
    gettimeofday(&t0, NULL);
    for( i=1;i<=number;i++)
    {
        gettimeofday(&t1,NULL);
        //sprintf(buf, "%d, %ld, %ld\n", i, t1.tv_sec, t1.tv_usec);
        time[i]=(t1.tv_sec-t0.tv_sec)*1000000+(t1.tv_usec-t0.tv_usec);
	sprintf(buf, "%d, %ld\n", i, time[i]);
	//printf("snd time[%d]= %ld\n", i, t1.tv_sec*1000000+t1.tv_usec);
        if (sendto(sockfd, buf, pktsize, 0,   (struct sockaddr*)&serv_addr, slen)==-1)
                    err("sendto()");
        gettimeofday(&t2, NULL);
        while((t2.tv_sec-t1.tv_sec)*1000000+(t2.tv_usec-t1.tv_usec)<duration-1)
            gettimeofday(&t2, NULL);

    }
    gettimeofday(&t3, NULL);
    long totduration = (t3.tv_sec-t0.tv_sec)*1000000+t3.tv_usec-t0.tv_usec;
    printf("takes time %ld us at rate %.2f Mbps\n", totduration, number*(pktsize+28)*8.0/totduration);
    //sleep(2);
 /*for(i=1; i<2; i++){
    if (sendto(sockfd, buf, 100, 0,   (struct sockaddr*)&serv_addr, slen)==-1)
                        err("sendto()");
 }*/

   close(sockfd);
   long sndtimestamp[number]; sndtimestamp[1] = 0;
   //for (i=2;i<=number;i++){
     //  if ((time[i]-time[i-1])>2*duration)
           // printf("sending interval: %ld us\n", time[i]-time[i-1]);
	//sndtimestamp[i] = sndtimestamp[i-1] + time[i] - time[i-1];
        //printf("sndtimestamp [%d] = %ld us\n", i, sndtimestamp[i] );
  // }

}

int main(int argc, char* argv[])
{
    serverIP=argv[1];
	init_tcp();
	start_est();
	close(rcv_tcpsock);
    return 0;
}
