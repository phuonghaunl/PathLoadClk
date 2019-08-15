
// Change train size to 100 when disable both clock and bass to check!
//Fleet, non-block
#include<stdlib.h>
#include <stdio.h>
#include<sys/socket.h>
#include<netinet/ip.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/net_tstamp.h>
#include<netinet/ip_icmp.h>
#include<net/ethernet.h>
#include<arpa/inet.h>
#include<string.h>
#include <math.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>

#define UDPBUFLEN 1500
#define TCPBUFLEN 200

#define CLKPORT 54040 // to send ctrl message to snd2
#define RCVPORT 54045 //send ctrl mess to snd1
#define UDPPORT 54001


#define N1 1 // number of received packets before sending ctrl mess to snd1


#define NUM_1 200

#define LOSSTHRES 0.5
#define SNDPKTSIZE 1500
#define CLKPKTSIZE 300

#define NUM_PKT 100000

#define FLEETSIZE 5


int snd_tcpsock, clk_tcpsock, udpsock;

char * CLKIP, * SNDIP;

int clkenable, bassenable;
struct timeval lastsleeptime;

double rate2=1.0, r2max=0.0, r2_thres=0.01; // r2_thres is for controlling overhead
int clkstate;
int vm_scheduling_detected;

double gaptrend_, gapthres=0.1;
double pct_trend_, act_pct_trend_, adr=0., pct_thres=0.55;
double act_pct_trend; //using median
int count=0;

int wait_thres=2000;
double num2_scale;


//int sockfd;
//int tcpsock, tcpsock2, rc, tcplength, totalcnt=0;
//int tcpsock_, rc_, tcplength_, totalcnt_=0; //for snd1
char sndbuffer[TCPBUFLEN], clkbuffer[TCPBUFLEN];
//socklen_t slen;
//int pktsize1, pktsize2=CLKPKTSIZE, pktsize1_checkr2 =CLKPKTSIZE;
int num2, num1;
double rate1 =1.0, r1min=1.0, r1max=0.0;
double g1min=0, g1max=0, w=10, x=40;
int trainid=0, fleetid=0, trend=0, grey=0; //
double fleet_gaptrend, fleet_act_pct;
double fleet_thres=0.6;






int converged_rmn_rmx=0, converged_gmn_rmn_tm=0, converged_rmn_rmx_tm=0, converged_gmx_rmx_tm=0, converged_gmn_rmn=0, converged_gmx_rmx=0;

#ifndef max_
	#define max_( a, b ) ( ((a) > (b)) ? (a) : (b) )
#endif
#ifndef min_
	#define min_( a, b ) ( ((a) < (b)) ? (a) : (b) )
#endif

long sndus[100000], act_rcvtime[100000], sndus1[100000];
long act_rcvgap[100000], act_sndgap[100000];
long newact_rcvgap[100000], newact_sndgap[100000];
int spikeup=100, spikedown=10;


void send_tcpSnd2();
void send_ctrl_messtoSnd();


void compute_num2(double scale){
	double x = (num1*5.0*rate2*scale/rate1)+N1;
	num2 = (int) x;
	if( num2 > 100000){
		printf("Need more than 100000 clock packets\n");
		exit(0);
	}
}
void err(char *s){
    perror(s);
    exit(1);
}

double PCT(double array[], int start, int end){
	int i, improvement =0;
	double total;
	for (i=start; i<end-1; i++){
		if ( array[i] < array[i+1] )
		        improvement += 1 ;
	}
	total = ( end - start ) ;
	return ( (double)improvement/total ) ;
}
double PDT(double array[], int start, int end){
	double y = 0 , y_abs = 0 ;
	int i ;
	for ( i = start+1 ; i < end    ; i++ )
	    {
	      y += array[i] - array[i-1] ;
	      y_abs += fabs(array[i] - array[i-1]) ;
	    }
	return y/y_abs ;
}
double median(int n, long x[]){
	long temp;
	int i, j;
	for(i=0; i<n-1; i++){
		for(j=i+1; j<n; j++){
			if(x[j]<x[i]){
				temp = x[i];
				x[i] = x[j];
				x[j] = temp;
			}
		}
	}
	if(n%2==0){
		return ((x[n/2]+x[n/2 - 1])/2.0);
	}
	else{
		return (x[n/2]);
	}
}
/********/

