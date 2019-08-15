//Snd2 code: non-block v1
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
#include <errno.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>


#define UDPBUFLEN 1500
#define TCPBUFLEN 200

#define CLKPORT 54040 // to send ctrl message to snd2
#define RCVPORT 54045 //send ctrl mess to snd1
#define UDPPORT 54001


int rcv_tcpsock;
int sockfd, slen;
struct sockaddr_in serv_addr;
char *serverIP;
//char buffer[BufferLength];
int totalcnt = 0;
double rate, gap;
int num=5000;//default 500 packets
int pktsize=272;
double traffic=0.;

/***********/
void init_tcp(){
	int on = 1;
	struct sockaddr_in serveraddr;
	struct sockaddr_in their_addr;
	int tcpsock, rc;

	/* Get a socket descriptor */
	if((tcpsock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("Snd2-socket() error");
		exit (-1);
	}
	//else printf("Snd2-socket() is OK\n");

	if((rc = setsockopt(tcpsock, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on))) < 0){
		perror("Snd2-setsockopt() error");
		close(tcpsock);
		exit (-1);
	}
	//else printf("Snd2-setsockopt() is OK\n");

	memset(&serveraddr, 0x00, sizeof(struct sockaddr_in));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(CLKPORT);
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

    int reuse = 1;
    if (setsockopt(tcpsock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

#ifdef SO_REUSEPORT
    if (setsockopt(tcpsock, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse)) < 0) 
        perror("setsockopt(SO_REUSEPORT) failed");
#endif

	if((rc = bind(tcpsock, (struct sockaddr *)&serveraddr, sizeof(serveraddr))) < 0){
		perror("Snd2-bind() error");
		close(tcpsock);
		exit(-1);
	}
	else printf("Snd2-bind() is OK\n");
	/* Up to 10 clients can be queued */
	if((rc = listen(tcpsock, 2)) < 0){
		perror("Snd2-listen() error");
		close(tcpsock);
		exit (-1);
	}
	//else printf("Snd2-Ready for Rcv connection...\n");

	int sin_size = sizeof(struct sockaddr_in);
	if((rcv_tcpsock = accept(tcpsock, (struct sockaddr *)&their_addr, &sin_size)) < 0){
		perror("Snd2-accept() error");
		close(tcpsock);
		exit (-1);
	}
	//else printf("Snd2-accept() is OK\n");

	//printf("Snd2-new socket, tcpsock2 is OK...\n");
	//printf("Got connection from Rcv \n") ;//, inet_ntoa(their_addr.sin_addr));

}

/*******/
void init_udp(char *serverIP){

    slen=sizeof(serv_addr);

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
}

int main(int argc, char* argv[])
{
	int trainid=0, state=0, i=0;
	serverIP=argv[1];
	char temp;
	char buffer[TCPBUFLEN];
	init_tcp();
	init_udp(serverIP);
	struct timeval t0, t1, t2, t3, rcvstop_time;
	char buf[UDPBUFLEN];
	long time[100002];  // max 100000 packets
	double duration;
	gettimeofday(&t0, NULL);
	while(1){
		while(state==0){
			int n =0, rc;
			bzero(buffer,sizeof(buffer));
			printf("Waiitng for message from Rcv\n");
			n = read(rcv_tcpsock, buffer, 20);
			gettimeofday(&t3, NULL);
			if(n<1){
				//printf("%d Nothing to read from socket, idling\n", n);
				usleep(1000);
				continue; //return;
			}
			sscanf(buffer, "%lf ", &rate);
			printf("Received %d data from Rcv: %.2f at %ld\n", n, rate, t3.tv_sec*1000000+t3.tv_usec);
			if(rate >0){
				state =1;
				duration = (pktsize+28)*8/rate;
				printf("Change to state %d, rate=%.2f\n", state, rate);
				break;
			}
			/*if(rate ==0.00){
				state =0;
			}*/
			if((int)rate == -2){
				printf("Rcv is done estimating\n");
				close(sockfd);
				exit(0);
			}
		}


		if(state==1){
			//check if receving Stop message from Rcv
			int flags = fcntl(rcv_tcpsock, F_GETFL, 0);
			if (flags < 0) perror("Could not get tcpsock flags: \n");
			int err_setnonblock = fcntl(rcv_tcpsock, F_SETFL, flags | O_NONBLOCK);
			if (err_setnonblock < 0) perror("Could set tcpsock to be non blocking: \n");
			char buffer[TCPBUFLEN];
			bzero(buffer,sizeof(buffer));
			int n = read(rcv_tcpsock, buffer, 20);
			//printf("n=%d i=%d ", n, i);
			if(n<1){
				i++;
				gettimeofday(&t1,NULL);
				//printf("send pkt %d at %ld\n", i, t1.tv_sec*1000000+t1.tv_usec);
				time[i]=(t1.tv_sec-t0.tv_sec)*1000000+(t1.tv_usec-t0.tv_usec);
				sprintf(buf, "%d, %ld\n", -i, time[i]);
				//printf("%.2f snd time[%d]= %ld\n", duration, i, time[i]);
				if (sendto(sockfd, buf, pktsize, 0,   (struct sockaddr*)&serv_addr, slen)==-1)
					    err("sendto()");
				traffic+= (pktsize+28)/1000.; // in KB
				gettimeofday(&t2, NULL);
				while((t2.tv_sec-t1.tv_sec)*1000000+(t2.tv_usec-t1.tv_usec)<duration-1)
				    gettimeofday(&t2, NULL);

				//if(i==100000) i=0;
				if(i==10000) break;
				continue;
			}
			else{
				double s_rate;
				sscanf(buffer, "%lf ", &s_rate);
				gettimeofday(&rcvstop_time, NULL);
				printf("Received Stop message %d from Rcv at %ld, sent %d pkts %.4f KB: %.2f \n", n, rcvstop_time.tv_sec*1000000+rcvstop_time.tv_usec, i, traffic, s_rate);
				if((int)s_rate ==-1){
					state =0;
					rate = s_rate;
					i =0;
					printf("State change to %d\n", state);
				}
			}


			//			
			
		}
	}

	close(rcv_tcpsock);
	return 0;
}

