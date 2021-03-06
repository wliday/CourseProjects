/*
*    Title:                 线程的创建、撤销、先来先服务调度、时间片轮转。
*    Created on：           2012-4-17
*    @author：              酷~行天下
*    @mail:                 mindlee@me.com
*    @homepage              http://mindlee.net
*/
#include<dos.h>
#include<stdlib.h>
#include<stdio.h>

#define FINISHED 0
#define RUNNING 1
#define READY 2
#define BLOCKED 3
#define NTCB 10
#define TL 10
#define GET_INDOS 0x34
#define GET_CRIT_ERR 0x5D06

typedef void (far *codeptr)( void);

void InitTcb(void);
int DosBusy(void);
void Init_TCB(void);
int create(char *name, codeptr code, int stck);
int find_ready_thread();
void interrupt my_swtch(void );
void interrupt new_int8(void );
void interrupt (*old_int8)(void );
void destroy(int id);
void over(void);
void tcb_state(void);
void print(char ch);
int finished();

int current = -1;/*记录正在运行的现场的内部标识符*/
int timecount;/* 记录此线程运行了多久  */
char far *indos_ptr = 0; /*INDOS标志地址*/
char far *crit_err_ptr = 0; /*严重错误标志地址*/

/* TCB */
struct TCB {
       unsigned char *stack;/*线程堆栈起始地址*/
       unsigned ss;/* 堆栈段址  */
       unsigned sp;/* 堆栈指针  */
       char name[10];/* 外部标识符  */
       char state;/* 线程状态  */
} tcb[NTCB];

/* 堆栈信息  */
struct int_regs {
       unsigned bp, di, si, ds, es, dx, cx, bx, ax, ip, cs, flags, off, seg;
};

/* 获得INDOS标志的地址和严重错误标志的地址  */
void InitInDos(void) {
       union REGS regs;
       struct SREGS segregs;
       regs.h.ah = GET_INDOS;
       intdosx (&regs, &regs, &segregs);
       indos_ptr = MK_FP(segregs.es, regs.x.bx);
       if (_osmajor < 3) {
             crit_err_ptr = indos_ptr + 1;
       } else if (_osmajor == 3 && _osmajor == 0) {
             crit_err_ptr = indos_ptr - 1;
       } else {
             regs.x.ax = GET_CRIT_ERR;
             intdosx(&regs,&regs,&segregs);
             crit_err_ptr = MK_FP(segregs.ds,regs.x.si);
       }
}

/* 判断DOS是否忙 */
/* 如果返回1，表示DOS忙 */
/* 如果返回0，表示DOS不忙 */
/* 如果返回-1，表示还没有调用InitDos()  */
int DosBusy(void) {
       if(indos_ptr && crit_err_ptr) {
             return (*indos_ptr || *crit_err_ptr);
       } else {
             return -1;
       }
}

/* 初始化TCB  */
void Init_TCB(void) {
       int i;
       for (i = 0; i < NTCB; i++) {
             tcb[i].name[0] = '\0' ;
             tcb[i].state = FINISHED;
             tcb[i].stack = NULL;
       }
}