//Listen
void listen_sndtcp(){
	int on = 1, n;
	struct sockaddr_in serveraddr;
	struct sockaddr_in their_addr;
	
	int tcpsock;

	/* Get a socket descriptor */
	if((tcpsock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("Rcv-socket() error");
		exit (-1);
	}
	else printf("Rcv-socket() is OK\n");

	if((n = setsockopt(tcpsock, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on))) < 0){
		perror("Crv-setsockopt() error");
		close(tcpsock);
		exit (-1);
	}
	else printf("Rcv-setsockopt() is OK\n");

	memset(&serveraddr, 0x00, sizeof(struct sockaddr_in));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(RCVPORT);
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

    int reuse = 1;
    if (setsockopt(tcpsock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

#ifdef SO_REUSEPORT
    if (setsockopt(tcpsock, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse)) < 0) 
        perror("setsockopt(SO_REUSEPORT) failed");
#endif

	if((n = bind(tcpsock, (struct sockaddr *)&serveraddr, sizeof(serveraddr))) < 0){
		perror("Rcv-bind() error");
		close(tcpsock);
		exit(-1);
	}
	//else printf("Snd1-bind() is OK\n");
	/* Up to 10 clients can be queued */
	if((n = listen(tcpsock, 2)) < 0){
		perror("Rcv-listen() error");
		close(tcpsock);
		exit (-1);
	}
	//else printf("Snd1-Ready for Rcv connection...\n");

	int sin_size = sizeof(struct sockaddr_in);

	snd_tcpsock = accept(tcpsock, (struct sockaddr *)&their_addr, &sin_size);
	if(snd_tcpsock<0){
		perror("Rcv-accept() error");
		close(tcpsock);
		exit (-1);
	}
	close(tcpsock);
}
//

/********/
//call bass() to avg out spikes
void bass(int begin, int end){
    long rcvsum=0, sndsum=0;
    int act_j, act_t;
    if(begin<end){
	//printf("begin=%d end=%d\n", begin, end);
        for(act_j=begin;act_j<=end;act_j++){
		rcvsum+= act_rcvgap[act_j];
		sndsum+= act_sndgap[act_j];
	}
        for(act_t=begin; act_t<=end; act_t++){
        	newact_rcvgap[act_t] = round( (double) rcvsum/(end-begin+1));
        	newact_sndgap[act_t] = round( (double) sndsum/(end-begin+1));
		//printf("newrcvgap=%ld ", newact_rcvgap[act_t]);
        }
    }
}
void spike_detect(){
	int begin, end;
	int act_state =0, spikemax, act_i;
	if(act_rcvgap[0]>act_rcvgap[1]+(spikedown)){
		begin = 0;
		spikemax = act_rcvgap[0];
        	act_state = 1;
        	act_i=1;
    }
    else act_i=0;
    for(;act_i<count;act_i++){
        switch (act_state){
            case 0:
                if(act_rcvgap[act_i]+spikeup<act_rcvgap[act_i+1]){
                    end=act_i;
                    bass(begin,end);
                    act_state=-1;
                    begin=act_i+1;
                    spikemax=act_rcvgap[begin];
                }
                break;
            case -1:
                spikemax=max_(spikemax,act_rcvgap[act_i]);
                if(act_rcvgap[act_i]+(spikedown)<spikemax){
                    act_state=1;
                }
                else
                	break;
            case 1:
                if(act_rcvgap[act_i]+spikeup<act_rcvgap[act_i+1]){
                    end=act_i;
                    act_state=-1;
                    spikemax=act_rcvgap[act_i+1];
                }
                else{
                    if(act_rcvgap[act_i]==act_rcvgap[count-2]){
                        end=act_i;
                    }
                    break;
                }
                bass(begin,end);
                begin=act_i+1;
        }

    }
    end = count-2;
    bass(begin,end);
    //printf("begin=%d, end=%d\n", begin, end);
}
//
/**********/

void send_tcpSnd1(){
	//prepare to recv train
	int i,j=0;

	char udpbuf[UDPBUFLEN];
	struct sockaddr_in cli_addr, my_addr;
	int sockfd;

	socklen_t slen;

	slen=sizeof(cli_addr);
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
	      err("socket");
    bzero(&my_addr, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(UDPPORT);
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

#ifdef SO_REUSEPORT
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse)) < 0)
        perror("setsockopt(SO_REUSEPORT) failed");
#endif

    if (bind(sockfd, (struct sockaddr* ) &my_addr, sizeof(my_addr))==-1)
          err("bind");

    struct timeval timeout, udp_wait1, udp_wait2;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    if (setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                sizeof(timeout)) < 0)
        error("setsockopt failed\n");



    struct timeval microsec [NUM_PKT];
    int id[NUM_PKT], id_pos[NUM_PKT];

    int negpos[NUM_PKT], all_last_seen_snd_pktid[NUM_PKT];

    int pktnum=0, jj=0;

    bzero(negpos,sizeof(negpos));
    bzero(id,sizeof(id));
    bzero(sndus,sizeof(sndus));
    bzero(sndus1,sizeof(sndus1));
    bzero(microsec,sizeof(microsec));
    count=0;


	//gettimeofday(&wt1, NULL);
	int slot_size[NUM_PKT], slotid=0, totpkt_slot=0, slot_pos[NUM_PKT], sid=0, positive_pos[NUM_PKT];
	int endoftrain=0;
//	if(clkstate==1){
		send_tcpSnd2();
