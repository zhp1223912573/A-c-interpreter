#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<memory.h>

#define int long long

int token;        //指向当前代码中的token
char *src,*old_src; //指向源代码字符串
int poolsize;       //模拟的text/data/stack大小
int line;           //行号

int *text,      //代码段，存放代码
    *old_text,  //for dump text segement
    *stack;     //栈，处理函数调用过程中的相关数据，如栈帧或者局部变量
char *data;     //数据段，存放初始化后的数据，保存都是字符型

/*虚拟机寄存器
    通过下四个寄存器保存当前计算机的运行状态
*/
int *pc,        //程序计数器，记录下一条指令执行地址
    *sp,        //栈指针寄存器，指向当前栈栈顶
    *bp,        //基址寄存器,指向当前栈中的某个位置，在函数调用中使用
    ax,         //普通寄存器，存放一条指令执行后的结果
    cycle;

/*虚拟机指令集
    带有参数的指令在前，不带的在后
*/
enum { LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,
       OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,
       OPEN,READ,CLOS,PRTF,MALC,MSET,MCMP,EXIT 
};


//支持的标记和类别，按照优先级先后排序
enum {
  Num = 128, Fun, Sys, Glo, Loc, Id,
  Char, Else, Enum, If, Int, Return, Sizeof, While,
  Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak
};


//用于词法分析，获取下一个标记，自动忽略空白字符
void next(){
    char *last_pos;
    int hash;

    token = *src++;
   
    return ;
}


//语法分析的入口，分析整个c语言程序
void program(){
    next();
    while(token>0){
        printf("token is: %c\n",token);
        next();
    }
}

//解析表达式
void expression(int level){

}

//虚拟机入口，解释目标代码
int eval(){
    //读取指令
    int op,*tmp;
    //循环次数
    int cycle = 0;
    while(1){
        cycle++;
        op = *pc++;

        /*load&store*/
        //将pc中的值读入ax
        if(op==IMM) ax = *pc++;   
        //加载ax中保存地址指向的int型值到ax    
        else if(op==LI) ax = *(int*)ax; 
        //加载ax中保存地址指向的char型值到ax
        else if(op==LC) ax = *(char*)ax;
        //加载ax中int型值到栈顶保持的地址中
        else if(op==SI) *(int*)*sp = ax;
        //加载ax中char型值到栈顶保持地址中
        else if(op==SC) *(char*)*sp = ax;
        //将ax中值放入栈顶
        else if(op==PUSH) *--sp = ax; 
        //在子函数中需要获取传递的参数时使用，获取距离当前子函数栈帧基址地址偏移量位置的址，可能是在进入当前子函数前提前压入栈中的参数
        else if(op==LEA){
            ax = (int)(bp + *pc++);
        }

        /*跳转指令*/
        //跳转到下一条指令   
        else if(op==JMP) pc = (int*)*pc;
        //当ax为0时跳转
        else if(op==JZ)  pc = ax ? pc+1:(int*)*pc;
        //当ax不为0时跳转
        else if(op==JNZ) pc = ax ? (int*)*pc:pc+1;

        /*函数调用指令*/
        //call函数调用，在栈中保存下一条指令地址，同时跳转到当前pc指向的位置
        else if(op==CALL) {
            *--sp = (int)(pc+1);//调用子函数时，先保持下一指令地址到栈中
            pc = (int*)*pc;//pc更新为call的参数，也就是子函数的代码地址，指令执行流切换成功
        }
        //ENT<size>  创建一个新栈帧，在一个函数被调用后使用.保存上一栈帧的栈帧地址，更新bp，预留出一定size空间给变量
        else if(op==ENT){
            *--sp = (int)bp; //保持父亲函数的栈帧基址地址
            bp = sp;        //更新bp，当前sp指向位置地址就是新函数的栈帧地址
            sp = sp - *pc++;//预留处当前函数临时变量需要保存的地址空间
        }
        //ADJ<size> 回收栈空间，在函数调用返回后，将子函数调用过程前，子函数所需要的参数空间，即压入栈中的args清除
        else if(op==ADJ){
            sp = sp+ *pc++;    //栈帧跳过传递参数个数
        }
        //LEV 使用该指令替代RET 在函数执行完成后调用该指令，返回到调用该函数的父函数执行指令的下一条指令
        else if(op==LEV){
            sp = bp;//sp指向当前栈帧的基质地址处，该地址存放上一栈帧的地址
            bp = (int*)*pc++;//更新bp，指向父函数的栈帧基址处
            pc = (int*)*pc++;//pc更新为调用子函数时下一条指令的地址，至此，指令执行流回归正常。
        }


        /*运算符指令*/
        else if (op == OR)  ax = *sp++ | ax;
        else if (op == XOR) ax = *sp++ ^ ax;
        else if (op == AND) ax = *sp++ & ax;
        else if (op == EQ)  ax = *sp++ == ax;
        else if (op == NE)  ax = *sp++ != ax;
        else if (op == LT)  ax = *sp++ < ax;
        else if (op == LE)  ax = *sp++ <= ax;
        else if (op == GT)  ax = *sp++ >  ax;
        else if (op == GE)  ax = *sp++ >= ax;
        else if (op == SHL) ax = *sp++ << ax;
        else if (op == SHR) ax = *sp++ >> ax;
        else if (op == ADD) ax = *sp++ + ax;
        else if (op == SUB) ax = *sp++ - ax;
        else if (op == MUL) ax = *sp++ * ax;
        else if (op == DIV) ax = *sp++ / ax;
        else if (op == MOD) ax = *sp++ % ax;

    /*为了实现自举，需要实现printf，但直接实现一个printf比较繁琐，与我们的目标背道而驰
    这里直接调用本地函数
    */
   else if (op == EXIT) { printf("exit(%d)", *sp); return *sp;}
    else if (op == OPEN) { ax = open((char *)sp[1], sp[0]); }
    else if (op == CLOS) { ax = close(*sp);}
    else if (op == READ) { ax = read(sp[2], (char *)sp[1], *sp); }
    else if (op == PRTF) { tmp = sp + pc[1]; ax = printf((char *)tmp[-1], tmp[-2], tmp[-3], tmp[-4], tmp[-5], tmp[-6]); }
    else if (op == MALC) { ax = (int)malloc(*sp);}
    else if (op == MSET) { ax = (int)memset((char *)sp[2], sp[1], *sp);}
    else if (op == MCMP) { ax = memcmp((char *)sp[2], (char *)sp[1], *sp);}

    //未知指令
    else {
        printf("unknown instruction:%d\n", op);
        return -1;
    }
    }
    return 0;
}

