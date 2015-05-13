/************************************************************************

This code forms the base of the operating system you will
build.  It has only the barest rudiments of what you will
eventually construct; yet it contains the interfaces that
allow test.c and z502.c to be successfully built together.

Revision History:
1.0 August 1990
1.1 December 1990: Portability attempted.
1.3 July     1992: More Portability enhancements.
Add call to sample_code.
1.4 December 1992: Limit (temporarily) printout in
interrupt handler.  More portability.
2.0 January  2000: A number of small changes.
2.1 May      2001: Bug fixes and clear STAT_VECTOR
2.2 July     2002: Make code appropriate for undergrads.
Default program start is in test0.
3.0 August   2004: Modified to support memory mapped IO
3.1 August   2004: hardware interrupt runs on separate thread
3.11 August  2004: Support for OS level locking
4.0  July    2013: Major portions rewritten to support multiple threads
************************************************************************/

#include             "global.h"
#include             "syscalls.h"
#include             "protos.h"
#include             "string.h"
#include    "myownheader.h"
#include <time.h>
// These loacations are global and define information about the page table
extern UINT16        *Z502_PAGE_TBL_ADDR;
extern INT16         Z502_PAGE_TBL_LENGTH;
INT32 PID=-1;
PCB *timerqueue_head;
PCB* currentprocess;
PCB* readyqueue_head;
PCB* suspendqueue_head;
PCB* diskqueue_head;
Msg* private_message_head;
Msg* broadcast_message_head;
Ft* frametable_head;
FIFO* fifo_head;
INT32 Iterations=-1;
INT32 Bitmap[12][1600];
INT32 Maximummemoryoutput=1000;
INT32 Maximumfaultoutput=2000;
INT32 Maximumdisoutput=1000;
INT32 Diskoption=100;
SPT* shadowtpagetable[VIRTUAL_MEM_PAGES];
int frame[64];
#define                  DO_LOCK                     1
#define                  DO_UNLOCK                   0
#define                  SUSPEND_UNTIL_LOCKED        TRUE
#define                  DO_NOT_SUSPEND              FALSE
extern void          *TO_VECTOR [];
#define Create 0
#define Useup -99
#define Wait 1
#define Suspend 2
#define Dispatch 3
#define Ready 4
#define Terminate 5
#define N 100//for the process name
#define         ILLEGAL_PRIORITY_1G            999
#define ILLEGAL_MESSAGE_LENGTH 1000
#define         LEGAL_PRIORITY                  10
#define         ILLEGAL_PRIORITY                -3
char                 *call_names[] = { "mem_read ", "mem_write",
	"read_mod ", "get_time ", "sleep    ",
	"get_pid  ", "create   ", "term_proc",
	"suspend  ", "resume   ", "ch_prior ",
	"send     ", "receive  ", "disk_read",
	"disk_wrt ", "def_sh_ar" };