//	}
//	else{
//		send_ctrl_messtoSnd();
//	}
	int last_seen_snd_pktid=0;

	while(1) {


		i=recvfrom(sockfd, udpbuf, UDPBUFLEN, 0, (struct sockaddr*)&cli_addr, &slen);
		if(i<1){
			printf("sleeping 1us i=%d\n", i);
			usleep(1);
			continue;
		}
		
	      //  if(j<3) printf("%d\n", i);
		gettimeofday(&microsec[pktnum], NULL);
		sscanf(udpbuf, "%d, %ld\n", &id[pktnum], &sndus[pktnum]);
		//printf("%d id=%d sndtime=%ld actrcvtime=%ld\n", pktnum, id[pktnum], sndus[pktnum], microsec[pktnum].tv_sec*1000000 + microsec[pktnum].tv_usec);
		//printf("sndtime %d = %ld\n", id[pktnum], sndus[pktnum]);

		if(id[pktnum]<0){

			if(pktnum <N1 && abs(id[pktnum]) > N1){
				continue;
			}
			negpos[jj] = pktnum;
			all_last_seen_snd_pktid[pktnum] = last_seen_snd_pktid;
			jj++;

			//printf("sndtime %d = %ld\n", id[pktnum], sndus[pktnum]);
			/*if(abs(id[pktnum]) >= num2 -1){
				pktnum++;
				printf("Received end from CLK\n");
				sleep(2);
				break;
			}*/


		}
		else if (id[pktnum]>0){
			if(pktnum < N1){
				continue;
			}
			last_seen_snd_pktid =  id[pktnum];
			positive_pos[count] = pktnum;
			sndus1[count]= sndus[pktnum];
			id_pos[count] = id[pktnum];
			act_rcvtime[count] = microsec[pktnum].tv_sec*1000000 + microsec[pktnum].tv_usec ;
			if(count>0){
				act_rcvgap[count] = act_rcvtime[count]-act_rcvtime[count-1];
				act_sndgap[count] = sndus1[count]-sndus1[count-1];
				//act_rcvgap[count] = act_sndgap[count];
			}
			//printf("%d ", count);
			gettimeofday(&udp_wait1, NULL);
			count++;
		}
	/*	for(k=2; k<count; k++){
			if(act_sndgap[k] > 5* act_sndgap[k-1]){
				printf("Context Switch at Sender\n");
				exit(0);
			}
		}
	*/
		//printf("count=%d ", count);
		
		if((count>= (num1 - 1)) ){
			printf("Received all %d pkts from SND, send Stop message to Clk\n", count);
			int rc;
			char temp;
			bzero(clkbuffer, sizeof(clkbuffer));
			sprintf(clkbuffer, "%.2f ", -1.0);
			rc = write(clk_tcpsock, clkbuffer, 20);
			if(rc < 0){
				int tcplength;
				perror("Client-write() error");
				rc = getsockopt(clk_tcpsock, SOL_SOCKET, SO_ERROR, &temp, &tcplength);
				if(rc == 0){
					errno = temp;
					perror("SO_ERROR was");
				}
				close(clk_tcpsock);
				exit(-1);
			}
			break;
		}
		else if(count > 10){
			gettimeofday(&udp_wait2, NULL);
			long diff_wait = (udp_wait2.tv_sec - udp_wait1.tv_sec)*1000000 + (udp_wait2.tv_usec - udp_wait1.tv_usec);
			if(diff_wait > 500000){
				// Send Stop message to Clk
				int rc;
				char temp;
				bzero(clkbuffer, sizeof(clkbuffer));
				sprintf(clkbuffer, "%.2f ", -1.0);
				rc = write(clk_tcpsock, clkbuffer, 20);
				if(rc < 0){
					int tcplength;
					perror("Client-write() error");
					rc = getsockopt(clk_tcpsock, SOL_SOCKET, SO_ERROR, &temp, &tcplength);
					if(rc == 0){
						errno = temp;
						perror("SO_ERROR was");
					}
					close(clk_tcpsock);
					exit(-1);
				}
				break;
			}
		}

		pktnum++;

		if(pktnum == N1){
			printf("send ctrl mess to server %s \n", SNDIP);
			send_ctrl_messtoSnd();
		}
		if(wait_thres >0) {

			struct timeval tmp1;
			double wt1, wt2;
			gettimeofday(&tmp1,NULL) ;
			wt1 = (double) tmp1.tv_sec * 1000000.0 +(double)tmp1.tv_usec ;
			wt2 = (double) lastsleeptime.tv_sec * 1000000.0 +(double) lastsleeptime.tv_usec ;
			if(wt1-wt2 > 30000){
				printf("Start simulating vm scheduling \n");
				usleep(wait_thres);
				printf("Done vm scheduling\n");
				gettimeofday(&lastsleeptime, NULL);

			}
		
		}

       }

	printf("count=%d jj=%d\n", count, jj);
	 double rel_rcvtime[count], actrcv_timestamp[count], actsnd_timestamp[count];

	 long tmp1, tmp2;

	 double act_rel_owd[count], rel_owd[count], rel_recvgap[count];
	 int posnum =0, pp=0;
	 int pospkts, pos_k1, pos_k2, count_;

	double errper_simple[count], errper_next[count];
	double errtot_simple = 0.0, errtot_next = 0.0;
	double avgerr_simple=0., agverr_next=0., avgerr_simple2=0.;
	double tot_rel=0, tot_act=0, errtot_simple2;

	int k;

	//while(count_<= count){
	if(clkstate==1){
		 for(k=0; k<jj-1; k++){
			 posnum = negpos[k+1] - negpos[k] -1;
			 pospkts = posnum;
			 pos_k1 = negpos[k+1];
			 pos_k2 = negpos[k];
			 tmp1 = sndus[pos_k1];
			 tmp2 = sndus[pos_k2];
			 //printf("id=%d sndus=%ld	", id[k], sndus[k]);
			 if(pospkts > 0){
					slot_size[slotid] = pospkts;
					slot_pos[slotid] = negpos[k];
					totpkt_slot+= slot_size[slotid];
					slot_pos[slotid+1] = negpos[k+1];
					//printf("slotid=%d slot_pos=%d ptks=%d\n", slotid, slot_pos[slotid], slot_size[slotid]);
					slotid++;
			 }
		 }
		/*if(count > totpkt_slot){
			printf("Reduce count=%d to totpktslot=%d\n", count, totpkt_slot);
			count = totpkt_slot;

		}*/
		 for(k=0; k<count; k++){
			for(sid=0; sid<slotid; sid++){
				if(positive_pos[k] > slot_pos[sid] && positive_pos[k] < slot_pos[sid+1]){
					break;
				}
			}
			if(sid==slotid){
				//printf("Please increase the number of clock packets\n");
				tmp2 = sndus[slot_pos[sid-1]];
				tmp1 = sndus[slot_pos[sid-1]];
				rel_rcvtime[k] = tmp2;
				//printf("%d sid=%d positive_pos=%d tmp2=%ld tmp1=%ld relrcvtime=%.2f\n", k, sid, positive_pos[k], tmp2, tmp1, rel_rcvtime[k]);
				//exit(0);
			}
			else{
				tmp2 = sndus[slot_pos[sid]];
				tmp1 = sndus[slot_pos[sid+1]];
				int act_slotsize = all_last_seen_snd_pktid[slot_pos[sid+1]] - all_last_seen_snd_pktid[slot_pos[sid]];
				rel_rcvtime[k] = tmp2 + (positive_pos[k] - slot_pos[sid])*(tmp1 - tmp2)/(double)act_slotsize;
				//printf("%d sid=%d positive_pos=%d tmp2=%ld tmp1=%ld relrcvtime=%.2f actual_slotsize=%d slotsize=%d\n", k, sid, positive_pos[k], tmp2, tmp1, rel_rcvtime[k], act_slotsize , slot_size[sid]);
			}

		 }


		 if(slotid<3){
			printf("******************************* Number of slots is less than 3\n");
		 }
	/*	 if(count < 300*0.99){
			printf("Too many packets lost of the train from sender\n");
			return;
		 }
	*/
		double rel_rcvgap[count], rel_actsndgap[count];


		for(k=1; k<count; k++){
				rel_rcvgap[k] = (double) (rel_rcvtime[k] - rel_rcvtime[k-1]);
		}

		/*Compute avg error in timestamps*/
		//printf("error percentage, recv_gap\n");
		int gapcount=0;
		double rel_act_trend=0.;
		for(k=1; (k<count) && (slotid>2); k++){
			if(k <slot_size[0])
				continue;
			if(k > totpkt_slot-slot_size[slotid-1])
				continue;
			if(rel_rcvgap[k] > act_sndgap[k]){
				rel_act_trend++;
			}
			tot_rel = tot_rel+rel_rcvgap[k];
			tot_act = tot_act+act_sndgap[k];

			errper_simple[k] = rel_rcvgap[k]/((double)act_sndgap[k]) -1;

			errtot_simple = errtot_simple + errper_simple[k];
			errtot_simple2 = errtot_simple2+ fabs( errper_simple[k]);
			gapcount++;
			//printf("%d relrcvgap=%.1f actrcvgap=%ld actsndgap=%ld err=%.4f err_tot=%.4f\n", k, rel_rcvgap[k], act_rcvgap[k], act_sndgap[k], errper_simple[k], errtot_simple);
		}
		if(gapcount>0){
			avgerr_simple = errtot_simple/gapcount; //(count -1-10);
			avgerr_simple2 = errtot_simple2/gapcount;
			rel_act_trend = rel_act_trend/gapcount;
		}
		else{
			avgerr_simple =0.0;
			avgerr_simple2 =0.0;
			rel_act_trend=0.0;
			clkstate =-1;
		}
		//printf("%d avgerrtot_simple2=%.4f avgerr_simple=%.4f rel_snd_trend(our pct after removing first and last blocks)=%.4f tot_rel=%.4f tot_act=%.4f\n", count-1, avgerr_simple2, avgerr_simple, rel_act_trend, tot_rel, tot_act);
		gaptrend_ = fabs(avgerr_simple);
	}