int main(int argc,char** argv){
    int i,fd;

    argc--;
    argv++;

    poolsize = 256*1024;    //自定义大小
    line = 1;

    //打开要读取读取的文件
    if((fd=open(*argv,0))<0){
        printf("can't open file(%s)\n",*argv);
        return -1;
    }

    //分配内存
    if(!(src=old_src=malloc(poolsize))){
        printf("can't malloc(%d) for source code\n",poolsize);
        return -1;
    }

    //读入文件源代码
    if((i=read(fd,src,poolsize-1))<0){
        printf("read() return %d\n",i);
        return -1;
    }

    src[poolsize] = 0;//添加结束符
    close(fd);

    //为虚拟机设置内存（通过设置虚拟机实现自己的指令集，也就是汇编语言，该指令集作为编译器输出的目标代码）
    if(!(text=old_text=malloc(poolsize))){
        printf("can't malloc(%d) for text segement",poolsize);
        return -1;
    }

      if(!(data=malloc(poolsize))){
        printf("can't malloc(%d) for data segement",poolsize);
        return -1;
    }

    if(!(stack=malloc(poolsize))){
            printf("can't malloc(%d) for stack segement",poolsize);
            return -1;
    }

    //初始化三个区域
    memset(text,0,poolsize);
    memset(data,0,poolsize);
    memset(stack,0,poolsize);

    //初始化寄存器
    sp =bp =(int*)((int)stack+poolsize);
    ax = 0;

    /*测试虚拟机功能*/
    i = 0;
    text[i++] = IMM;
    text[i++] = 10;
    text[i++] = PUSH;
    text[i++] = IMM;
    text[i++] = 20;
    text[i++] = ADD;
    text[i++] = PUSH;
    text[i++] = EXIT;
    pc = text;

    program();
    return eval();
}