/* 创建线程  */
int create(char *name, codeptr code, int stck) {
       struct int_regs far *r;
       int i, id = -1;

       for(i = 0; i < NTCB; i++) { /* 找到第一个空线程  */
             if(tcb[i].state == FINISHED) {
                   id = i;
                   break;
             }
       }

       if(id == -1) { /* 没找到  */
             return -1;
       }

       disable();
       /* 栈顶是低地址，栈低是高地址，存数据是从栈底到栈顶，也就是从高地址到低地址 */
       /* 下面这句的stack是指向栈顶的（低地址） */
       tcb[id].stack = (unsigned char *)malloc(stck);
       /* 下面这句，r是一个指向int_reg类型的指针，int_reg类型的结构体从栈底开始存（所以是tcb[i].stack + stck） */
       r = (struct int_regs *)(tcb[id].stack + stck);
       /* r此时指向栈底（实际含义是表示，此时栈是空的，因为r指向栈底） */
       /* r--意思是，指针从栈底向下移一个位，指向int_reg这个结构体, 如果不r--, 此时r指向栈底，没法存数据。 */
       r--;
       tcb[id].ss = FP_SEG(r);
       tcb[id].sp = FP_OFF(r);
       r->cs = FP_SEG(code);
       r->ip = FP_OFF(code);
       r->ds = _DS;
       r->es = _DS;
       r->flags = 0x200;
       r->seg = FP_SEG(over);
       r->off = FP_OFF(over);
       tcb[id].state = READY;
       for(i = 0; i < 10; i++, name++) {
             if((tcb[id].name[i] = *name) == NULL) {
                   break;
             }
       }
       tcb[id].name[9] = '\0' ;
       printf("Create Thread %d, name is %s.\n" , id, tcb[id].name);
       enable();
       return 0;
}

/*寻找就绪线程*/
int find_ready_thread() {
       int i;
       for (i = current + 1; i < NTCB; i++) {
             if (READY == tcb[i].state) {
                   return i;
             }
       }
       for (i = 0; i < current; i++) {
             if (READY == tcb[i].state) {
                   return i;
             }
       }
       if (i == current) {
             return -1;
       }
}

/*线程执行完毕的线程调度*/
void interrupt my_swtch() {
       int target_thread = find_ready_thread();
       if (target_thread < 0) {
             return ;
       }
       if (0 == target_thread) puts("\n\n" ); /*控制下输出格式*/

       disable();

       tcb[current].ss = _SS;
       tcb[current].sp = _SP;
       if(tcb[current].state == RUNNING) {
             tcb[current].state = READY;
       }

       _SS = tcb[target_thread].ss;
       _SP = tcb[target_thread].sp;
       tcb[target_thread].state = RUNNING;
       timecount = 0;
       printf("\nSwitch Thread %d(%s) to Thread %d(%s).\n" ,
             current, tcb[current].name, target_thread, tcb[target_thread].name);
       current = target_thread;

       enable();
}

/* 时间片引起的调度  */
void interrupt new_int8(void ) {
       (*old_int8)();
       if(++timecount < TL || DosBusy()) {
             return;
       }
       my_swtch();
}

/* 销毁线程  */
void destroy(int id) {
       disable();

       free(tcb[id].stack);
       tcb[id].stack = NULL;
       tcb[id].state = FINISHED;
       printf("Thread %d is destroyed, name is %s.\n" , id, tcb[id].name);

       enable();
}

/* 结束旧线程，调度新线程  */
void over(void) {
       destroy(current);
       my_swtch();
}

/*输出所有线程的状态 */
void tcb_state(void) {
       int i;
       puts("" );
       for (i = 0; i < NTCB; i++) {
             if (FINISHED == tcb[i].state ) {
                   printf("Thread %d is FINISHED.\n" , i);
             } else if (RUNNING == tcb[i].state) {
                   printf("Thread %d is RUNNING.\n" , i);
             } else if (READY == tcb[i].state) {
                   printf("Thread %d is READY.\n" , i);
             } else if (BLOCKED == tcb[i].state) {
                   printf("Thread %d is BLOCKED.\n" , i);
             }
       }
}      

/*除0号线程以外其他线程是否都已经运行完成*/
int finished() {
       int i;
       for (i = 1; i < NTCB; i++) {
             if (FINISHED != tcb[i].state) {
                   return 0;
             }
       }
       return 1;
}

/* 打印辅助函数  */
void print(char ch) {
       int i, j, k;
       for (i = 0; i < 70; i++) {
             printf("%c" , ch);
             for (j = 0; j < 3000; j++) /* 延迟  */
                   for (k = 0; k < 3000; k++);
       }
       puts("" );
}