/////
	printf("############################## Done processing clk pkts ########################\n");
	int vm_count=0, pkt_reordered=0;
	for(k=1; k<count; k++){
		if(act_rcvgap[k] <5){
			vm_count++;
		}
		if(id_pos[k-1] > id_pos[k]){
			pkt_reordered++;
		}
	}
	if(vm_count>count/4){
		vm_scheduling_detected++;
		printf("************************** VM sechduling detected %d , vm_count=%d ***\n", vm_scheduling_detected, vm_count);
	}

	if(pkt_reordered){
		printf("************************** Packet reordering ****\n");
	}

		/*Smooth out gaps using BASS*/
	if(bassenable==1){
		spike_detect();
	}
		/*********/
		double rel_rcvgap[count], rel_actsndgap[count];//to compute error and gaptrend
		long act_relrcvtime[count];
			actrcv_timestamp[0] = 0;
			actsnd_timestamp[0] = 0;
			act_relrcvtime[0] = 0;
		 for(k=1; k<count; k++){
			//below is new actual timestamp with BASS
			actrcv_timestamp[k] = actrcv_timestamp[k-1]+newact_rcvgap[k]; //act_rcvtime[k] - act_rcvtime[firstalpha];
			actsnd_timestamp[k] = actsnd_timestamp[k-1]+newact_sndgap[k];
			act_relrcvtime[k] = act_relrcvtime[k-1] + act_rcvgap[k];
			if(bassenable==1){
				act_rel_owd[k] = actrcv_timestamp[k] - actsnd_timestamp[k];
			}
			else{
				
				act_rel_owd[k] = act_relrcvtime[k] - sndus1[k];
				//printf("%d act_rel_owd=%.4f, act_relrcvtime=%ld, act_sndtime=%ld\n", k, act_rel_owd[k], act_relrcvtime[k], sndus1[k]);
			}
			rel_owd[k] = rel_rcvtime[k]-sndus1[k]; // relowd in microsecond

	       }	
		int firstalpha=5;
		/*Pairwise Comparison Test, Pairwise Difference Test*/
		int G = 10;//sqrt(k-1-firstalpha); //alpha;
		if(count <10) G = 1;
		double act_owd_median[G];
		int order_med=0, pct_i=firstalpha+1;
		int pkts_pct =  (count-1-firstalpha)/G; //(k-1-firstalpha)/G; //(count-1)/G;
		//printf("pkts_pct=%d\n", pkts_pct);
		long act_group_pct[pkts_pct];;
		
		while (pct_i<count-1){
			for(k=0; k<pkts_pct; k++){
					act_group_pct[k] = act_rel_owd[pct_i];
					pct_i++;
					//printf("%d group_pct=%ld\n", k, group_pct[k]);
			}
			act_owd_median[order_med] = median(pkts_pct, act_group_pct);
			//printf("%d owd_median =%.2f, pct_i=%d \n", order_med, owd_median[order_med], pct_i);
			order_med++;
		}
		act_pct_trend = PCT(act_owd_median, 0, order_med);
		printf("Using median: med_act_PCT_Trend = %.2lf\n", act_pct_trend);


		//Not use median
		double pct_=0, act_pct_=0, dec_pct_=0, dec_act_pct_=0, equal=0;

		for(k=1; k<count; k++){
			if(rel_owd[k] > rel_owd[k-1]){
				pct_++;
				//printf("%d %.2f\n", k, pct_);
			}
			else if(rel_owd[k] < rel_owd[k-1]){
				dec_pct_++;
			}
			else equal++;
			
		}

		for(k=1; k<count; k++){
			if(act_rel_owd[k] > act_rel_owd[k-1]){
				act_pct_++;
			}
		}

		pct_trend_ = pct_/(count-1);
		act_pct_trend_ = act_pct_/(count-1);
		//printf("pct(our pct)=%.2f, act_pct_trend=%.2f, gaptrend=%.2f\n", pct_trend_, act_pct_trend_, gaptrend_);
		printf("act_pct_=%.4f, act_pct_trend=%.2f\n", act_pct_, act_pct_trend_);
		if(count < LOSSTHRES*num1){
			pct_trend_ =1;
			act_pct_trend_=1;
			gaptrend_=1;
		}
		printf("pct=%.2f, act_pct_trend=%.2f, gaptrend=%.2f\n", pct_trend_, act_pct_trend_, gaptrend_);

		
	close(sockfd);
}

