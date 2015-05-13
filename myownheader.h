
#ifndef  MYOWNHEADER_H
#define  MYOWNHEADER_H
#include      "stdlib.h"
#include        "stdio.h"
//一个链表，其中包括进程的首地址
//typedef struct TimerQueue {
//		void * startaddress;
//		void *endaddress;
//		int length;
//} TimerQueue;
#define LEGAL_MESSAGE_LENGTH 64
#define DISK_BUF_SIZE 16
typedef struct ProcessControlBlock{
		char pcbname[16];
		INT32  priority;
		INT32  process_id;
		void* startaddress;
		void* context;
		INT32 time;
		INT32 state;
		struct dif *diskinfo;
		UINT16* pagetable;
		struct ProcessControlBlock *next_pcb;
		struct ProcessControlBlock *previous_pcb;
}PCB;
typedef struct dif
{
	long diskid;
	long sector;
	int read0rwrite;
	int done;
	char data[16];
}DiskInfo;
typedef struct Message{
    long    target_pid;
    long    source_pid;
    long    actual_source_pid;
    long    send_length;
	char    msg_buffer[100];
	int capacity;
    long    receive_length;
    long    actual_send_length;
    struct Message *next_msg;
	struct Message *previous_msg;
} Msg;
typedef struct Frametable{
	INT32 processId;
	INT32 framenumber;
	INT32 time;
	INT32 totalframe;
	struct  Frametable *next_ft;
	struct Frametable *previous_ft;
}Ft;
typedef struct fifo{
	INT32 frame;
	struct  fifo *next_item;
}FIFO;
typedef struct ShadowPageTable{
	int vpn;
	struct dif *diskinfo;
	//struct ShadowPageTable* nextspt;
}SPT;
#endif