void Dispatcher();
void Move_Process_to_ReadyQueue(PCB* pcb);
void Move_Ready_To_Run(PCB* pcb);
void  RemoveFromTimerQueue(INT32 time);
void AddToTimerQueue(PCB* pcb);
PCB* Createprocess(char *processname, void* startaddress, INT32 initial_priority, INT32* processidreg, INT32* errorreg);
void GET_Process_Id(char* processname,INT32* processidreg,INT32* errorreg);
PCB* Remove_Process_From_Readyqueue_To_GetCPU();
void Add_Process_To_SuspendQueue(PCB* pcb);
void Terminate_Process(INT32 terminate,INT32* errorreg);
void Scheduler_Printer(INT32 time,char* state,INT32 target_pid);
/******************************************************************************************************************
Scheduler printer
In this routine, i need to print every process in the timer queue , ready queue and suspend queue when everytime
i  opearte the timer queue, ready queue and susupend queue. 
*****************************************************************************************************************/
void Scheduler_Printer(INT32 time,char* state,INT32 target_pid)
{
	PCB* pcb_readyqueue=(PCB* )malloc(sizeof(PCB));
	PCB* pcb_timerqueue=(PCB* )malloc(sizeof(PCB));
	PCB* pcb_suspendqueue=(PCB* )malloc(sizeof(PCB));
	PCB* pcb_diskqueue=(PCB* )malloc(sizeof(PCB));
	pcb_readyqueue=readyqueue_head;
	pcb_timerqueue=timerqueue_head;
	pcb_suspendqueue=suspendqueue_head;
	pcb_diskqueue=diskqueue_head;
	CALL(SP_setup( SP_TIME_MODE, time ));
	CALL(SP_setup_action( SP_ACTION_MODE, state ));
	CALL(SP_setup( SP_TARGET_MODE, target_pid ));
	CALL(SP_setup( SP_RUNNING_MODE, target_pid ));
	while (pcb_readyqueue->next_pcb!=NULL)
	{
		CALL(SP_setup( SP_READY_MODE, pcb_readyqueue->next_pcb->process_id ));
		pcb_readyqueue=pcb_readyqueue->next_pcb;
	}
	while (pcb_timerqueue->next_pcb!=NULL)
	{
		CALL(SP_setup( SP_TIMER_SUSPENDED_MODE, pcb_timerqueue->next_pcb->process_id ));
		pcb_timerqueue=pcb_timerqueue->next_pcb;
	}
	while (pcb_suspendqueue->next_pcb!=NULL)
	{
		CALL(SP_setup( SP_PROCESS_SUSPENDED_MODE, pcb_suspendqueue->next_pcb->process_id ));
		pcb_suspendqueue=pcb_suspendqueue->next_pcb;
	}
	while (pcb_diskqueue->next_pcb!=NULL)
	{
		CALL(SP_setup( SP_DISK_SUSPENDED_MODE, pcb_diskqueue->next_pcb->process_id ));
		pcb_diskqueue=pcb_diskqueue->next_pcb;
	}
	CALL(SP_print_line());
}
/*************************************************************************************************************************************
Move one process to ready queue
In this routine, i will add a process into the readyqueue by the priority.(compare the priority and move the process which has the most favarable priority to the top of readyqueue)
Go through the readyqueue, if the priority of this process is larger than the priority of other process which is in the readyqueue, then
just moving to the next pcb in the ready queue to compare the priority, and put the process into the position that the prior process which has the lesser number.
*******************************************************************************************************************************/
void Move_Process_to_ReadyQueue(PCB* pcb)
{
	PCB* p=(PCB *)malloc(sizeof(PCB));
	if (readyqueue_head->next_pcb==NULL)
	{
		readyqueue_head->next_pcb=pcb;
		pcb->previous_pcb=readyqueue_head;
		pcb->state=Ready;
	}
	else
	{
		p=readyqueue_head;
		while(p->next_pcb!=NULL)
		{
			if (p->next_pcb->priority<=pcb->priority)
			{
				p=p->next_pcb;
			}
			else
			{
				pcb->previous_pcb=p;
				pcb->next_pcb=p->next_pcb;
				pcb->state=Ready;
				p->next_pcb->previous_pcb=pcb;
				p->next_pcb=pcb;
				break;
			}
		}
		if (p->next_pcb==NULL)
		{
			p->next_pcb=pcb;
			pcb->previous_pcb=p;
			pcb->state=Ready;
		}

	}
}
/******************************************************************************************************************
Dispatch the process in the readyqueue to get CPU
In this routine, i wil remove one process from the readyqueue (call the "Remove_Process_From_Readyqueue_To_GetCPU" function) and 
return the pcb. 
then checking the pcb's context, if the context is null. then make the context and then save the context into pcb, and switchcontext
otherwise, if the context is not null, directly switch the context.
*****************************************************************************************************************/
void Dispatcher()
{
	static INT32 Time;
	void   *next_context=NULL;
	INT32 LockResult;
	PCB* pcb=(PCB*)malloc(sizeof(PCB));
	CALL(MEM_READ( Z502ClockStatus, &Time));
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	pcb=Remove_Process_From_Readyqueue_To_GetCPU();
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	if (Maximumdisoutput>0)
	{
		if (pcb!=NULL)
		{
			Scheduler_Printer(Time,"Dispatch",pcb->process_id);
			Maximumdisoutput--;
		}
	}
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	if (pcb!=NULL)
	{
		if (pcb->context==NULL)
		{
			Z502MakeContext(&next_context,pcb->startaddress, KERNEL_MODE );
			pcb->context=next_context;
			pcb->state=Dispatch;
			currentprocess=pcb;
			Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE , &next_context );
		}
		else
		{
			pcb->state=Dispatch;
			currentprocess=pcb;
			Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE , &currentprocess->context );
		}
	}
	else
	{
		Z502Idle();
		while(readyqueue_head->next_pcb==NULL)
		{
			MEM_READ( Z502ClockStatus, &Time);
		}
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		pcb=Remove_Process_From_Readyqueue_To_GetCPU();
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		pcb->state=Dispatch;
		currentprocess=pcb;
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE , &currentprocess->context );
	}


}
/******************************************************************************************************************
Remove the process From timerqueue
In this routine, i remove the processes from timer queue which the value of "time" in its pcb are less than the actual "time"
Just go through the timer queue to get process which the value of "time" in its pcb are less than the actual "time", everytime when i remove 
one process from timerqueue, i need to reschedule the timer queue. 
*****************************************************************************************************************/
void  RemoveFromTimerQueue(INT32 time)
{
	PCB* p=timerqueue_head;	
	PCB* pcb;
	INT32 LockResult;
	/*READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,
	&LockResult);*/
	if (p->next_pcb!=NULL)
	{
		while(p->next_pcb!=NULL)
		{
			if (p->next_pcb->time>time)
			{
				p=p->next_pcb;
			}
			else
			{
				if (p==timerqueue_head)
				{
					if (p->next_pcb->next_pcb!=NULL)
					{
						pcb=(PCB*)malloc(sizeof(PCB));
						pcb=p->next_pcb;
						p->next_pcb->next_pcb->previous_pcb=p;
						p->next_pcb=p->next_pcb->next_pcb;
						pcb->next_pcb=NULL;
						pcb->previous_pcb=NULL;
						if (pcb->state!=Suspend)
						{
							READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
							Move_Process_to_ReadyQueue(pcb);
							READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
						}
						else
						{
							Add_Process_To_SuspendQueue(pcb);
						}	
					}
					else
					{
						pcb=(PCB*)malloc(sizeof(PCB));
						pcb=p->next_pcb;
						p->next_pcb=NULL;
						pcb->next_pcb=NULL;
						pcb->previous_pcb=NULL;
						if (pcb->state!=Suspend)
						{

							READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
							Move_Process_to_ReadyQueue(pcb);
							READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);

						}
						else
						{
							READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
							Add_Process_To_SuspendQueue(pcb);
							READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
						}
					}
				}
				else
				{
					pcb=(PCB*)malloc(sizeof(PCB));
					pcb=p->next_pcb;
					p->previous_pcb->next_pcb=p->next_pcb;
					p->next_pcb->previous_pcb=p->previous_pcb;
					pcb->next_pcb=NULL;
					pcb->previous_pcb=NULL;
					if (pcb->state!=Suspend)
					{
						READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
						Move_Process_to_ReadyQueue(pcb);
						READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					}
					else
					{
						READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
						Add_Process_To_SuspendQueue(pcb);
						READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					}
				}
			}
		}
	}
}
/*****************************************************************************
Add the process into the timerqueue 
In this routine i just add a process into the timer queue
I use the double-link structure to build the queue, so when i add a process to the timer queue,
i need to set the previouspcb and nextpcb of this process carefully. Because it is very easy to make 
the "link" mistake in there.
And i need to compare the time in pcb, put the least "time" in the top of the timer queue 
******************************************************************************/
void AddToTimerQueue(PCB* pcb) 
{
	PCB* p=(PCB *)malloc(sizeof(PCB));
	INT32 LockResult;
	if (timerqueue_head->next_pcb==NULL)
	{
		if (pcb->state!=Terminate)
		{
			timerqueue_head->next_pcb=pcb;
		}
	}
	else
	{
		p=timerqueue_head;
		while(p->next_pcb!=NULL)
		{
			if(pcb->time>p->next_pcb->time)
			{
				p=p->next_pcb;
			}
			else
			{
				pcb->next_pcb=p->next_pcb;
				pcb->previous_pcb=p;
				p->next_pcb->previous_pcb=pcb;
				p->next_pcb=pcb;
				break;
			}
		}
		if (p->next_pcb==NULL)
		{
			p->next_pcb=pcb;
			pcb->previous_pcb=p;
		}
	}
}
/****************************************************************************************************
Create the process 
In this process, i just allow to create 8 processes.
Before, i create a process, there are lots of things i need to check, 
one is priority, if priority is illegal, then this process cannot be created.return NULL.
the other one is the process name, if the process name has already existed in the readyqueue, then this process cannot be created.return NULL.
If the process is successfully created, then return the process's pcb. 
**************************************************************************************************/
PCB* Createprocess(char *processname, void* startaddress, INT32 initial_priority, INT32* processidreg, INT32* errorreg)
{
	PCB* pcb=(PCB *)malloc(sizeof(PCB));
	PCB* p=(PCB *)malloc(sizeof(PCB));
	//BOOL valid_pcb=false;
	if (PID==8)
	{
		*errorreg=1;
		return NULL;
	}
	if (initial_priority==ILLEGAL_PRIORITY)
	{
		*errorreg=1;
		pcb=NULL;
		return pcb;
	}
	else
	{
		//遍历readyqueue里的进程有没有重复的名字
		p=readyqueue_head;
		if (p->next_pcb==NULL)
		{
			PID+=1;
			//*processidreg=PID;
			//pcb->pcbname=processname;
			strcpy(pcb->pcbname,processname);
			pcb->priority=initial_priority;
			pcb->process_id=PID;
			pcb->startaddress=startaddress;
			pcb->time=0;
			pcb->next_pcb=NULL;
			pcb->previous_pcb=NULL;
			pcb->context=NULL;
			pcb->state=Create;
			*processidreg=pcb->process_id;
			*errorreg=0;
			return pcb;
		}
		else
		{
			//pcb->pcbname=processname;
			strcpy(pcb->pcbname,processname);
			while(p->next_pcb!=NULL)
			{
				//如果名字不一样，就往后挪一个节点。这里pcbname是存的地址，要取出地址里的值和新来pcb地址取值比较
				if (strcmp((p->next_pcb->pcbname),(pcb->pcbname))!=0)
				{
					p=p->next_pcb;
				}
				else
				{
					//这里说明出错误了，有两个进程名字一样了，所以应该改变erroreg的值为1，表示有一个错误！
					*errorreg=1;
					pcb=NULL;
					return pcb;
				}
			}
		}
		PID+=1;
		//*processidreg=PID;
		pcb->priority=initial_priority;
		pcb->process_id=PID;
		pcb->startaddress=startaddress;
		pcb->time=0;
		pcb->next_pcb=NULL;
		pcb->previous_pcb=NULL;
		pcb->context=NULL;
		pcb->state=Create;
		*errorreg=0;
		*processidreg=pcb->process_id;
		return pcb;
	}
}
/****************************************************************************************************
Get the process's pid by processname
This is the routine just get the process's id
Just go through the timer queue and ready queue to get the process's id by the "processname"
**************************************************************************************************/
void GET_Process_Id(char* processname,INT32* processidreg,INT32* errorreg)
{
	PCB* p_readyqueue=(PCB *)malloc(sizeof(PCB));
	PCB* p_timerqueue=(PCB *)malloc(sizeof(PCB));
	BOOL readyqueue=TRUE;
	p_readyqueue=readyqueue_head;
	p_timerqueue=timerqueue_head;
	if (strcmp("",processname)==0)
	{
		*processidreg=currentprocess->process_id;
		*errorreg=0;
	}
	else
	{
		while(p_readyqueue->next_pcb!=NULL)
		{
			if (strcmp(p_readyqueue->next_pcb->pcbname,processname)!=0)
			{
				p_readyqueue=p_readyqueue->next_pcb;
			}
			else
			{
				*processidreg=p_readyqueue->process_id;
				*errorreg=0;
				break;
			}
		}
		//在readyqueue中找不到名字和process那么一样的。那么就返回错误
		if (p_readyqueue->next_pcb==NULL)
		{
			readyqueue=FALSE;
		}
		while (p_timerqueue->next_pcb!=NULL)
		{
			if (strcmp(p_timerqueue->next_pcb->pcbname,processname)!=0)
			{
				p_timerqueue=p_timerqueue->next_pcb;
			}
			else
			{
				*processidreg=p_timerqueue->process_id;
				*errorreg=0;
				break;
			}

		}
		if (p_timerqueue->next_pcb==NULL&&readyqueue==FALSE)
		{
			*errorreg=1;
		}
	}
}
/***********************************************************************************
Remove the process from ready queue
This is the routine just remove the process from ready queue
Just taking the first process out in the radyqueue , return the process' pcb
************************************************************************************/
PCB* Remove_Process_From_Readyqueue_To_GetCPU()
{
	PCB* pcb=(PCB* )malloc(sizeof(PCB));
	if (readyqueue_head->next_pcb==NULL)
	{
		pcb=NULL;
		return pcb;
	}
	else
	{

		if (readyqueue_head->next_pcb->next_pcb==NULL)
		{
			pcb=readyqueue_head->next_pcb;
			readyqueue_head->next_pcb=NULL;
			//因为这个进程已经从readyqueu移出来，所以它现在不属于任何一个queue
			pcb->next_pcb=NULL;
			pcb->previous_pcb=NULL;
			return pcb;
		}
		else
		{
			pcb=readyqueue_head->next_pcb;
			readyqueue_head->next_pcb->next_pcb->previous_pcb=readyqueue_head;
			readyqueue_head->next_pcb=readyqueue_head->next_pcb->next_pcb;
			pcb->next_pcb=NULL;
			pcb->previous_pcb=NULL;
			return pcb;
		}
	}
}
/******************************************************************************************************************
Add the process to suspend queue
This is the routine just add the process into the suspend queue
Just set the suspend queue head's nextpcb to the the process, and set the process's nextpcb to the suspend queue head's nextpcb's nextpcb,
and set the  process's previouspcb to the suspend queue head(means add the process to the top of the suspend queue) 
*********************************************************************************************************************/
void Add_Process_To_SuspendQueue(PCB* pcb)
{
	if (suspendqueue_head->next_pcb==NULL)
	{
		suspendqueue_head->next_pcb=pcb;
		pcb->previous_pcb=suspendqueue_head;
		pcb->state=Suspend;
	}
	else
	{
		//直接加在suspendqueue最前面
		pcb->previous_pcb=suspendqueue_head;
		pcb->next_pcb=suspendqueue_head->next_pcb;
		pcb->state=Suspend;
		suspendqueue_head->next_pcb->previous_pcb=pcb;
		suspendqueue_head->next_pcb=pcb;
	}
}
/******************************************************************************************************************
Suspend one process and add to suspend queue(In test1e, because in this routine, it will suspend itself, the code can never back to the code after  the suspend itself .)
This is the routine about to suspend the process add the process into the suspend queue(Use the routine named "Add_Process_To_SuspendQueue")
Just set the suspend queue head's nextpcb to the the process, and set the process's nextpcb to the suspend queue head's nextpcb's nextpcb,
and set the  process's previouspcb to the suspend queue head(means add the process to the top of the suspend queue) 
*********************************************************************************************************************/
void Suspend_Process( INT32 processidreg,INT32* errorreg)
{
	//use the processidreg to find the process in the readyqueue, andremove it.
	PCB* p_readyqueue=(PCB*)malloc(sizeof(PCB));
	PCB* p_timerqueue=(PCB*)malloc(sizeof(PCB));
	PCB* pcb_to_suspend=(PCB*)malloc(sizeof(PCB));
	INT32 LockResult;
	BOOL notchange=TRUE;
	if (processidreg==-1)
	{
		Add_Process_To_SuspendQueue(currentprocess);
		currentprocess->state=Suspend;
		Dispatcher();
		*errorreg=0;
	}
	else
	{

		if (readyqueue_head->next_pcb==NULL&&timerqueue_head->next_pcb==NULL)
		{
			*errorreg=1;
		}
		else
		{
			p_readyqueue=readyqueue_head;
			while (p_readyqueue->next_pcb!=NULL)
			{
				if (p_readyqueue->next_pcb->process_id!=processidreg)
				{
					p_readyqueue=p_readyqueue->next_pcb;
				}
				else
				{
					if (p_readyqueue->next_pcb->next_pcb==NULL)
					{
						pcb_to_suspend=p_readyqueue->next_pcb;
						pcb_to_suspend->state=Suspend;
						p_readyqueue->next_pcb=NULL;
						pcb_to_suspend->next_pcb=NULL;
						pcb_to_suspend->previous_pcb=NULL;
						//这里suspendqueue是不用管排序的，直接往里面加就可以
						READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
						Add_Process_To_SuspendQueue(pcb_to_suspend);
						READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
						notchange=FALSE;
						*errorreg=0;
						break;
					}
					else
					{
						pcb_to_suspend=p_readyqueue->next_pcb;
						pcb_to_suspend->state=Suspend;
						p_readyqueue->next_pcb->next_pcb->previous_pcb=p_readyqueue;
						p_readyqueue->next_pcb=p_readyqueue->next_pcb->next_pcb;
						pcb_to_suspend->next_pcb=NULL;
						pcb_to_suspend->previous_pcb=NULL;
						READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
						Add_Process_To_SuspendQueue(pcb_to_suspend);
						READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
						notchange=FALSE;
						*errorreg=0;
						break;
					}
				}
			}
			p_timerqueue=timerqueue_head;
			while (p_timerqueue->next_pcb!=NULL)
			{
				if (p_timerqueue->next_pcb->process_id!=processidreg)
				{
					p_timerqueue=p_timerqueue->next_pcb;
				}
				else
				{
					p_timerqueue->next_pcb->state=Suspend;
					*errorreg=0;
					break;
				}
			}
			if (p_readyqueue->next_pcb==NULL&&notchange&&p_timerqueue->next_pcb==NULL)
			{
				*errorreg=1;
			}
		}
	}
}
/******************************************************************************************************************
Resume one process in suspend queue
This is the routine resume the process from suspend queue.
I just go through the suspend queue to find the process which has the "processidreg", and take the process out of 
the suspend queue to the readyqueue, return ture if resume successfuly, otherwise return false when there is no such process which 
has pid which is equal to "processidreg"
*********************************************************************************************************************/
BOOL Resume_Process(INT32 processidreg,INT32* errorreg)
{
	PCB* p_suspendqueue=(PCB*)malloc(sizeof(PCB));
	PCB* pcb=(PCB*)malloc(sizeof(PCB));
	INT32 LockResult;
	BOOL notchange=TRUE;
	p_suspendqueue=suspendqueue_head;	
	if (p_suspendqueue->next_pcb==NULL)
	{
		*errorreg=1;
		return FALSE;
	}
	else
	{
		while (p_suspendqueue->next_pcb!=NULL)
		{
			if (p_suspendqueue->next_pcb->process_id!=processidreg)
			{
				p_suspendqueue=p_suspendqueue->next_pcb;
			}
			else
			{
				//从suspendqueue中找到process，从suspendqueue中取出process，并加进readyqueue
				if (p_suspendqueue->next_pcb->next_pcb==NULL)
				{
					pcb=p_suspendqueue->next_pcb;
					p_suspendqueue->next_pcb=NULL;
					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					Move_Process_to_ReadyQueue(pcb);
					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					notchange=FALSE;
					*errorreg=0;
					return TRUE;
				}
				else
				{
					pcb=p_suspendqueue->next_pcb;
					p_suspendqueue->next_pcb->next_pcb->previous_pcb=p_suspendqueue;
					p_suspendqueue->next_pcb=p_suspendqueue->next_pcb->next_pcb;
					pcb->next_pcb=NULL;
					pcb->previous_pcb=NULL;
					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					Move_Process_to_ReadyQueue(pcb);
					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					notchange=FALSE;
					*errorreg=0;
					return TRUE;
				}

			}
		}
		if (p_suspendqueue->next_pcb==NULL&&notchange)
		{
			*errorreg=1;
			return FALSE;
		}
	}
}
/****************************
Remove the process from the readyqueue to change the priority.
This is the routine to change the process's priority.
In this routine, i just remove a process to change the priority that the "newpriority" give.
After changing the priority, i call the "Move_Process_to_ReadyQueue" to add this process to the readyqueue.
***************************/
void Remove_Process_From_Readyqueue_To_Change_Priority(INT32 processid,INT32 newpriority)
{
	PCB* pcb_readyqueue=(PCB*)malloc(sizeof(PCB));
	PCB* pcb=(PCB*)malloc(sizeof(PCB));
	INT32 LockResult;
	pcb_readyqueue=readyqueue_head;
	while (pcb_readyqueue->next_pcb!=NULL)
	{
		if (pcb_readyqueue->next_pcb->process_id!=processid)
		{
			pcb_readyqueue=pcb_readyqueue->next_pcb;
		}
		else
		{
			pcb=pcb_readyqueue->next_pcb;
			if (pcb_readyqueue->next_pcb->next_pcb==NULL)
			{
				pcb_readyqueue->next_pcb=NULL;
			}
			else
			{
				pcb_readyqueue->next_pcb->next_pcb->previous_pcb=pcb_readyqueue;
				pcb_readyqueue->next_pcb=pcb_readyqueue->next_pcb->next_pcb;
			}
			pcb->priority=newpriority;
			pcb->next_pcb=NULL;
			pcb->previous_pcb=NULL;
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			Move_Process_to_ReadyQueue(pcb);
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			break;
		}
	}

}
/****************************
Change the priority of process
This is the routine to change the process's priority.
But there is a special stiuation I need to think.
It is that when i change priority of one process which is already in the readyqueue.
In this situation, after changing the priority of this process, i need to reschedule the process in the ready queue.(put the most favarable priority in the top of  readyqueue)
But if changing the priority of one process which is in any other queue, such as tiemer queue, suspend queue. It is unecessary to
reschedule the process in that queue. return true if change successfully, otherwise return false when the changepriority is illegal or change priority unsuccessfully 
***************************/
BOOL Change_Process_Priority(INT32 changeprocessid, INT32 changepriority, INT32* errorreg)
{
	//如果processid=-1，则直接改变currenprocess的priority
	//如果不是，则根据processId,在readyqueue，timerqueue，还有suspendqueue里找到这个process并修改它的priority
	PCB* pcb_readyqueue=(PCB*)malloc(sizeof(PCB));
	PCB* pcb_timerqueue=(PCB*)malloc(sizeof(PCB));
	PCB* pcb_suspendqueue=(PCB*)malloc(sizeof(PCB));
	PCB* pcb=(PCB*)malloc(sizeof(PCB));
	INT32 LockResult;
	BOOL change=FALSE;
	//怎么判断传进来的changepriority是无效的
	if (changepriority<0||changepriority>99)
	{
		*errorreg=1;
		return change;
	}
	if (changeprocessid==-1)
	{
		currentprocess->priority=changepriority;
		*errorreg=0;
		return change;
	}
	if (timerqueue_head->next_pcb==NULL)
	{
		pcb=NULL;
	}
	else
	{
		pcb_timerqueue=timerqueue_head;
		while (pcb_timerqueue->next_pcb!=NULL)
		{
			if (pcb_timerqueue->next_pcb->process_id!=changeprocessid)
			{
				pcb_timerqueue=pcb_timerqueue->next_pcb;
			}
			else
			{
				pcb_timerqueue->next_pcb->priority=changepriority;
				change=TRUE;
				*errorreg=0;
				break;
			}
		}
	}
	if (change==FALSE)
	{
		if (suspendqueue_head->next_pcb==NULL)
		{
			pcb=NULL;
		}
		else
		{
			pcb_suspendqueue=suspendqueue_head;
			while (pcb_suspendqueue->next_pcb!=NULL)
			{
				if (pcb_suspendqueue->next_pcb->process_id!=changeprocessid)
				{
					pcb_suspendqueue=pcb_suspendqueue->next_pcb;
				}
				else
				{
					pcb_suspendqueue->next_pcb->priority=changepriority;
					change=TRUE;
					*errorreg=0;
					break;
				}
			}
		}
	}
	if (change==FALSE)
	{
		if (readyqueue_head->next_pcb==NULL)
		{
			pcb=NULL;
		}
		else
		{
			//从readyqueue中找到该process，将其移出
			//改变其priority
			//然后放进readyqueue
			//这里需要对readyqueue进行两次操作，必须注意锁的问题
			pcb_readyqueue=readyqueue_head;
			while (pcb_readyqueue->next_pcb!=NULL)
			{
				if (pcb_readyqueue->next_pcb->process_id!=changeprocessid)
				{
					pcb_readyqueue=pcb_readyqueue->next_pcb;
				}
				else
				{
					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					Remove_Process_From_Readyqueue_To_Change_Priority(changeprocessid,changepriority);
					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					change=TRUE;
					*errorreg=0;
					break;
				}
			}
		}
	}
	if (change==FALSE)
	{
		*errorreg=1;
		return change;
	}
	return change;
}
/****************************
Send  message
This is the routine for one process to send the message to another process.
Before send a message, i need to judge if this pid is legal, if not then i return the false.
If yes, i will send the message.
When send a message, if the targetId=-1, then i need basicly to add this message to the broadcastqueue. and set sourceId equal to the sender's pid
and set the targetId to -1(means this message canbe got by any first process), and set the message buffer equal to the message that sender sends. retrun true,otherwise return false.
But if  targetId!=-1, just the same steps like the above stiuation but just set the targetId equal to the actual targetId,retrun true otherwise return false.
***************************/
BOOL Send_Message_To_Process(INT32 targetId,char* message_buffer,INT32 message_send_length,INT32* errorreg)
{
	Msg* msg=(Msg*)malloc(sizeof(Msg));
	if (targetId<-1||targetId>99)
	{
		*errorreg=1;
		return FALSE;
	}
	else
	{
		if (targetId==-1)
		{
			if (broadcast_message_head->capacity>10)
			{
				*errorreg=1;
				return FALSE;
			}
			//如果是targetid为-1，则代表这个消息是要传送给所有reciever的
			if (broadcast_message_head->next_msg==NULL)
			{
				msg->target_pid=targetId;
				if (sizeof(message_buffer)<=message_send_length&&message_send_length!=ILLEGAL_MESSAGE_LENGTH)
				{
					//msg->msg_buffer=message_buffer;
					strcpy(msg->msg_buffer,message_buffer);
					msg->target_pid=targetId;
					msg->source_pid=currentprocess->process_id;
					msg->send_length=message_send_length;
					msg->previous_msg=broadcast_message_head;
					msg->next_msg=NULL;
					broadcast_message_head->next_msg=msg;
					broadcast_message_head->capacity+=1;
					if (suspendqueue_head->next_pcb!=NULL)
					{
						//在这里我把这个进程给resume出来了，表示这个进程的消息已经到了
						//它完全可以receive它的消息了
						Resume_Process(targetId,errorreg);

					}
					*errorreg=0;
					return TRUE;
				}
				else
				{
					*errorreg=1;
					return FALSE;
				}
			}
			else
			{
				//broadcast列表不只一个消息
				msg->target_pid=targetId;
				if (sizeof(message_buffer)<=message_send_length&&message_send_length!=ILLEGAL_MESSAGE_LENGTH)
				{
					//msg->msg_buffer=message_buffer;
					strcpy(msg->msg_buffer,message_buffer);
					msg->send_length=message_send_length;
					msg->target_pid=targetId;
					msg->source_pid=currentprocess->process_id;
					msg->previous_msg=broadcast_message_head;
					msg->next_msg=broadcast_message_head->next_msg;
					broadcast_message_head->next_msg->previous_msg=msg;
					broadcast_message_head->next_msg=msg;
					broadcast_message_head->capacity+=1;
					if (suspendqueue_head->next_pcb!=NULL)
					{
						Resume_Process(targetId,errorreg);
					}
					*errorreg=0;
					return TRUE;
				}
				else
				{
					*errorreg=1;
					return FALSE;
				}
			}
		}
		else
		{
			if (private_message_head->capacity>10)
			{
				*errorreg=1;
				return FALSE;
			}
			//如果是targetid为-1，则代表这个消息是要传送给所有reciever的
			if (private_message_head->next_msg==NULL)
			{
				msg->target_pid=targetId;
				if (sizeof(message_buffer)<=message_send_length&&message_send_length!=ILLEGAL_MESSAGE_LENGTH)
				{
					//msg->msg_buffer=message_buffer;
					strcpy(msg->msg_buffer,message_buffer);
					msg->send_length=message_send_length;
					msg->target_pid=targetId;
					msg->source_pid=currentprocess->process_id;
					msg->previous_msg=private_message_head;
					msg->next_msg=NULL;
					private_message_head->next_msg=msg;
					private_message_head->capacity+=1;
					if (suspendqueue_head->next_pcb!=NULL)
					{
						Resume_Process(targetId,errorreg);
					}
					*errorreg=0;
					return TRUE;
				}
				else
				{
					*errorreg=1;
					return FALSE;
				}
			}
			else
			{
				//broadcast列表不只一个消息
				msg->target_pid=targetId;
				if (sizeof(message_buffer)<=message_send_length&&message_send_length!=ILLEGAL_MESSAGE_LENGTH)
				{
					//msg->msg_buffer=message_buffer;
					strcpy(msg->msg_buffer,message_buffer);
					msg->send_length=message_send_length;
					msg->target_pid=targetId;
					msg->source_pid=currentprocess->process_id;
					msg->previous_msg=private_message_head;
					msg->next_msg=private_message_head->next_msg;
					private_message_head->next_msg->previous_msg=msg;
					private_message_head->next_msg=msg;
					private_message_head->capacity+=1;
					if (suspendqueue_head->next_pcb!=NULL)
					{
						Resume_Process(targetId,errorreg);
						/*	if (readyqueue_head->next_pcb!=NULL)
						{
						Dispatcher();
						}*/
					}
					*errorreg=0;
					return TRUE;
				}
				else
				{
					*errorreg=1;
					return FALSE;
				}
			}
		}
	}
}
/****************************
Receive  message from message queue
This is the routine to receive the message from message queue when a process call "receive".
When receive a message, if the sourceId=-1, then I  need the receiver to check the privateQ and publicQ to find the tagetId which 
is equal to the receiver's pid, and if  the receiver already get the message then  will not go to the publicQ. But if the receiver doesn't get
any message in message queue then will go to the public queue to get message. If the sourceId!=-1, then the receiver just go to the message
queue to get the message that targetId is equal to the receiver's pid
And before receive the message queue, i need to check the size of the message buffer to see if the lenth is less than the message_receive_length.
***************************/
BOOL Receive_Process_Message(INT32 sourceId,char* message_buffer,INT32 message_recieve_length,INT32* message_send_length,INT32* senderId,INT32* errorreg)
{			
	//判断这个sourceid是-1，那么就要去privatemsgqueue和broadcastqueue
	BOOL receiveprivate=TRUE;
	BOOL receiveprivate2=TRUE;
	BOOL receivebroadcast=TRUE;
	BOOL receivebroadcast2=TRUE;
	BOOL receive=FALSE;
	Msg* msg_privatequeue=(Msg*)malloc(sizeof(Msg));
	Msg* msg_broadcastqueue=(Msg*)malloc(sizeof(Msg));
	if (currentprocess->process_id==0)
	{
		//如果是主进程，那么只要判断在private中有没有消息即可

	}
	if (sourceId==-1)
	{
		//主进程也在判断public
		//先看privatequeue, 遍历寻找，找到删除这个消息，并跳出；只找了一个消息并退出
		if (private_message_head->next_msg==NULL)
		{
			//则说明这个privatemsgqueue没有消息
			receiveprivate=FALSE;
		}
		else
		{
			msg_privatequeue=private_message_head;
			while (msg_privatequeue->next_msg!=NULL)
			{
				if (msg_privatequeue->next_msg->target_pid!=currentprocess->process_id)
				{
					msg_privatequeue=msg_privatequeue->next_msg;
					if (msg_privatequeue->next_msg==NULL)
					{
						receiveprivate=FALSE;
					}
				}
				else
				{
					if (msg_privatequeue->next_msg->send_length>message_recieve_length||message_recieve_length==ILLEGAL_MESSAGE_LENGTH)
					{
						//则说明这个message太长不能加进去
						*errorreg=1;
						msg_privatequeue=msg_privatequeue->next_msg;
					}
					else
					{
						//要给message_buffer,message_send_length,senderId,errorreg
						strcpy(message_buffer, msg_privatequeue->next_msg->msg_buffer);
						//message_buffer=msg_privatequeue->next_msg->msg_buffer;
						*message_send_length=msg_privatequeue->next_msg->send_length;
						*senderId=msg_privatequeue->next_msg->source_pid;
						*errorreg=0;
						receive=TRUE;
						if (msg_privatequeue->next_msg->next_msg==NULL)
						{
							msg_privatequeue->next_msg=NULL;
							private_message_head->capacity-=1;
							receiveprivate=TRUE;
							break;
						}
						else
						{
							msg_privatequeue->next_msg->next_msg->previous_msg=msg_privatequeue;
							msg_privatequeue->next_msg=msg_privatequeue->next_msg->next_msg;
							private_message_head->capacity-=1;
							receiveprivate=TRUE;
							break;
						}

					}

				}
			}
			//在列表中找完也找不到message，则
			if (msg_privatequeue->next_msg==NULL&&receiveprivate!=TRUE)
			{
				receiveprivate=FALSE;
			}
		}
		if (broadcast_message_head->next_msg==NULL)
		{
			receivebroadcast=FALSE;
		}
		else
		{
			msg_broadcastqueue=broadcast_message_head;
			while (msg_broadcastqueue->next_msg!=NULL)
			{
				/*if (msg_broadcastqueue->next_msg->target_pid!=currentprocess->process_id)
				{
				msg_broadcastqueue=msg_broadcastqueue->next_msg;
				if (msg_broadcastqueue->next_msg==NULL)
				{
				receivebroadcast=FALSE;
				}
				}
				else
				{*/
				if (msg_broadcastqueue->next_msg->send_length>message_recieve_length||message_recieve_length==ILLEGAL_MESSAGE_LENGTH)
				{
					//则说明这个message太长不能加进去
					*errorreg=1;
					msg_broadcastqueue=msg_broadcastqueue->next_msg;
				}
				else
				{
					//要给message_buffer,message_send_length,senderId,errorreg
					//但是这里的话，因为可能在privatequeue已经找到了要发给这个进程的消息，那么这里赋值就会覆盖掉数据了！
					strcpy(message_buffer, msg_broadcastqueue->next_msg->msg_buffer);
					//message_buffer=msg_broadcastqueue->next_msg->msg_buffer;
					*message_send_length=msg_broadcastqueue->next_msg->send_length;
					*senderId=msg_broadcastqueue->next_msg->source_pid;
					*errorreg=0;
					receive=TRUE;
					if (msg_broadcastqueue->next_msg->next_msg==NULL)
					{
						msg_broadcastqueue->next_msg=NULL;
						broadcast_message_head->capacity-=1;
						break;
					}
					else
					{
						msg_broadcastqueue->next_msg->next_msg->previous_msg=msg_broadcastqueue;
						msg_broadcastqueue->next_msg=msg_broadcastqueue->next_msg->next_msg;
						broadcast_message_head->capacity-=1;
						break;
					}
				}
				/*	}*/
			}
			if (msg_broadcastqueue->next_msg==NULL&&receivebroadcast!=TRUE)
			{
				receivebroadcast=FALSE;
			}

		}
		if (receivebroadcast==FALSE&&receiveprivate==FALSE&&*errorreg!=1)
		{
			//这个时候可能是这个send给这个进程的消息还没有到，所以这个时候不应该是error
			//而应该suspend它自己，然后从readyqueue里取进程继续执行
			//errorreg=1;
			Suspend_Process(-1,errorreg);
			//currentprocess->state=Suspend;
			//这里需要重新receive消息。
			//这里有两个queue都要取东西
			//如果从一个queue中取了，那么另一个queue就不取了
			if (private_message_head->next_msg!=NULL)
			{
				msg_privatequeue=private_message_head;
				while (msg_privatequeue->next_msg!=NULL)
				{
					if (msg_privatequeue->next_msg->target_pid!=currentprocess->process_id)
					{

						msg_privatequeue=msg_privatequeue->next_msg;
					}
					else
					{
						if (msg_privatequeue->next_msg->send_length>message_recieve_length||message_recieve_length==ILLEGAL_MESSAGE_LENGTH)
						{
							msg_privatequeue=msg_privatequeue->next_msg;
						}
						else
						{
							strcpy(message_buffer, msg_privatequeue->next_msg->msg_buffer);
							//message_buffer=msg_privatequeue->next_msg->msg_buffer;
							*message_send_length=msg_privatequeue->next_msg->send_length;
							*senderId=msg_privatequeue->next_msg->source_pid;
							*errorreg=0;
							//receive=TRUE;
							if (msg_privatequeue->next_msg->next_msg==NULL)
							{
								msg_privatequeue->next_msg=NULL;
								private_message_head->capacity-=1;
								receiveprivate=TRUE;
								break;
							}
							else
							{
								msg_privatequeue->next_msg->next_msg->previous_msg=msg_privatequeue;
								msg_privatequeue->next_msg=msg_privatequeue->next_msg->next_msg;
								private_message_head->capacity-=1;
								receiveprivate=TRUE;
								break;
							}
						}
					}
				}
				if (msg_privatequeue->next_msg==NULL&&receiveprivate!=TRUE)
				{
					msg_broadcastqueue=broadcast_message_head;
					if (broadcast_message_head->next_msg!=NULL)
					{
						while (msg_broadcastqueue->next_msg!=NULL)
						{
							if (msg_broadcastqueue->next_msg->send_length>message_recieve_length||message_recieve_length==ILLEGAL_MESSAGE_LENGTH)
							{
								//则说明这个message太长不能加进去
								*errorreg=1;
								msg_broadcastqueue=msg_broadcastqueue->next_msg;
							}
							else
							{
								//要给message_buffer,message_send_length,senderId,errorreg
								//但是这里的话，因为可能在privatequeue已经找到了要发给这个进程的消息，那么这里赋值就会覆盖掉数据了！
								strcpy(message_buffer, msg_broadcastqueue->next_msg->msg_buffer);
								//message_buffer=msg_broadcastqueue->next_msg->msg_buffer;
								*message_send_length=msg_broadcastqueue->next_msg->send_length;
								*senderId=msg_broadcastqueue->next_msg->source_pid;
								*errorreg=0;
								receive=TRUE;
								if (msg_broadcastqueue->next_msg->next_msg==NULL)
								{
									msg_broadcastqueue->next_msg=NULL;
									broadcast_message_head->capacity-=1;
									break;
								}
								else
								{
									msg_broadcastqueue->next_msg->next_msg->previous_msg=msg_broadcastqueue;
									msg_broadcastqueue->next_msg=msg_broadcastqueue->next_msg->next_msg;
									broadcast_message_head->capacity-=1;
									break;
								}
							}
						}
					}
				}
			}
		}
	}
	else
	{
		if (private_message_head->next_msg==NULL)
		{
			//则说明这个privatemsgqueue没有消息
			*errorreg=1;
		}
		else
		{
			msg_privatequeue=private_message_head;
			while (msg_privatequeue->next_msg!=NULL)
			{
				if (msg_privatequeue->next_msg->target_pid!=currentprocess->process_id)
				{
					msg_privatequeue=msg_privatequeue->next_msg;
					if (msg_privatequeue->next_msg==NULL)
					{
						receiveprivate=FALSE;
					}
				}
				else
				{
					if (msg_privatequeue->next_msg->send_length>message_recieve_length||message_recieve_length==ILLEGAL_MESSAGE_LENGTH)
					{
						//则说明这个message太长不能加进去
						*errorreg=1;
						msg_privatequeue=msg_privatequeue->next_msg;
					}
					else
					{
						//要给message_buffer,message_send_length,senderId,errorreg
						strcpy(message_buffer, msg_privatequeue->next_msg->msg_buffer);
						//message_buffer=msg_privatequeue->next_msg->msg_buffer;
						*message_send_length=msg_privatequeue->next_msg->send_length;
						*senderId=msg_privatequeue->next_msg->source_pid;
						*errorreg=0;
						receive=TRUE;
						if (msg_privatequeue->next_msg->next_msg==NULL)
						{
							msg_privatequeue->next_msg=NULL;
							private_message_head->capacity-=1;
							break;
						}
						else
						{
							msg_privatequeue->next_msg->next_msg->previous_msg=msg_privatequeue;
							msg_privatequeue->next_msg=msg_privatequeue->next_msg->next_msg;
							private_message_head->capacity-=1;
							break;
						}
					}

				}
			}
			if (msg_privatequeue->next_msg==NULL&&receiveprivate!=TRUE&&*errorreg!=1)
			{
				//Add_Process_To_SuspendQueue(currentprocess);
				Suspend_Process(-1,errorreg);
				currentprocess->state=Suspend;
			}
		}
	}
	return receive;
	//先去broadcastqueue，把公开的msg全部取出来，存进message_buffer里（只有一个进程取出，取完就把该消息删除）
	//去privatequeue，把和sourceId相等的，在messagequeue找到该source，然后找到它要传送的消息，存进messagebuffer
	//存进messagebuffer之前，要和message_recieve_length判断（大就不行，小就可以存进buffer，然后将sendbuffer大小存进message_send_length）
	//将传给它消息的processId存进targetId；
}
/****************************
Terminate the process
if the "terminate"= -1, the just set the currentprocess's state to "Terminate",
everytime when i terminate one process,and i will call the Dispatch to let other process get CPU.
if the "terminate"!=-1, the i will terminate process and clear all the queue.
***************************/
void Terminate_Process(INT32 terminate,INT32* errorreg)
{
	static INT32 Time;
	INT32 LockResult;
	if (terminate==-1)
	{
		currentprocess->state=Terminate;
	}
	else
	{
		//将所有进程结束，也就是readyqueue，timerqueueu，suspendqueue里所有进程都结束
		CALL(MEM_READ( Z502ClockStatus, &Time));
		readyqueue_head->next_pcb=NULL;
		timerqueue_head->next_pcb=NULL;
		suspendqueue_head->next_pcb=NULL;
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		Scheduler_Printer(Time,"Finish",0);
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		printf("ALL TERMINATE!"); 
		Z502Halt();
	}

}
/***************************************************************
This routine is just like the "Createprocess" i write in later. 
this routine is just for create the first process (the parent process) and move this process into readyqueue and 
take it  out from readyqueue imediately to get CPU and then switch the context.
***************************************************************/
void* os_make_process(void *ThreadStartAddress,void *nextcontext, char* processname)
{

	PCB* pcb=(PCB *)malloc(sizeof(PCB));
	INT32 i;
	INT32 LockResult;
	static INT32 Time;
	currentprocess=(PCB *)malloc(sizeof(PCB));
	PID+=1;
	strcpy(pcb->pcbname,processname);
	//pcb->pagetable=Z502_PAGE_TBL_ADDR;
	pcb->startaddress=ThreadStartAddress;
	pcb->process_id=PID;
	pcb->next_pcb=NULL;
	pcb->previous_pcb=NULL;
	pcb->priority=LEGAL_PRIORITY;
	pcb->time=0;
	pcb->state=Create;
	pcb->pagetable=NULL;
	pcb->diskinfo=NULL;
	Move_Process_to_ReadyQueue(pcb);
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	Scheduler_Printer(Time,"Create",pcb->process_id);
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	CALL(MEM_READ( Z502ClockStatus, &Time));
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	pcb=Remove_Process_From_Readyqueue_To_GetCPU();
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	Scheduler_Printer(Time,"Dispatch",pcb->process_id);
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	Z502MakeContext(&nextcontext,ThreadStartAddress, KERNEL_MODE );
	pcb->state=Dispatch;
	pcb->context=nextcontext;
	currentprocess=pcb;
	return nextcontext;
}
/***************************************************************
	Find_diskqueue
this routine is find whether one diskoption is done or want to be done in the diskqueue
each time when a disk option triger the fault handler, it will go into the diskqueue and find one pcb which 
contains this disk and then remove this pcb from the diskqueue add into the readyqueue
***************************************************************/
PCB* find_diskqueue(int diskid)
{

	PCB* p=(PCB*)malloc(sizeof(PCB));
	PCB* pp=(PCB*)malloc(sizeof(PCB));
	/*pp->diskinfo=NULL;*/
	if (diskqueue_head->next_pcb==NULL)
	{
		printf("this is null diskqueue!\n");
		getchar();
		return NULL;
	}
	else
	{
		p=diskqueue_head->next_pcb;
		while (p!=NULL)
		{
			if (p->diskinfo->diskid!=diskid)
			{
				p=p->next_pcb;
			}
			else
			{
				if (p->next_pcb==NULL)
				{
					pp=p;
					p->previous_pcb->next_pcb=NULL;
					pp->next_pcb=NULL;
					pp->previous_pcb=NULL;

				}
				else
				{
					pp=p;
					p->previous_pcb->next_pcb=p->next_pcb;
					p->next_pcb->previous_pcb=p->previous_pcb;
					pp->next_pcb=NULL;
					pp->previous_pcb=NULL;

				}
				return pp;
			}
		}
		return NULL;
	}


}
/***************************************************************
	Diskoption
this routine is just like a combination of disk_write and disk_read routine.
every time when come to this routine, just add this pcb into our diskqueue first and 
then do the disk option.
***************************************************************/
void diskoption(long diskid,long sector,char* data,INT32 readorwrite,PCB* pcb)
{
	INT32 Temp;
	static INT32 Time;
	INT32 LockResult;
	PCB* p=(PCB*)malloc(sizeof(PCB));
	DiskInfo* diskinfo=(DiskInfo*)malloc(sizeof(DiskInfo));
	diskinfo->done=1;

	diskinfo->diskid=diskid;
	diskinfo->read0rwrite=readorwrite;
	diskinfo->sector=sector;
	if (readorwrite==1)
	{
		memcpy(diskinfo->data,data,DISK_BUF_SIZE);
	}
	if (diskqueue_head->next_pcb==NULL)
	{
		pcb->diskinfo=diskinfo;
		diskqueue_head->next_pcb=pcb;
		pcb->previous_pcb=diskqueue_head;
	}
	else
	{
		p=diskqueue_head;
		while (p!=NULL)
		{
			if (p->next_pcb!=NULL)
			{
				p=p->next_pcb;
			}
			else
			{
				pcb->diskinfo=diskinfo;
				p->next_pcb=pcb;
				pcb->previous_pcb=p;
				break;
			}
		}
	}
	MEM_WRITE(Z502DiskSetID, &diskid);
	MEM_WRITE(Z502DiskSetSector, &sector);
	MEM_WRITE(Z502DiskSetBuffer, (INT32*)diskinfo->data);
	MEM_WRITE(Z502DiskSetAction, &readorwrite);
	Temp=0;
	MEM_WRITE(Z502DiskStart, &Temp);
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		CALL(MEM_READ( Z502ClockStatus, &Time));
		if (readorwrite==1)
		{
			Scheduler_Printer(Time,"Write",currentprocess->process_id);
			Diskoption--;
		}
		else
		{
			Scheduler_Printer(Time,"Read",currentprocess->process_id);
			Diskoption--;
		}
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	/*printf("have't add into queue!!");*/
}
/************************************************************************
	Traveldiskqueue
	this routine just for debug. it just travel through the diskqueue to see the 
	what's going on in my diskqueue
************************************************************************/
void traveldiskqueue()
{
	PCB* p;
	p=diskqueue_head->next_pcb;

	printf("        Disk queue                       \n");
	if (p==NULL)
	{
		printf("the diskqueue is null\n");
		getchar();
	}
	while (p!=NULL)
	{
		printf("this is the %dth process with %dth disk\n",p->process_id,p->diskinfo->diskid);
		p=p->next_pcb;
	}

}
/************************************************************************
	Travelshadowpagetable
	this routine just for debug. it just travel through the shadowpagetable to see the 
	what's going on in my shadowpagetable
************************************************************************/
void travelshadow()
{ 
	int i;
	printf("---------------------------------\n");
	for ( i = 0; i < 1024; i++)
	{
		printf("this the vpn is %dth\n",shadowtpagetable[i]->vpn);
		if (shadowtpagetable[i]->diskinfo!=NULL)
		{
			printf("this the %dth disk and %dth sector\n",shadowtpagetable[i]->diskinfo->diskid,shadowtpagetable[i]->diskinfo->sector);
		}
		else
		{
			printf("this the %dth vpn has no diskinfo\n");
		}
	}
	printf("---------------------------------\n");
}
/************************************************************************
INTERRUPT_HANDLER
When the Z502 gets a hardware interrupt, it transfers control to
this routine in the OS.
************************************************************************/
void    interrupt_handler( void ) {
	INT32              device_id;
	INT32              status;
	INT32              Index = 0;
	INT32 LockResult;
	INT32 Temp;
	static INT32       how_many_interrupt_entries = 0;    /** TEMP **/
	static INT32     Time;
	//DiskInfo* diskinfo=(DiskInfo*)malloc(sizeof(DiskInfo));
	PCB* pcb=(PCB*)malloc(sizeof(PCB));
	INT32 operateagain=0;
	// Get cause of interrupt
	MEM_READ(Z502InterruptDevice, &device_id);
	// Set this device as target of our query
	MEM_WRITE(Z502InterruptDevice, &device_id);
	// Now read the status of this device
	MEM_READ(Z502InterruptStatus, &status);
	//printf("yes\n");
	/** REMOVE THE NEXT SIX LINES **/
	while (device_id!=-1)
	{

		switch (device_id)
		{
		case TIMER_INTERRUPT:
			if (status==ERR_SUCCESS)
			{
				CALL(MEM_READ( Z502ClockStatus, &Time));
				READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
				RemoveFromTimerQueue(Time);
				READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			}
			break;
		case DISK_INTERRUPT_DISK1:
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			//printf("%dth disk option is done!\n",1);
			/*printf("------------------------------------------\n");
			traveldiskqueue();
			printf("------------------------------------------\n");*/
			pcb=find_diskqueue(1);
			if (pcb!=NULL)
			{
				if (pcb->diskinfo->done==0)
				{
				/*	printf("------------------------------------------\n");*/
					operateagain=1;
					diskoption(pcb->diskinfo->diskid,pcb->diskinfo->sector,pcb->diskinfo->data,pcb->diskinfo->read0rwrite,pcb);
					/*if (pcb->diskinfo->read0rwrite==0)
					{
						printf("this is %dth disk read in interrupt!\n",pcb->diskinfo->diskid);
						printf("------------------------------------------\n");
					}
					else if (pcb->diskinfo->read0rwrite==1)
					{
						printf("this is %dth disk write in interrupt!\n",pcb->diskinfo->diskid);
						printf("------------------------------------------\n");
					}*/

				}
				if (operateagain!=1)
				{
			/*		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			CALL(MEM_READ( Z502ClockStatus, &Time));
		if (pcb->diskinfo->read0rwrite==1&&Diskoption>0)
		{
			Scheduler_Printer(Time,"WriteDone",currentprocess->process_id);
				Diskoption--;
		}
		else if(pcb->diskinfo->read0rwrite==0&&Diskoption>0)
		{
			Scheduler_Printer(Time,"ReadDone",currentprocess->process_id);
			Diskoption--;
		}
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);*/
					
					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					Move_Process_to_ReadyQueue(pcb);

					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					
				}

			}
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			break;
		case  DISK_INTERRUPT_DISK2:
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			//printf("%dth disk option is done!\n",2);
			/*			printf("------------------------------------------\n");
			traveldiskqueue();
			printf("------------------------------------------\n");*/
			pcb=find_diskqueue(2);
	
			if (pcb!=NULL)
			{
				if (pcb->diskinfo->done==0)
				{
					operateagain=1;
					/*printf("------------------------------------------\n");*/

					diskoption(pcb->diskinfo->diskid,pcb->diskinfo->sector,pcb->diskinfo->data,pcb->diskinfo->read0rwrite,pcb);
					//pcb->diskinfo->done=1;
				/*	if (pcb->diskinfo->read0rwrite==0)
					{
						printf("this is %dth disk read in interrupt!\n",pcb->diskinfo->diskid);
						printf("------------------------------------------\n");
					}
					else if (pcb->diskinfo->read0rwrite==1)
					{
						printf("this is %dth disk write in interrupt!\n",pcb->diskinfo->diskid);
						printf("------------------------------------------\n");
					}*/
				}

				if (operateagain!=1)
				{
			/*	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			CALL(MEM_READ( Z502ClockStatus, &Time));
		if (pcb->diskinfo->read0rwrite==1&&Diskoption>0)
		{
			Scheduler_Printer(Time,"WriteDone",currentprocess->process_id);
				Diskoption--;
		}
		else if(pcb->diskinfo->read0rwrite==0&&Diskoption>0)
		{
			Scheduler_Printer(Time,"ReadDone",currentprocess->process_id);
			Diskoption--;
		}
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);*/
					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					Move_Process_to_ReadyQueue(pcb);

					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
				}	
			}
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			break;
		case DISK_INTERRUPT_DISK3:
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			//printf("%dth disk option is done!\n",3);
			/*			printf("------------------------------------------\n");
			traveldiskqueue();
			printf("------------------------------------------\n");*/
			pcb=find_diskqueue(3);
			if (pcb!=NULL)
			{
				if (pcb->diskinfo->done==0)
				{operateagain=1;
				//printf("------------------------------------------\n");
				diskoption(pcb->diskinfo->diskid,pcb->diskinfo->sector,pcb->diskinfo->data,pcb->diskinfo->read0rwrite,pcb);
				//pcb->diskinfo->done=1;
				/*if (pcb->diskinfo->read0rwrite==0)
				{
					printf("this is %dth disk read in interrupt!\n",pcb->diskinfo->diskid);
					printf("------------------------------------------\n");
				}
				else if (pcb->diskinfo->read0rwrite==1)
				{
					printf("this is %dth disk write in interrupt!\n",pcb->diskinfo->diskid);
					printf("------------------------------------------\n");
				}*/
				}

				if (operateagain!=1)
				{
				/*	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			CALL(MEM_READ( Z502ClockStatus, &Time));
		if (pcb->diskinfo->read0rwrite==1&&Diskoption>0)
		{
			Scheduler_Printer(Time,"WriteDone",currentprocess->process_id);
				Diskoption--;
		}
		else if(pcb->diskinfo->read0rwrite==0&&Diskoption>0)
		{
			Scheduler_Printer(Time,"ReadDone",currentprocess->process_id);
			Diskoption--;
		}
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);*/
					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					Move_Process_to_ReadyQueue(pcb);

					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
				}	
			}

			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			break;
		case DISK_INTERRUPT_DISK4:
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			//printf("%dth disk option is done!\n",4);
			/*		printf("------------------------------------------\n");
			traveldiskqueue();
			printf("------------------------------------------\n");*/
			pcb=find_diskqueue(4);
			if (pcb!=NULL)
			{
				if (pcb->diskinfo->done==0)
				{
					operateagain=1;
				/*printf("------------------------------------------\n");*/
					diskoption(pcb->diskinfo->diskid,pcb->diskinfo->sector,pcb->diskinfo->data,pcb->diskinfo->read0rwrite,pcb);
					//pcb->diskinfo->done=1;
				/*	if (pcb->diskinfo->read0rwrite==0)
					{
						printf("this is %dth disk read in interrupt!\n",pcb->diskinfo->diskid);
						printf("------------------------------------------\n");
					}
					else if (pcb->diskinfo->read0rwrite==1)
					{
						printf("this is %dth disk write in interrupt!\n",pcb->diskinfo->diskid);
						printf("------------------------------------------\n");
					}*/
				}
				if (operateagain!=1)
				{
					/*READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			CALL(MEM_READ( Z502ClockStatus, &Time));
		if (pcb->diskinfo->read0rwrite==1&&Diskoption>0)
		{
			Scheduler_Printer(Time,"WriteDone",currentprocess->process_id);
				Diskoption--;
		}
		else if(pcb->diskinfo->read0rwrite==0&&Diskoption>0)
		{
			Scheduler_Printer(Time,"ReadDone",currentprocess->process_id);
			Diskoption--;
		}
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);*/
					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					Move_Process_to_ReadyQueue(pcb);

					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
				}	
			}
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			break;
		case DISK_INTERRUPT_DISK5:
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			//printf("%dth disk option is done!\n",5);
		/*	printf("------------------------------------------\n");
			traveldiskqueue();
			printf("------------------------------------------\n");*/
			pcb=find_diskqueue(5);

			if (pcb!=NULL)
			{
				if (pcb->diskinfo->done==0)
				{operateagain=1;
				//printf("------------------------------------------\n");
				diskoption(pcb->diskinfo->diskid,pcb->diskinfo->sector,pcb->diskinfo->data,pcb->diskinfo->read0rwrite,pcb);
				//pcb->diskinfo->done=1;
				/*if (pcb->diskinfo->read0rwrite==0)
				{
					printf("this is %dth disk read in interrupt!\n",pcb->diskinfo->diskid);
					printf("------------------------------------------\n");
				}
				else if (pcb->diskinfo->read0rwrite==1)
				{
					printf("this is %dth disk write in interrupt!\n",pcb->diskinfo->diskid);
				printf("------------------------------------------\n");
				}*/
				}

				if (operateagain!=1)
				{
					/*READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			CALL(MEM_READ( Z502ClockStatus, &Time));
		if (pcb->diskinfo->read0rwrite==1&&Diskoption>0)
		{
			Scheduler_Printer(Time,"WriteDone",currentprocess->process_id);
				Diskoption--;
		}
		else if(pcb->diskinfo->read0rwrite==0&&Diskoption>0)
		{
			Scheduler_Printer(Time,"ReadDone",currentprocess->process_id);
			Diskoption--;
		}
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);*/
					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
					Move_Process_to_ReadyQueue(pcb);
					READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
				}	
			}
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			break;
		default:
			break;
		}

		// Clear out this device - we're done with it

		// Set this device as target of our query
		MEM_WRITE(Z502InterruptClear, &Index );
		MEM_READ(Z502InterruptDevice, &device_id);
		// Set this device as target of our query
		MEM_WRITE(Z502InterruptDevice, &device_id);
	}


}                                       /* End of interrupt_handler */
/************************************************************************
		Disk_Write
when the test ask for disk_write, then the svc will run this routine
in this routine, first, i need to judge whether this disk is free by using Z502DiskStatus
if it is free, then i add this pcb into diskqueue first and then do the write  option
after that, i dispacher. so when this disk get into the fault handler, the write option could be done.
if it is not free, i just add this pcb into disk queue and then dispacher. after the process triger the 
fault handler, it will get back to the next line of dispacher which do copy the data into our data buffer
************************************************************************/
void Disk_Write(long diskid,long sector,char* data)
{
	INT32 Temp;
	static INT32 Time;
	PCB* p=(PCB*)malloc(sizeof(PCB));
	DiskInfo* diskinfo=(DiskInfo*)malloc(sizeof(DiskInfo));
	INT32 LockResult;
	diskinfo->diskid=diskid;
	diskinfo->sector=sector;
	diskinfo->read0rwrite=1;
	memcpy(diskinfo->data,data,DISK_BUF_SIZE);
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	MEM_WRITE(Z502DiskSetID, &diskid);
	MEM_READ(Z502DiskStatus, &Temp);
	if (Temp==DEVICE_FREE)
	{
		//printf("this is %dth process writinging in %dth disk!\n",currentprocess->process_id,diskid);
		if (diskqueue_head->next_pcb==NULL)
		{
			currentprocess->diskinfo=diskinfo;
			diskqueue_head->next_pcb=currentprocess;
			currentprocess->previous_pcb=diskqueue_head;
		}
		else
		{
			p=diskqueue_head;
			while (p!=NULL)
			{
				if (p->next_pcb!=NULL)
				{
					p=p->next_pcb;
				}
				else
				{
					currentprocess->diskinfo=diskinfo;
					p->next_pcb=currentprocess;
					currentprocess->previous_pcb=p;
					break;
				}
			}
		}
		diskinfo->done=1;
		MEM_WRITE(Z502DiskSetSector, &sector);
		MEM_WRITE(Z502DiskSetBuffer, &diskinfo->data);
		Temp=1;
		MEM_WRITE(Z502DiskSetAction, &Temp);
		Temp=0;
		MEM_WRITE(Z502DiskStart, &Temp);
			if (Diskoption>0)
		{
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		CALL(MEM_READ( Z502ClockStatus, &Time));
		
		Diskoption--;
		Scheduler_Printer(Time,"Write",currentprocess->process_id);
			
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		}
	}
	else
	{
		/*take the pcb into the disk queue
		call dispatcher*/
		//diskinfo->data=data;
		diskinfo->done=0;	
		printf("this is %dth wait for write in %dth disk!\n",currentprocess->process_id,diskid);
		if (diskqueue_head->next_pcb==NULL)
		{
			currentprocess->diskinfo=diskinfo;
			diskqueue_head->next_pcb=currentprocess;
			currentprocess->previous_pcb=diskqueue_head;
		}
		else
		{
			p=diskqueue_head;
			while (p!=NULL)
			{
				if (p->next_pcb!=NULL)
				{
					p=p->next_pcb;
				}
				else
				{
					currentprocess->diskinfo=diskinfo;
					p->next_pcb=currentprocess;
					currentprocess->previous_pcb=p;
					break;
				}
			}
		}
	}
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	Dispatcher();
}
/************************************************************************
		Disk_Read
when the test ask for disk_read, then the svc will run this routine
in this routine, first, i need to judge whether this disk is free by using Z502DiskStatus
if it is free, then i add this pcb into diskqueue first and then do the  read option
after that, i dispacher. so when this disk get into the fault handler, the write option could be done.
if it is not free, i just add this pcb into disk queue and then dispacher.
************************************************************************/
void Disk_Read(long diskid,long sector,char* data)
{
	INT32 Temp;
	static INT32 Time;
	PCB* p=(PCB*)malloc(sizeof(PCB));
	DiskInfo* diskinfo=(DiskInfo*)malloc(sizeof(DiskInfo));
	INT32 LockResult;
	diskinfo->diskid=diskid;
	diskinfo->sector=sector;
	diskinfo->read0rwrite=0;
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	MEM_WRITE(Z502DiskSetID, &diskid);
	MEM_READ(Z502DiskStatus, &Temp);
	if (Temp==DEVICE_FREE)
	{
		//printf("this is %dth process reading in %dth disk!\n",currentprocess->process_id,diskid);
		if (diskqueue_head->next_pcb==NULL)
		{
			currentprocess->diskinfo=diskinfo;
			diskqueue_head->next_pcb=currentprocess;
			currentprocess->previous_pcb=diskqueue_head;
		}
		else
		{
			p=diskqueue_head;
			while (p!=NULL)
			{
				if (p->next_pcb!=NULL)
				{
					p=p->next_pcb;
				}
				else
				{
					currentprocess->diskinfo=diskinfo;
					p->next_pcb=currentprocess;
					currentprocess->previous_pcb=p;
					break;
				}
			}
		}
		Temp=0;
		MEM_WRITE(Z502DiskSetID, &diskid);
		MEM_WRITE(Z502DiskSetSector, &sector);
		MEM_WRITE(Z502DiskSetBuffer,&diskinfo->data);
		MEM_WRITE(Z502DiskSetAction, &Temp);
		MEM_WRITE(Z502DiskStart, &Temp);
		diskinfo->done=1;
		if (Diskoption>0)
		{
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		CALL(MEM_READ( Z502ClockStatus, &Time));
		
		Diskoption--;
		Scheduler_Printer(Time,"Read",currentprocess->process_id);
			
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		}
		
	}
	else
	{
		//printf("this is %dth process wait to read in %dth disk  !\n",currentprocess->process_id,diskid);
		diskinfo->done=0;
		if (diskqueue_head->next_pcb==NULL)
		{
			currentprocess->diskinfo=diskinfo;
			diskqueue_head->next_pcb=currentprocess;
			currentprocess->previous_pcb=diskqueue_head;
		}
		else
		{
			p=diskqueue_head->next_pcb;
			while (p!=NULL)
			{
				if (p->next_pcb!=NULL)
				{
					p=p->next_pcb;
				}
				else
				{
					currentprocess->diskinfo=diskinfo;
					p->next_pcb=currentprocess;
					currentprocess->previous_pcb=p;
					break;
				}
			}

		}		
	}
	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
	Dispatcher();
	memcpy(data,currentprocess->diskinfo->data,DISK_BUF_SIZE);
	//getchar();
}
/************************************************************************
		MemoryPrint
Every time i need to print what's going on in my memory, it will display
 which vpn occupied this specific frame, and what's frame state.
************************************************************************/
void MemoryPrint(int frame,int stat)
{
	int valid=0;
	int refe=0;
	int modify=0;
	if ((currentprocess->pagetable[stat]& PTBL_VALID_BIT)==32768)
	{
		valid=4;
	}
	if ((currentprocess->pagetable[stat]& PTBL_MODIFIED_BIT)==16384)
	{
		modify=2;
	}
	if ((currentprocess->pagetable[stat]& PTBL_REFERENCED_BIT)==8192)
	{
		refe=1;
	}
	MP_setup(frame,currentprocess->process_id,stat,valid+modify+refe);


}
/************************************************************************
FAULT_HANDLER
The beginning of the OS502.  Used to receive hardware faults.
************************************************************************/
void    fault_handler( void )
{
	INT32       device_id;
	INT32       status;
	INT32       Index = 0;
	INT32*   errorreg;
	INT32 result;
	INT32 keeptrack=-1;
	int i;
	Ft* ft=(Ft *)malloc(sizeof(Ft));
	Ft* transverse=(Ft *)malloc(sizeof(Ft));
	INT32 framenumber;
	INT32 data[DISK_BUF_SIZE]={0};
	INT32 LockResult;
	// Get cause of interrupt
	MEM_READ(Z502InterruptDevice, &device_id );
	//Set this device as target of our query
	MEM_WRITE(Z502InterruptDevice, &device_id );
	// Now read the status of this device
	MEM_READ(Z502InterruptStatus, &status );
	if (Maximumfaultoutput>0)
	{
		printf( "Fault_handler: Found vector type %d with value %d\n",device_id, status );
		Maximumfaultoutput--;
	}
	
	switch (device_id)
	{
	case PRIVILEGED_INSTRUCTION:
		Terminate_Process(-2,NULL);
	case INVALID_MEMORY:
		if (currentprocess->pagetable==NULL)
		{
			Z502_PAGE_TBL_LENGTH = 1024;
			Z502_PAGE_TBL_ADDR = (UINT16 *) calloc(sizeof(UINT16),Z502_PAGE_TBL_LENGTH);
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			framenumber=getframe();
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			currentprocess->pagetable=Z502_PAGE_TBL_ADDR;
			Z502_PAGE_TBL_ADDR[status]|=PTBL_VALID_BIT+framenumber;
			//遍历这个进程的pagetable所有page遍历一次
			//将有page的放setup进去
			//通过status找到pagenumber，setup进去，找到64个结束
		/*READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			for ( i = 0; i < 1024; i++)
			{
				if ((currentprocess->pagetable[i]&PTBL_PHYS_PG_NO)==0&&(currentprocess->pagetable[i]&PTBL_VALID_BIT)==32768)
				{
					keeptrack+=1;
					MemoryPrint(0,i);
				}
				else if ((currentprocess->pagetable[i]&PTBL_PHYS_PG_NO)>=1)
				{
					keeptrack+=1;
					MemoryPrint(currentprocess->pagetable[i]&PTBL_PHYS_PG_NO,i);
				}
				else if (keeptrack==64)
				{
					keeptrack=-1;
					break;
				}
			}
			MP_print_line();
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);*/

		}
		else
		{
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			framenumber=getframe();
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			//the vpn had used a frame but be viewed as a victim and removed
			//find in shadowpagetable, if find the one in shadow
			// then read the data and wrtite the data into pyhsical memory
			if (status>=1024)
			{
				Z502Halt();
			}
			if (shadowtpagetable[status]->vpn==status)
			{
				//travelshadow();
				if (framenumber!=Useup)
				{
					currentprocess->pagetable[status]|=PTBL_VALID_BIT+framenumber;
				/*	READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			for ( i = 0; i < 1024; i++)
			{
				if ((currentprocess->pagetable[i]&PTBL_PHYS_PG_NO)==0&&(currentprocess->pagetable[i]&PTBL_VALID_BIT)==32768)
				{
					keeptrack+=1;
					MemoryPrint(0,i);
				}
				else if ((currentprocess->pagetable[i]&PTBL_PHYS_PG_NO)>=1)
				{
					keeptrack+=1;
					MemoryPrint(currentprocess->pagetable[i]&PTBL_PHYS_PG_NO,i);
				}
				else if (keeptrack==64)
				{
					keeptrack=-1;
					break;
				}
			}
			MP_print_line();
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);*/
					//printf("before read the diskid is %d, and the disk sector is %d\n",shadowtpagetable[status]->diskinfo->diskid,shadowtpagetable[status]->diskinfo->sector);
					Disk_Read(shadowtpagetable[status]->diskinfo->diskid,shadowtpagetable[status]->diskinfo->sector,(char*)data);
					//printf("the victim's data is %d\n",*data);
					Z502WritePhysicalMemory(framenumber,(char*)data);
					shadowtpagetable[status]->vpn=-1;
					shadowtpagetable[status]->diskinfo=NULL;
				}
				else
				{
					//find a vctim and then use the frame to write our own data.
					find_victim();
					framenumber=getframe();
					currentprocess->pagetable[status]|=PTBL_VALID_BIT+framenumber;
				/*READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			for ( i = 0; i < 1024; i++)
			{
				if ((currentprocess->pagetable[i]&PTBL_PHYS_PG_NO)==0&&(currentprocess->pagetable[i]&PTBL_VALID_BIT)==32768)
				{
					keeptrack+=1;
					MemoryPrint(0,i);
				}
				else if ((currentprocess->pagetable[i]&PTBL_PHYS_PG_NO)>=1)
				{
					keeptrack+=1;
					MemoryPrint(currentprocess->pagetable[i]&PTBL_PHYS_PG_NO,i);
				}
				else if (keeptrack==64)
				{
					keeptrack=-1;
					break;
				}
			}
			MP_print_line();
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);*/
				//printf("before read the diskid is %d, and the disk sector is %d\n",shadowtpagetable[status]->diskinfo->diskid,shadowtpagetable[status]->diskinfo->sector);		
			Disk_Read(shadowtpagetable[status]->diskinfo->diskid,shadowtpagetable[status]->diskinfo->sector,(char*)data);
					Z502WritePhysicalMemory(framenumber,(char*)data);
					//printf("the victim's data is %d\n",*data);
					shadowtpagetable[status]->vpn=-1;
					shadowtpagetable[status]->diskinfo=NULL;
				}
			}
			//not in the shadow, then just do the normal thing
			else
			{
				if (framenumber!=Useup)
				{
					if (status>=1024)
					{
						Z502Halt();
					}
					else
					{
						currentprocess->pagetable[status]|=PTBL_VALID_BIT+framenumber;
						/*READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			for ( i = 0; i < 1024; i++)
			{
				if ((currentprocess->pagetable[i]&PTBL_PHYS_PG_NO)==0&&(currentprocess->pagetable[i]&PTBL_VALID_BIT)==32768)
				{
					keeptrack+=1;
					MemoryPrint(0,i);
				}
				else if ((currentprocess->pagetable[i]&PTBL_PHYS_PG_NO)>=1)
				{
					keeptrack+=1;
					MemoryPrint(currentprocess->pagetable[i]&PTBL_PHYS_PG_NO,i);
				}
				else if (keeptrack==64)
				{
					keeptrack=-1;
					break;
				}
			}
			MP_print_line();
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);*/
					}

				}
				else
				{
					find_victim();
					framenumber=getframe();
					currentprocess->pagetable[status]|=PTBL_VALID_BIT+framenumber;
		/*READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			for ( i = 0; i < 1024; i++)
			{
				if ((currentprocess->pagetable[i]&PTBL_PHYS_PG_NO)==0&&(currentprocess->pagetable[i]&PTBL_VALID_BIT)==32768)
				{
					keeptrack+=1;
					MemoryPrint(0,i);
				}
				else if ((currentprocess->pagetable[i]&PTBL_PHYS_PG_NO)>=1)
				{
					keeptrack+=1;
					MemoryPrint(currentprocess->pagetable[i]&PTBL_PHYS_PG_NO,i);
				}
				else if (keeptrack==64)
				{
					keeptrack=0;
					break;
				}
			}
			MP_print_line();
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);*/
				}
			}
		}
		if (Maximummemoryoutput>0)
		{
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			for ( i = 0; i < 1024; i++)
			{
				if ((currentprocess->pagetable[i]&PTBL_PHYS_PG_NO)==0&&(currentprocess->pagetable[i]&PTBL_VALID_BIT)==32768)
				{
					keeptrack+=1;
					MemoryPrint(0,i);
				}
				else if ((currentprocess->pagetable[i]&PTBL_PHYS_PG_NO)>=1)
				{
					keeptrack+=1;
					MemoryPrint(currentprocess->pagetable[i]&PTBL_PHYS_PG_NO,i);
				}
				else if (keeptrack==64)
				{
					keeptrack=-1;
					break;
				}
			}
			MP_print_line();
			Maximummemoryoutput--;
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		}
		
		break;
	default:
		break;
	}
	// Clear out this device - we're done with it
	MEM_WRITE(Z502InterruptClear, &Index );
}                                       /* End of fault_handler */
/***************************************************************
	Find_Victim
when the frame is already full, we need to find a frame as victim use it again.
There are lots of algorithm you can choose to implement this routine 
I just use the FIFO algorithm, so each time i just pick a frame which is in the 
first positon of the FIFO queue and then get the data indirectly from our pagetable and 
do a diskwrite to store this data resulted in we can find this data again next time
***************************************************************/
INT32 find_victim()
{
	int i;
	int j;
	int k;
	int l;
	INT32 frame;
	BOOL getdisk=FALSE;
	long       disk_id;
	long      sector;
	Ft* f;
	INT32 data[DISK_BUF_SIZE]={0};
	DiskInfo* diskio=(DiskInfo*)malloc(sizeof(DiskInfo));
	//take the first frame out and store the framenumber
	//remove the firstframe from FIFOqueue
	frame=fifo_head->next_item->frame;
	if (fifo_head->next_item->next_item!=NULL)
	{
		fifo_head->next_item=fifo_head->next_item->next_item;
	}
	f=frametable_head->next_ft;
	for ( i = 0; i <=frame; i++)
	{
		if (i==frame)
		{
			//get the processId and use this id to get the process
			if (currentprocess->process_id==f->processId)
			{
				//find in the pagetable to get the vpn which use that frame.
				for ( j = 0; j < 1024; j++)
				{
					if (currentprocess->pagetable[j]!=0)
					{

						if ((currentprocess->pagetable[j]& PTBL_PHYS_PG_NO)==frame)
						{
							Z502ReadPhysicalMemory(frame, (char*)data);

							Iterations+=1;
							currentprocess->pagetable[j]=0;

							disk_id = (currentprocess->process_id/ 2) % MAX_NUMBER_OF_DISKS + 1;
							sector = currentprocess->process_id + (Iterations * 17) % NUM_LOGICAL_SECTORS;
							diskio->diskid=disk_id;
							diskio->sector=sector;
							shadowtpagetable[j]->diskinfo=diskio;
							shadowtpagetable[j]->vpn=j;
							Disk_Write(disk_id,sector,(char*)data);
							break;
						}
					}
				}
			}
			f->processId=-1;
			// then get the pagetable of that process
			// find the one which use the frame we want to swap out
			// get the data that stored in the pyhics memory
			// stored this data in other place
			break;
		}
		else
		{
			f=f->next_ft;
		}
	}

}
/***************************************************************
	GetFrame
	travel the frametable to find a free frame, in my design, i initialize the the process
	of each frame in frame table with -1, so if that frame is free, that mean that the value of 
	process should be -1 about this frame
	each time when i get a free frame, i will add this frame into the tail of my FIFO queue, so
	next time when i want to find a victim use FIFO  algorithm i can just get the frame by getting 
	the first one on my FIFO QUEUE.
***************************************************************/
INT32 getframe()
{
	INT32 i;
	INT32 Time;
	Ft* f=(Ft*)malloc(sizeof(Ft));
	FIFO* fif=(FIFO*)malloc(sizeof(FIFO));
	f=frametable_head->next_ft;
	for (i = 0; i < 64; i++)
	{
		if (f->processId==-1)
		{
			FIFO* fi=(FIFO*)malloc(sizeof(FIFO));
			f->processId=currentprocess->process_id;
			f->framenumber=i;
			if (fifo_head->next_item==NULL)
			{
				fi->frame=i;
				fi->next_item=NULL;
				fifo_head->next_item=fi;
			}
			else
			{
				fif=fifo_head;
				while (fif!=NULL)
				{
					if (fif->next_item!=NULL)
					{
						fif=fif->next_item;
					}
					else
					{
						fi->frame=i;
						fi->next_item=NULL;
						fif->next_item=fi;
						break;
					}
				}
			}
			return i;
		}
		else
		{
			f=f->next_ft;
		}
	}
	return Useup;
}
/************************************************************************
SVC
The beginning of the OS502.  Used to receive software interrupts.
All system calls come to this point in the code and are to be
handled by the student written code here.
The variable do_print is designed to print out the data for the
incoming calls, but does so only for the first ten calls.  This
allows the user to see what's happening, but doesn't overwhelm
with the amount of data.
************************************************************************/
void    svc( SYSTEM_CALL_DATA *SystemCallData) {
	short               call_type;
	static short        do_print = 10;
	short               i;
	static INT32     Time;
	char process_name[16];
	char message_buffer[100];
	char* receive_buffer;
	char* data_written;
	long diskId;
	long sector;
	void *starting_address;
	INT32 address;
	INT32 *data;
	INT32* senderId;
	INT32 initial_priority;
	INT32 suspendId;
	INT32 changepriority;
	INT32 targetId;
	INT32 terminate;
	INT32 sourceId;
	INT32 changeprocessId;
	INT32 message_send_length;
	INT32* message_sendlength;
	INT32 message_recieve_length;
	INT32* processidreg;
	INT32* errorreg;
	INT32 count=100;
	INT32 LockResult;
	INT32 Temp;//stored the sleep time
	PCB* new_pcb=(PCB *)malloc(sizeof(PCB));
	call_type = (short)SystemCallData->SystemCallNumber;
	if ( do_print > 0 ) {
		printf( "SVC handler: %s\n", call_names[call_type]);
		for (i = 0; i < SystemCallData->NumberOfArguments - 1; i++ ){
			//Value = (long)*SystemCallData->Argument[i];
			//printf( "Arg %d: Contents = (Decimal) %8ld,  (Hex) %8lX\n", i,
			//(unsigned long )SystemCallData->Argument[i],
			//(unsigned long )SystemCallData->Argument[i]);
		}
	}
	do_print--;
	switch (call_type) { 
	case SYSNUM_GET_TIME_OF_DAY:   // This value is found in syscalls.h
		CALL(MEM_READ( Z502ClockStatus, &Time));
		*(INT32 *)SystemCallData->Argument[0]=Time;
		break;
	case SYSNUM_TERMINATE_PROCESS:
		terminate=(INT32)SystemCallData->Argument[0];
		errorreg=(INT32*)SystemCallData->Argument[1];
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		Terminate_Process(terminate,errorreg);
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		if (readyqueue_head->next_pcb!=NULL)
		{
			Dispatcher();	
		}
		else
		{
			//if readyqueue is NULL, then i need to use the "CAll" command to let the time increase which may result in one process wake 
			//up from timerqueue and move into readyqueue.
			if (timerqueue_head->next_pcb!=NULL)
			{
				MEM_WRITE(Z502TimerStart, &timerqueue_head->next_pcb->time);
				Z502Idle();
				while(readyqueue_head->next_pcb==NULL)
				{
					CALL(MEM_READ( Z502ClockStatus, &Time));
				}
				Dispatcher();	
			}
		}
		//if timerqueue and readyqueue are already NULL, then halt machine.
		if (timerqueue_head->next_pcb==NULL&&readyqueue_head->next_pcb==NULL)
		{
			Z502Halt();
		}
		break;
	case SYSNUM_GET_PROCESS_ID:
		strcpy(process_name,(char *)SystemCallData->Argument[0]);
		processidreg=(INT32 *)SystemCallData->Argument[1];
		errorreg=(INT32 *)SystemCallData->Argument[2];
		GET_Process_Id(process_name,processidreg,errorreg);
		break;
	case SYSNUM_CREATE_PROCESS:
		strcpy(process_name,(char *)SystemCallData->Argument[0]);
		starting_address=SystemCallData->Argument[1];
		initial_priority=(INT32)SystemCallData->Argument[2];
		processidreg=(INT32 *)SystemCallData->Argument[3];
		errorreg=(INT32 *)SystemCallData->Argument[4];
		new_pcb=Createprocess(process_name, starting_address,initial_priority,processidreg, errorreg);
		if (errorreg!=ERR_SUCCESS&&new_pcb)
		{	
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			Move_Process_to_ReadyQueue(new_pcb);
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			Scheduler_Printer(Time,"Create",new_pcb->process_id);
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		}
		break;
	case SYSNUM_SLEEP:
		Temp =(INT32)SystemCallData->Argument[0];
		CALL(MEM_READ( Z502ClockStatus, &Time));
		currentprocess->time=Temp+Time;
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		AddToTimerQueue(currentprocess);
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		Scheduler_Printer(Time,"Wait",currentprocess->process_id);
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		MEM_WRITE(Z502TimerStart, &Temp);
		//make sure there  no process run and then make Idle.
		if (readyqueue_head->next_pcb!=NULL)
		{
			Dispatcher();
		}
		else
		{
			Z502Idle();
			//if readyqueue is NULL, then i need to use the "CAll" command to let the time increase which may result in one process wake 
			//up from timerqueue and move into readyqueue.
			while (readyqueue_head->next_pcb==NULL)
			{
				CALL(MEM_READ( Z502ClockStatus, &Time));
			}
			if (readyqueue_head->next_pcb==NULL)
			{
				Z502Halt();
			}
			else
			{
				Dispatcher();
			}
		}
		break;
	case SYSNUM_SUSPEND_PROCESS:
		suspendId=(INT32 )SystemCallData->Argument[0];
		errorreg=(INT32 *)SystemCallData->Argument[1];
		CALL(MEM_READ( Z502ClockStatus, &Time));
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		Suspend_Process(suspendId,errorreg);
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		if (*errorreg==0)
		{
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			Scheduler_Printer(Time,"Suspend",suspendId);
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		}
		break;
	case SYSNUM_RESUME_PROCESS:
		suspendId=(INT32 )SystemCallData->Argument[0];
		errorreg=(INT32 *)SystemCallData->Argument[1];
		CALL(MEM_READ( Z502ClockStatus, &Time));
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		Resume_Process(suspendId,errorreg);
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		if (*errorreg==0)
		{
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
			Scheduler_Printer(Time,"Resume",suspendId);
			READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		}
		break;
	case SYSNUM_CHANGE_PRIORITY:
		changeprocessId=(INT32 )SystemCallData->Argument[0];
		changepriority=(INT32 )SystemCallData->Argument[1];
		errorreg=(INT32 *)SystemCallData->Argument[2];
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		Change_Process_Priority(changeprocessId,changepriority,errorreg);
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		break;
	case SYSNUM_SEND_MESSAGE:
		targetId=(INT32)SystemCallData->Argument[0];
		strcpy(message_buffer,(char*)SystemCallData->Argument[1]);
		message_send_length=(INT32)SystemCallData->Argument[2];
		errorreg=(INT32*)SystemCallData->Argument[3];
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_LOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		Send_Message_To_Process(targetId,message_buffer,message_send_length,errorreg);
		READ_MODIFY(MEMORY_INTERLOCK_BASE, DO_UNLOCK, SUSPEND_UNTIL_LOCKED,&LockResult);
		break;
	case SYSNUM_RECEIVE_MESSAGE:
		sourceId=(INT32)SystemCallData->Argument[0];
		receive_buffer=(char*)SystemCallData->Argument[1];
		message_recieve_length=(INT32)SystemCallData->Argument[2];
		message_sendlength=(INT32*)SystemCallData->Argument[3];
		senderId=(INT32*)SystemCallData->Argument[4];
		errorreg=(INT32*)SystemCallData->Argument[5];
		Receive_Process_Message(sourceId,receive_buffer,message_recieve_length,message_sendlength,senderId,errorreg);
		break;
	case SYSNUM_DISK_WRITE:
		diskId=(long)SystemCallData->Argument[0];
		sector=(long)SystemCallData->Argument[1];
		//memcpy(data_written,(char*)SystemCallData->Argument[2],16);
		data_written=(char*)SystemCallData->Argument[2];
		Disk_Write(diskId,sector,data_written);
		break;
	case SYSNUM_DISK_READ:
		diskId=(long)SystemCallData->Argument[0];
		sector=(long)SystemCallData->Argument[1];
		data_written=(char*)SystemCallData->Argument[2];
		Disk_Read(diskId,sector,data_written);
		break;
	default:  
		printf("ERROR!call_type not recognized!\n");
		printf("Call_type is - %i\n", call_type);
	}                                           
}                                               // End of svc
/**********************************
Initialize the Timer Queue
This is the routine to intialize my timerQ
Intialize  timerqueue_head's previous_pcb to null
Intialize  timerqueue_head's next_pcb to null
***********************************/
void InitTimerQueue()
{
	timerqueue_head=(PCB *)malloc(sizeof(PCB));
	timerqueue_head->previous_pcb=NULL;
	timerqueue_head->next_pcb=NULL;
}
/**********************************
Initialize the Ready Queue
This is the routine to intialize my ReadyQ
Intialize  readyqueue_head's previous_pcb to null
Intialize  readyqueue_head's next_pcb to null
***********************************/
void InitReadyQueue()
{
	readyqueue_head=(PCB *)malloc(sizeof(PCB));
	readyqueue_head->previous_pcb=NULL;
	readyqueue_head->next_pcb=NULL;
}
/**********************************
Initialize the Frametable
This is the routine to intialize my Frametable
Intialize  Frametable's previous_ft to null
Intialize  Frametable's next_ft to null
Intialize  Frametable's total frame to 0
and then intialize the 64 frame in frametable
***********************************/
void InitFrametable()
{
	Ft* f=(Ft *)malloc(sizeof(Ft));
	int i;

	frametable_head=(Ft *)malloc(sizeof(Ft));
	frametable_head->next_ft=NULL;
	frametable_head->previous_ft=NULL;
	frametable_head->processId=-1;
	frametable_head->time=0;
	frametable_head->totalframe=0;
	for ( i = 0; i < 64; i++)
	{
		Ft* frame=(Ft *)malloc(sizeof(Ft));
		frame->processId=-1;
		frame->time=0;
		if (frametable_head->next_ft==NULL)
		{
			frametable_head->next_ft=frame;
			frame->previous_ft=frametable_head;
			frametable_head->totalframe++;
			frame->next_ft=NULL;
		}
		else
		{
			f=frametable_head;
			while (f!=NULL)
			{
				if (f->next_ft!=NULL)
				{
					f=f->next_ft;
				}
				else
				{
					f->next_ft=frame;
					frame->previous_ft=f;
					frame->next_ft=NULL;
					frametable_head->totalframe++;
					break;
				}
			}
		}
	}
}
/**********************************
Initialize the FIFOQ
This is the routine to intialize my Frametable
Intialize  Frametable's frame to -1
Intialize  Frametable's next_ft to null
***********************************/
void InitFIFOQuqeqe(){
	fifo_head=(FIFO*)malloc(sizeof(FIFO));
	fifo_head->frame=-1;
	fifo_head->next_item=NULL;
}
/**********************************
Initialize the Diskqueue
This is the routine to intialize my Frametable
Intialize  Diskqueue's previous_pcb to null
Intialize  Diskqueue's next_pcb to null
Intialize  Diskqueue's diskinfo to null
***********************************/
void InitDiskqueue()
{
	diskqueue_head=(PCB*)malloc(sizeof(PCB));
	diskqueue_head->previous_pcb=NULL;
	diskqueue_head->next_pcb=NULL;
	diskqueue_head->diskinfo=NULL;
}
/**********************************
Initialize the ShadowPageTable
Intialize 1024 shadowpage's diskinfo to null
Intialize 1024 shadowpage's vpn to -1
***********************************/
void InitShadowPageTable()
{
	int i=0;
	for (i = 0; i < VIRTUAL_MEM_PAGES; i++)
	{
		shadowtpagetable[i]=(SPT*)malloc(sizeof(SPT));
		shadowtpagetable[i]->diskinfo=NULL;
		shadowtpagetable[i]->vpn=-1;
	}
}
/**********************************
Initialize the Suspend Queue
This is the routine to intialize my SuspendQ
Intialize  suspendqueue_head's previous_pcb to null
Intialize  suspendqueue_head's next_pcb to null
***********************************/
void InitSuspendQueue()
{
	suspendqueue_head=(PCB*)malloc(sizeof(PCB));
	suspendqueue_head->previous_pcb=NULL;
	suspendqueue_head->next_pcb=NULL;
}
/**********************************
Initialize the Message Queue
This is the routine to intialize my MessageQ
Intialize  private_message_head's next_msg to null
Intialize  private_message_head's previous_msg to null
Intialize  private_message_head's capacity to null
Intialize  broadcast_message_head's next_msg to null
Intialize  broadcast_message_head's previous_msg to null
Intialize  broadcast_message_head's capacity to null
***********************************/
void InitMessageQueue()
{
	private_message_head=(Msg*)malloc(sizeof(Msg));
	broadcast_message_head=(Msg*)malloc(sizeof(Msg));
	private_message_head->next_msg=NULL;
	private_message_head->previous_msg=NULL;
	private_message_head->capacity=0;
	broadcast_message_head->next_msg=NULL;
	broadcast_message_head->previous_msg=NULL;
	broadcast_message_head->capacity=0;
}
/************************************************************************
osInit
This is the first routine called after the simulation begins.  This
is equivalent to boot code.  All the initial OS components can be
defined and initialized here.
************************************************************************/
void    osInit( int argc, char *argv[]  ) {
	void                *next_context=NULL;
	INT32               i;
	InitTimerQueue();
	InitReadyQueue();
	InitSuspendQueue();
	InitMessageQueue();
	InitFrametable();
	InitFIFOQuqeqe();
	InitDiskqueue();
	InitShadowPageTable();
	printf( "Program called with %d arguments:", argc );
	for ( i = 0; i < argc; i++ )
		printf( " %s", argv[i] );
	printf( "\n" );
	printf( "Calling with argument 'sample' executes the sample program.\n" );
	TO_VECTOR[TO_VECTOR_INT_HANDLER_ADDR]   = (void *)interrupt_handler;
	TO_VECTOR[TO_VECTOR_FAULT_HANDLER_ADDR] = (void *)fault_handler;
	TO_VECTOR[TO_VECTOR_TRAP_HANDLER_ADDR]  = (void *)svc;
	if (( argc > 1 ) && ( strcmp( argv[1], "sample" ) == 0 ) ) {
		Z502MakeContext( &next_context, (void *)sample_code, KERNEL_MODE );
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE,&next_context );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test1a" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test1a,next_context,argv[1]);
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	}   
	if (( argc > 1 ) && ( strcmp( argv[1], "test1b" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test1b,next_context,argv[1]);
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	}   
	if (( argc > 1 ) && ( strcmp( argv[1], "test1c" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test1c,next_context,argv[1]);
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test1d" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test1d,next_context,argv[1]);
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test0" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test0,next_context,argv[1]);
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test1e" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test1e,next_context,argv[1]);
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test1f" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test1f,next_context,argv[1]);
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test1g" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test1g,next_context,argv[1]);
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test1h" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test1h,next_context,argv[1]);
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test1i" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test1i,next_context,argv[1]);
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test1j" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test1j,next_context,argv[1]);
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	}
	if (( argc > 1 ) && ( strcmp( argv[1], "test1k" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test1k,next_context,argv[1]);
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test1l" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test1l,next_context,argv[1]);
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test2a" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test2a,next_context,argv[1]);
		Maximumdisoutput=0;
			Diskoption=0;
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test2b" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test2b,next_context,argv[1]);
		Maximumdisoutput=0;
			Diskoption=0;
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test2c" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test2c,next_context,argv[1]);
		Maximumfaultoutput=10;

		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test2d" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test2d,next_context,argv[1]);
		Maximumfaultoutput=10;
		Maximumdisoutput=10;
		Diskoption=10;
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test2e" ) == 0 ) ) {
		void* currentcontext=os_make_process((void *)test2e,next_context,argv[1]);
		Maximummemoryoutput=10;
		Maximumfaultoutput=10;
		Maximumdisoutput=10;
			Diskoption=10;
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	} 
	if (( argc > 1 ) && ( strcmp( argv[1], "test2f" ) == 0 ) ) {
		
		void* currentcontext=os_make_process((void *)test2f,next_context,argv[1]);
		Maximummemoryoutput=10;
		Maximumfaultoutput=10;
		Maximumdisoutput=0;
			Diskoption=0;
		Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &currentcontext );
	
	}   
	/*  This should be done by a "os_make_process" routine, so that
	test0 runs on a process recognized by the operating system.    */
}                                               // End of osInit