/*********/
void init_clktcp(){

	struct sockaddr_in serveraddr;
	int rc;
	struct hostent *hostp;
	//sprintf(buffer, "%lf", rate2);

	if((clk_tcpsock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("RcvClk-socket() error");
		exit(-1);
	}
	//else printf("RcvSnd2-socket() OK\n");

	memset(&serveraddr, 0x00, sizeof(struct sockaddr_in));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(CLKPORT);

	if((serveraddr.sin_addr.s_addr = inet_addr(CLKIP)) == (unsigned long)INADDR_NONE){
		memcpy(&serveraddr.sin_addr, hostp->h_addr, sizeof(serveraddr.sin_addr));
	}

	if((rc = connect(clk_tcpsock, (struct sockaddr *)&serveraddr, sizeof(serveraddr))) < 0){
		perror("RcvClk-connect() error");
		close(clk_tcpsock);
		exit(-1);
	}
	//else printf("RcvSnd2 Connection established...\n");
}
void send_tcpSnd2(){
	if(clkstate==-1){
		rate2 =0.02;
	}
	struct timeval ctrl_time;
	gettimeofday(&ctrl_time, NULL);
	printf("Sending rate2=%.2f to Snd2 %s at %ld ...\n", rate2, CLKIP, ctrl_time.tv_sec*1000000+ctrl_time.tv_usec);
	int rc;
	char temp;

	bzero(clkbuffer, sizeof(clkbuffer));
	sprintf(clkbuffer, "%.2f ", rate2);
	rc = write(clk_tcpsock, clkbuffer, 20);

	if(rc < 0){
		int tcplength;
		perror("Client-write() error");
		rc = getsockopt(clk_tcpsock, SOL_SOCKET, SO_ERROR, &temp, &tcplength);
		if(rc == 0){
			errno = temp;
			perror("SO_ERROR was");
		}
		close(clk_tcpsock);
		exit(-1);
	}
/*
	rc = read(clk_tcpsock, &clkbuffer, sizeof(clkbuffer));
	if(rc < 0){
		printf("%d", rc);
		perror("Client-read() error");
		close(clk_tcpsock);
		exit(-1);
	}
	//printf("RcvSnd2-read() is OK\n");
	double r; int numpkt2;
	sscanf(clkbuffer, "%lf %d", &r, &numpkt2);
	if(numpkt2==num2) printf("Echoed data from Snd2: r=%.2f=rate2= %.2f, numpkt2=num2=%d\n", r, rate2, num2);
*/
}
void send_ctrl_messtoSnd(){
	//printf("Sending num2, rate2 to Snd2 %s...\n", snd1IP);
	int rc;
	char temp;
	struct timeval ctrl_time;
	bzero(sndbuffer, sizeof(sndbuffer));
	sprintf(sndbuffer, "%.2f %d", rate1, num1);
	gettimeofday(&ctrl_time, NULL);
	rc = write(snd_tcpsock, sndbuffer, strlen(sndbuffer)+1);
	printf("Send message to Snd: rate1= %.2f, num1=%d at %ld\n", rate1, num1, ctrl_time.tv_sec*1000000+ctrl_time.tv_usec);
	if(rc < 0){
		int tcplength;
		perror("Snd-write() error");
		rc = getsockopt(snd_tcpsock, SOL_SOCKET, SO_ERROR, &temp, &tcplength);
		if(rc == 0){
			errno = temp;
			perror("SO_ERROR was");
		}
		close(snd_tcpsock);
		exit(-1);
	}
/*	rc = read(snd_tcpsock, &sndbuffer, sizeof(sndbuffer));
	if(rc <= 0){
		perror("Snd-read() error");
		close(snd_tcpsock);
		exit(-1);
	}
	//printf("RcvSnd2-read() is OK\n");
	double r; int numpkt1;
	sscanf(sndbuffer, "%lf %d", &r, &numpkt1);
	if(numpkt1==num1) printf("Echoed data from Snd: r=%.2f=rate1= %.2f, numpkt1=num1=%d\n", r, rate1, num1);
*/
}
/*********/
/****Adjust rate1***/
int converged(){
	int ret_val=0;
	if ( (converged_gmx_rmx_tm && converged_gmn_rmn_tm) || converged_rmn_rmx_tm  )
		ret_val=1;
	else if (r1max != 0 && r1max != r1min){
		if ( r1max - r1min <= w ){
			converged_rmn_rmx=1;
			ret_val=1;
		}
		else if( r1max - g1max <= x && g1min - r1min <= x){

		converged_gmn_rmn = 1;
		converged_gmx_rmx = 1;
		ret_val=1;
		}
	}



  return ret_val ;
}


void radj_notrend();
void radj_increasing(){
	if ( g1max != 0 && g1max >= r1min ){
		if ( r1max - g1max <= x ){
			converged_gmx_rmx = 1;
			if ( g1min || r1min ){
				radj_notrend() ;
				printf("Call rate_inc but actually exe rate_notrend");
			}
			else{
				if ( g1min < g1max )
					rate1 = g1min/2. ;
				else
					rate1 = g1max/2. ;
			}
		}


		else 
			rate1 = ( r1max + g1max)/2. ;
	}
	else
		rate1 =  (r1max + r1min)/2.<1.?1.:(r1min+r1max)/2. ;
}
void radj_notrend(){
    rate1 =  2*rate1>adr?adr:2*rate1 ;
	if ( g1min != 0 && g1min <= r1max ){
		if ( g1min - r1min <= x ){
			converged_gmn_rmn = 1;
			radj_increasing() ;
			printf("Call rate_notrend but actaully exe rate_inc");
		}


		else{
			rate1 =  (r1min+g1min)/2.<1.?1.:(r1min+g1min)/2. ;
		}

	}
	else{
		rate1 =  (r1max + r1min)/2.<1.?1.:(r1min+r1max)/2. ;
	}
}
void radj_greymin();
void radj_greymax(){
	if ( r1max == 0 )
		rate1 = (rate1+.5*rate1)<adr?(rate1+.5*rate1):adr ;
	else if ( r1max - g1max <= x ){
		converged_gmx_rmx = 1;
		radj_greymin() ;
		printf("Call greymax but actually exe greymin");
	}
	else 
		rate1 = ( r1max + g1max)/2. ;
}
void radj_greymin(){
	if ( g1min - r1min <= x ){
		converged_gmn_rmn = 1;
		radj_greymax() ;
		printf("Call greymin but actually exe greymax");
	}
	else 
		rate1 = (r1min+g1min)/2.<1.?1.:(r1min+g1min)/2. ;
}

/***********/
//Check if r2 in range of (accuracy,overhead)
int calibrateclock(){
	printf("**************** Calibrating clock\n");
	rate1 = 1.;
//	pktsize1 = PKTSIZE; //pktsize1_checkr2;
	num1=10;
	//compute_num2(2);
	//send_tcpSnd2();
	send_tcpSnd1();

	// compare gap metric instead of pct 
	if(gaptrend_> gapthres)
		return 0;
	rate1 = 2.; // 2*r1_min, which is rate to check r2
//	pktsize1 = PKTSIZE;
	//compute_num2(2);
	//send_tcpSnd2();
	send_tcpSnd1();

	if(gaptrend_> gapthres)
		return 0;
	return 1;
}
//

void estabbass(){
 
 	if( ((int) fleet_act_pct) == 1){ // increase
		if(g1max>=rate1) g1max = g1min =0;
		r1max = rate1;//r1>ab1
		if (!converged_gmx_rmx_tm ){
			if ( !converged() ) 
				radj_increasing();
		}
		else{
			if ( !converged() )
				radj_notrend();
		}
		printf("Increase: r1min-r1max=%.2f-%.2f, g1min-g1max=%.2f-%.2f\n", r1min, r1max, g1min, g1max);
	}
	else if( ((int)fleet_act_pct) == 0){ //no trend
		if(g1min<rate1) g1min = 0;
		if(g1max<rate1) g1max=g1min=0;
		if(rate1>r1min)
			r1min=rate1;
		if ( !converged_gmn_rmn_tm && !converged() ) 
			radj_notrend() ;
		printf("No Trend: r1min-r1max=%.2f-%.2f, g1min-g1max=%.2f-%.2f\n", r1min, r1max, g1min, g1max);
	}
	else{ // grey
		if((g1min==0) && (g1max==0)){
			g1min=rate1;
			g1max=rate1;
		}
		if((rate1==g1max)||(rate1>g1max)){
			g1max=rate1;
			if ( !converged_gmx_rmx_tm ){
				if ( !converged() )
					radj_greymax() ;
			}
			else{
				if ( !converged() )
				  radj_notrend() ;
				}
			}
			else if((rate1<g1min)||(g1min==0)){
				g1min=rate1;
				if ( !converged() )
					radj_greymin() ;
			}
			printf("Grey: r1min-r1max=%.2f-%.2f, g1min-g1max=%.2f-%.2f\n", r1min, r1max, g1min, g1max);
		}

}

//
void estabclk(){
	if(vm_scheduling_detected){
	 	if( ((int)fleet_gaptrend) == 1){ // increase
			if(g1max>=rate1) g1max = g1min =0;
			r1max = rate1;//r1>ab1
			if (!converged_gmx_rmx_tm ){
				if ( !converged() ) 
					radj_increasing();
			}
			else{
				if ( !converged() )
					radj_notrend();
			}
			printf("Increase: r1min-r1max=%.2f-%.2f, g1min-g1max=%.2f-%.2f\n", r1min, r1max, g1min, g1max);
		}
		else if( ((int)fleet_gaptrend) ==0 ){ //no trend
			if(g1min<rate1) g1min = 0;
			if(g1max<rate1) g1max=g1min=0;
			if(rate1>r1min)
				r1min=rate1;
			if ( !converged_gmn_rmn_tm && !converged() ) 
				radj_notrend() ;
			printf("No Trend: r1min-r1max=%.2f-%.2f, g1min-g1max=%.2f-%.2f\n", r1min, r1max, g1min, g1max);
		}
		else{ // grey
			if((g1min==0) && (g1max==0)){
				g1min=rate1;
				g1max=rate1;
			}
			if((rate1==g1max)||(rate1>g1max)){
				g1max=rate1;
				if ( !converged_gmx_rmx_tm ){
					if ( !converged() )
						radj_greymax() ;
				}
				else{
					if ( !converged() )
					  radj_notrend() ;
					}
				}
				else if((rate1<g1min)||(g1min==0)){
					g1min=rate1;
					if ( !converged() )
						radj_greymin() ;
				}
				printf("Grey: r1min-r1max=%.2f-%.2f, g1min-g1max=%.2f-%.2f\n", r1min, r1max, g1min, g1max);
			}
	}
	else{
	 	if( (((int)fleet_gaptrend)==1) || (((int)fleet_act_pct) ==1) ){ // increase
			if(g1max>=rate1) g1max = g1min =0;
			r1max = rate1;//r1>ab1
			if (!converged_gmx_rmx_tm ){
				if ( !converged() ) 
					radj_increasing();
			}
			else{
				if ( !converged() )
					radj_notrend();
			}
			printf("Increase: r1min-r1max=%.2f-%.2f, g1min-g1max=%.2f-%.2f\n", r1min, r1max, g1min, g1max);
		}
		else if( (((int)fleet_gaptrend) == 0) && (((int)fleet_act_pct) == 0) ){ //no trend
			if(g1min<rate1) g1min = 0;
			if(g1max<rate1) g1max=g1min=0;
			if(rate1>r1min)
				r1min=rate1;
			if ( !converged_gmn_rmn_tm && !converged() ) 
				radj_notrend() ;
			printf("No Trend: r1min-r1max=%.2f-%.2f, g1min-g1max=%.2f-%.2f\n", r1min, r1max, g1min, g1max);
		}
		else{ // grey
			if((g1min==0) && (g1max==0)){
				g1min=rate1;
				g1max=rate1;
			}
			if((rate1==g1max)||(rate1>g1max)){
				g1max=rate1;
				if ( !converged_gmx_rmx_tm ){
					if ( !converged() )
						radj_greymax() ;
				}
				else{
					if ( !converged() )
					  radj_notrend() ;
					}
				}
				else if((rate1<g1min)||(g1min==0)){
					g1min=rate1;
					if ( !converged() )
						radj_greymin() ;
				}
				printf("Grey: r1min-r1max=%.2f-%.2f, g1min-g1max=%.2f-%.2f\n", r1min, r1max, g1min, g1max);
			}
	}
}


int main(int argc, char *argv[]){

	struct timeval exp_start_time, exp_end_time;

	SNDIP=argv[1];
	CLKIP = argv[2];
	wait_thres = atoi(argv[3]);// use for simulating VM
	clkenable = atoi(argv[4]);
	bassenable = atoi(argv[5]);
	r2_thres = atof(argv[6]);
	//num2_scale = atof(argv[6]);

	int iteration_max;
	int train_num =FLEETSIZE, i_train;

/************/
	gettimeofday(&exp_start_time, NULL);
	gettimeofday(&lastsleeptime, NULL);
	listen_sndtcp();
	init_clktcp();


		num1=NUM_1;
		r1max= 100.; // (1500*8)/MIN_INTERVAL*1.0; // set back to this to find the right adr
		iteration_max = (int) (log(r1max)/log(2)) *2 ;
		r1min =1.;
		rate1 = r1max;
		adr=r1max; 

		w=0.01*adr; x=.05*adr<12?.05*adr:12;
		printf("############adr=%.2f, w=%.2f, x=%.2f, r2_thres=%.3f\n", adr, w, x, r2_thres);
		//init rate2
		
		r2max = 0.;
		// while est. AB
		int iteration=0;

		double minab, maxab;
		int converged_rmx_rmn=0, converged_gmn_rmn=0, converged_rmx_gmx=0;


		//printf("Min num of clock pkts %d\n", num2);
		while (r1max-r1min>w) {//&& (g1min-r1min>x || r1max-g1max>x)){
			if(iteration > iteration_max){
				break;
			}
			printf("********************************** %d maxiteration=%d *************************************\n", iteration, iteration_max);

			double inc_fleet_gaptrend=0., inc_fleet_act_pct=0.; //for increasing trend
			double no_fleet_gaptrend=0., no_fleet_act_pct=0.; // for no trend

			num1 =NUM_1;
			rate2= r2_thres*rate1;
			double minrate2 = 3*CLKPKTSIZE*rate1/(num1*SNDPKTSIZE);
			double oldrate1;
			if(rate2 < minrate2) rate2 = minrate2;


			if (clkenable==1){
				clkstate = 1;
				if(rate2> r2max) {
					oldrate1 = rate1;
					if(calibrateclock() ==0){
						printf("******** Calibrateclk() ==0 *********\n");
						clkstate=-1;
					}
					else
						r2max = rate2;
					rate1 = oldrate1;
				}	
			}
			else{
				clkstate = -1;
				
			}
			fleetid++;
			printf("****************** Probing fleet %d \n", fleetid);
			num1 =NUM_1;
			//compute_num2(num2_scale);
			vm_scheduling_detected=0;
			for(trainid=1; trainid <= train_num; trainid++){
				printf("Receiving train %d \n", trainid);
				send_tcpSnd1();
				// Decide if fleet_gaptrend is increasing, notrend, or grey
				if(gaptrend_ > gapthres){
					inc_fleet_gaptrend++;
				}
				else if(gaptrend_ <= 0.5*gapthres){
					no_fleet_gaptrend++;
				}
				// Decide if fleet_act_pct is increasing, notrend, or grey
				if(act_pct_trend > pct_thres*1.1){
					inc_fleet_act_pct++; printf("inc_fleet_actpct=%.4f\n", inc_fleet_act_pct);
				}
				else if(act_pct_trend <pct_thres*0.9){
					no_fleet_act_pct++;
				}
				usleep(3000);

			}
			//Compute average fleet_gaptrend, fleet_act_pct
			if( (inc_fleet_gaptrend/train_num) >= fleet_thres) 
				fleet_gaptrend = 1; //inc_fleet_gaptrend/train_num;
			else if(no_fleet_gaptrend/train_num >= fleet_thres)
				fleet_gaptrend = 0; //no_fleet_gaptrend/train_num;
			else fleet_gaptrend = -1;
			//
			if( (inc_fleet_act_pct/train_num) > fleet_thres){
				fleet_act_pct = 1;
			}
			else if( (no_fleet_act_pct/train_num) > fleet_thres){
				fleet_act_pct = 0;
			}
			else fleet_act_pct = -1;
			
printf("Fleetid=%d: inc_fleetgaptrend=%.4f, no_fleetgaptend=%.4f, fleet_gaptrend=%.4f, inc_pct=%.4f, no_pct=%.4f, fleet_act_pct=%.4f, vm_detected=%d *********** \n", fleetid, inc_fleet_gaptrend, no_fleet_gaptrend, fleet_gaptrend, inc_fleet_act_pct, no_fleet_act_pct, fleet_act_pct, vm_scheduling_detected);
			//
			if(clkstate==1){
				estabclk();
			}
			else
				estabbass();

			//printf("r1min= %.2f r1max=%.2f\n", r1min, r1max);
			printf("Iteration: %.2f-%.2f, %.2f-%.2f, AB= %.2f-%.2f\n", r1min, r1max, g1min, g1max, minab, maxab);

			// wait for 2000us before sending next train
			sleep(0.1);
			/*gettimeofday(&tmp1,NULL) ;
			wt1 = (double) tmp1.tv_sec * 1000000.0 +(double)tmp1.tv_usec ;
			do{
				gettimeofday(&tmp1, NULL) ;
				wt2 = (double) tmp1.tv_sec * 1000000.0 +(double)tmp1.tv_usec ;
			}while((wt2 - wt1) < 2000 );*/
			iteration++;
		}


		gettimeofday(&exp_end_time, NULL);
		long exp_duration = (exp_end_time.tv_sec - exp_start_time.tv_sec)*1000000 + exp_end_time.tv_usec - exp_start_time.tv_usec;


		if(r1max-r1min < w) converged_rmx_rmn=1;
		if(g1min-r1min < x) converged_gmn_rmn=1;
		if(r1max-g1max < x) converged_rmx_gmx=1;
		if( converged_rmx_rmn){
			minab=r1min; maxab = r1max;
		}
		else if(converged_gmn_rmn && converged_rmx_gmx){
			minab=g1min; maxab= g1max;
		}
		else{
			minab=r1min; maxab=r1max;
		}

		printf("Done estimating: %.2f-%.2f, %.2f-%.2f, AB= %.2f-%.2f\n", r1min, r1max, g1min, g1max, minab, maxab);

		printf("Done estimating: AB= %.2f-%.2f %.4f\n", r1min, r1max, (r1min+r1max)/2);
		printf("Experiment duration: %ld\n", exp_duration);


	bzero(sndbuffer,sizeof(sndbuffer));
	sprintf(sndbuffer, "%.2f %d", 0.0, 0);
	int rc_;
	rc_ = write(snd_tcpsock, sndbuffer, sizeof(sndbuffer));
	if(rc_ < 0){
		perror("Rcv-write() error");
	}

	bzero(clkbuffer,sizeof(clkbuffer));
	sprintf(clkbuffer, "%.2f ", -2.0);
	rc_ = write(clk_tcpsock, clkbuffer, 20);
	if(rc_ < 0){
		perror("Rcv-write() error");
	}

	close(snd_tcpsock);
	close(clk_tcpsock);
	exit(0);
return 0;
}

