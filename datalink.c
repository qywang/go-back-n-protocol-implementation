#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"
#define inc(k) if(k<MAX_SEQ) k++; else k=0;
#define DATA_TIMER  2000
#define MAX_SEQ 5

struct FRAME { 
    unsigned char kind; /* FRAME_DATA */
    unsigned char ack;
    unsigned char seq;
    unsigned char data[PKT_LEN]; 
    unsigned int  padding;
};

static unsigned char frame_nr = 0, buffer[MAX_SEQ+1][PKT_LEN], nbuffered;
static unsigned char frame_expected = 0,next_frame_to_send=0,ACK_expected=0;
static int phl_ready = 0;
typedef unsigned int seq_nr;                         /* sequence or ack numbers */

static void put_frame(unsigned char *frame, int len)            //to_physical_layer
{
    *(unsigned int *)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;
}

static int between(seq_nr a, seq_nr b, seq_nr c)
{
	if((a<=b && b<c && c<=MAX_SEQ) ||
	   (c<a && a<=b && b<=MAX_SEQ) ||
	   (b<c && c<a && a<=MAX_SEQ))
	   {
		return 1;
	}
	else
		return 0;
}

static void send_data_frame(seq_nr frame_nr)
{
    struct FRAME s;

    s.kind = FRAME_DATA;        
    s.seq = frame_nr;                  //帧序号
    s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);  //相当于frame_expected-1

    memcpy(s.data, buffer[s.seq], PKT_LEN);
	dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);//日志

    put_frame((unsigned char *)&s, 3 + PKT_LEN);
    start_timer(frame_nr, DATA_TIMER);
}

static void send_ack_frame(void)
{
    struct FRAME s;

    s.kind = FRAME_ACK;
    s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

    dbg_frame("Send ACK  %d\n", s.ack);

    put_frame((unsigned char *)&s, 2);
}

int main(int argc, char **argv)
{
    int event, arg,i;
    struct FRAME f;
    int len = 0;

    protocol_init(argc, argv); 
    lprintf("Designed by Jiang Yanjun, build: " __DATE__"  "__TIME__"\n");

    disable_network_layer();

    for (;;) {
        event = wait_for_event(&arg);

        switch (event) {
        case NETWORK_LAYER_READY:
            get_packet(buffer[next_frame_to_send]);
            nbuffered++;                                  //发送窗口+1
            send_data_frame(next_frame_to_send);          
			inc(next_frame_to_send);
            break;

        case PHYSICAL_LAYER_READY:
            phl_ready = 1;
            break;

        case FRAME_RECEIVED: 
            len = recv_frame((unsigned char *)&f, sizeof f);
            if (len < 5 || crc32((unsigned char *)&f, len) != 0) {
                dbg_event("**** Receiver Error, Bad CRC Checksum\n");
                break;
            }
           dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.data);
                if(f.seq == frame_expected) {
                    put_packet(f.data, len - 7);   //to_network_layer
                    inc(frame_expected);
                }
                //send_ack_frame();
          
            while(between(ACK_expected,f.ack,next_frame_to_send)) {  //f.ack窗口下界，next_frame_to_Send窗口上界
                stop_timer(ACK_expected);
                nbuffered--;
                inc(ACK_expected);
            }
            break; 

        case DATA_TIMEOUT:
            dbg_event("---- DATA %d timeout\n", arg); 
			next_frame_to_send=ACK_expected; //重发未收到ACK的帧
			for(i=1;i<=nbuffered;i++)
			{
				send_data_frame(next_frame_to_send);
				inc(next_frame_to_send);}
				break;
			}
        if (nbuffered <MAX_SEQ && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();   //发送窗口已满
   }
}