void fun1(void) {
       puts("I'm fun1(), I want to output AAA." );
       print('A' );
}

void fun2(void) {
       puts("I'm fun2(), I want to output BBB." );
       print('B' );
}

void fun3(void) {
       puts("I'm fun3(), I want to output CCC." );
       print('C' );
}

int main() {
       InitInDos();
       Init_TCB();

       current = 0;
       timecount = 0;
       old_int8 = getvect(8);

       strcpy(tcb[0].name,"main" );
       tcb[0].state = RUNNING;


       create("fun1" , fun1, 1024);
       create("fun2" , fun2, 1024);
       create("fun3" , fun3, 1024);
       tcb_state();

       setvect(8, new_int8);
       my_swtch();

       while (!finished());
       tcb[0].name[0] = '\0' ;
       tcb[0].state = FINISHED;
       setvect(8, old_int8);

       tcb_state();
       printf("Press any key to exit!\n" );
       getchar();
       return 0;
}
/*
Create Thread 1, name is fun1.
Create Thread 2, name is fun2.
Create Thread 3, name is fun3.

Thread 0 is RUNNING.
Thread 1 is READY.
Thread 2 is READY.
Thread 3 is READY.
Thread 4 is FINISHED.
Thread 5 is FINISHED.
Thread 6 is FINISHED.
Thread 7 is FINISHED.
Thread 8 is FINISHED.
Thread 9 is FINISHED.

Switch Thread 0(main) to Thread 1(fun1).
I'm fun1(), I want to output AAA.
AAAAAAAAAAAAAAA
Switch Thread 1(fun1) to Thread 2(fun2).
I'm fun2(), I want to output BBB.
BBBBBBBBBBBBBBBBB
Switch Thread 2(fun2) to Thread 3(fun3).
I'm fun3(), I want to output CCC.
CCCCCCCCCCCCCCCC



Switch Thread 3(fun3) to Thread 0(main).

Switch Thread 0(main) to Thread 1(fun1).
AAAAAAAAAAAAAAAA
Switch Thread 1(fun1) to Thread 2(fun2).
BBBBBBBBBBBBBBB
Switch Thread 2(fun2) to Thread 3(fun3).
CCCCCCCCCCCCCCCCC



Switch Thread 3(fun3) to Thread 0(main).

Switch Thread 0(main) to Thread 1(fun1).
AAAAAAAAAAAAAAAA
Switch Thread 1(fun1) to Thread 2(fun2).
BBBBBBBBBBBBBBBB
Switch Thread 2(fun2) to Thread 3(fun3).
CCCCCCCCCCCCCCCC



Switch Thread 3(fun3) to Thread 0(main).

Switch Thread 0(main) to Thread 1(fun1).
AAAAAAAAAAAAAAAA
Switch Thread 1(fun1) to Thread 2(fun2).
BBBBBBBBBBBBBBBB
Switch Thread 2(fun2) to Thread 3(fun3).
CCCCCCCCCCCCCCC



Switch Thread 3(fun3) to Thread 0(main).

Switch Thread 0(main) to Thread 1(fun1).
AAAAAAA
Thread 1 is destroyed, name is fun1.

Switch Thread 1(fun1) to Thread 2(fun2).
BBBBBB
Thread 2 is destroyed, name is fun2.

Switch Thread 2(fun2) to Thread 3(fun3).
CCCCCC
Thread 3 is destroyed, name is fun3.




Switch Thread 3(fun3) to Thread 0(main).

Thread 0 is FINISHED.
Thread 1 is FINISHED.
Thread 2 is FINISHED.
Thread 3 is FINISHED.
Thread 4 is FINISHED.
Thread 5 is FINISHED.
Thread 6 is FINISHED.
Thread 7 is FINISHED.
Thread 8 is FINISHED.
Thread 9 is FINISHED.
Press any key to exit!

*/  
