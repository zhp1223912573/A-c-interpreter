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



/*
标识符，也就是一个变量名，在词法分析过程中，我们不关心变量的名称是什么，我们只关心这个变量名代表的唯一标识。
（例如 int a; 定义了变量 a，而之后的语句 a = 10，我们需要知道这两个 a 指向的是同一个变量。）
在词法分析过程中会将标识符加入symbol table内，对于已经出现过的标识符，直接返回。
struct identifier {
    int token;  标识符返回的标记，理论上所有的变量返回的标识符都是id，但是还存在if，else，return等关键字，他们也存在对于的标记
    int hash; 标识符的hash值，用于标识符的快速比较
    char * name; 标识符本身字符串
    int class; 标识符类别，如数字，全局变量，局部变量
    int type; 标识符的类型，即是个变量时，是int还是char
    int value;  标识符的值，如果是个函数，存放的是函数的地址
    int Bclass; 下述三个B***用于区别全局标识符和局部标识符，当局部标识符的名字与全局标识符相同时，用作保存全局标识符的信息。
    int Btype;
    int Bvalue;
}*/
int token_val;      //当前标识符的值,主要用于识别数值
int *current_id,     //当前解析的标识符
    *symbols;        //符号表
int  *idmain;       //main函数入口


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


/*符号表中，标识符的域*/
enum{
    Token,Hash,Name,Class,Type,Value,Bclass,Btype,Bvalue,IdSize
};

/*变量和函数的类型*/
enum{
    CHAR,INT,PTR
};

//用于词法分析，获取下一个标记，自动忽略空白字符
void next(){
    char *last_pos;
    int hash;

    //开始解析源码
    while(token=*src){
        ++src;

        /*跳过换行符*/
        if(token == '\n'){
            line++;
        }
        /*不支持宏定义，直接跳过*/
        else if(token == '#'){
            while(*src!=0 || *src!='\n'){
                src++;
            }
        }
        /*读取变量，变量名只能以a-z，A-Z，_开头*/
        else if((token>='a'&&token<='z')||(token>='A'&&token<='Z')||(token=='_')){
            //记录起始位置
            last_pos = src-1;
            hash = token;

            //读取整个变量
            while((*src>='a'&&*src<='z')||(*src>='A'&&*src<='Z')||(*src=='_')||(*src>='0'&&*src<='9')){
                hash = hash*147 + *src;
                src++;
            }

            //在符号表中进行线性检测，如果存在，直接返回
            current_id = symbols;
            while(current_id[Token]){
                //hash值一致且标识名一致，存在并返回
                if(current_id[Hash]==hash && memcmp((char*)current_id[Name],last_pos,src-last_pos)){
                    token  = current_id[Token];
                    return;
                }
                //移动到下一个标识符
                current_id += IdSize;
            }

            //符号表中不存在当前标识，说明当前标识第一次出现，记录到符号表中
            current_id[Hash] = hash;
            current_id[Name] = (int)last_pos;
            token = current_id[Token] = Id;
            return ;
            
        }
        /*识别数字 
        识别十进制，八进制，十六进制三种格式数值*/
        else if(token>='0'&&token<='9'){

            token_val = token - '0';

            if(token_val>0){//十进制，1-9开头
                while(*src>='0'&&*src<='9'){
                    token_val = token_val*10 + *src++ - '0';
                }
            }
            //以0开头，不是八进制就是十六进制
            else{
                if(*src=='x'||*src=='X'){//十六进制
                    token = *++src;
                    while((token>='0'&&token<='9')||(token>='a'&&token<='f')||(token>='A'&&token<='F')){
                        //这里的（token&15）获取16进制的个位，token>='A'用于判断是超出A，超出需要加9
                        //尝试带入'A'（41）或者'a'（61）就可以理解这里的含义。
                        token_val = token_val*16 + (token&15) + (token>='A'?9:0);
                        token = *++src;
                    }
                }else{//八进制
                    token = *src;
                    while(token>='0'&&token<='7'){
                        token_val = token_val*8 + token-'0';
                        token = *++src;
                    }
                }
            }
            token = Num;
            return ;
        }
        /*字符串*/
        else if(token=='"'||token=='\''){
            //data用于保存字符型变量
            last_pos = data;//保存字符串起始位置
            while(*src!=0&&*src!=token){
                //仅支持'\n'这种转义字符
                token_val = *src++;
                if(token_val = '\\'){
                    token_val = *src++;
                    if(token_val = 'n'){
                        token_val = '\n';
                    }
                }

                if(token=='"'){
                    *data++ = token_val;//如果是字符串类型，将字符串保存到data中
                }
            }

            src++;//跳过最后的',"
            if(token == '\''){//如果是单字符类型，我们把它视作Num类型
                token = Num;
            }else{
                token_val = (int) last_pos;
            }
            return ;
        }
        /*注释*/
        else if(token=='/'){
            if(*src=='/'){
                //跳过注释
                while(*src!=0&&*src!='\n'){
                    src++;
                }
            }else{
                //除法符号
                token = DIV;
                return;
            }
        }
        /*其他一些基本符号*/
        else if(token=='+'){
            if(*src == '+'){
                src++;
                token = Inc;
            }else{
                token = Add;
            }
            return ;
        }
        else if(token=='-'){
            if(*src == '-'){
                src++;
                token = Dec;
            }else{
                token = Sub; 
            }
            return ;
        }
        else if(token=='='){
             if(*src == '='){
                src++;
                token = Eq;
            }else{
                token = Assign; 
            }
            return ;
        }
        else if(token=='!'){
             if(*src == '='){
                src++;
                token = Ne;
            }
            return ;
        }
        else if(token =='<'){
            if(*src=='<'){
                src++;
                token = Shl;
            }else if(*src=='='){
                src++;
                token = Le;
            }else{
                token = Lt;
            }
            return ;
        }
        else if(token =='>'){
            if(*src=='>'){
                src++;
                token = Shr;
            }else if(*src=='='){
                src++;
                token = Ge;
            }else{
                token = Gt;
            }
            return ;
        }
        else if(token =='|'){
            if(*src=='|'){
                src++;
                token = Lor;
            }else{
                token = Or;
            }
            return ;
        }
        else if(token =='&'){
            if(*src=='&'){
                src++;
                token = Lan;
            }else{
                token = And;
            }
            return ;
        }
         else if (token == '^') {
            token = Xor;
            return;
        }
        else if (token == '%') {
            token = Mod;
            return;
        }
        else if (token == '*') {
            token = Mul;
            return;
        }
        else if (token == '[') {
            token = Brak;
            return;
        }
        else if (token == '?') {
            token = Cond;
            return;
        }
         else if (token == '~' || token == ';' || token == '{' || token == '}' || token == '(' || token == ')' || token == ']' || token == ',' || token == ':') {
            // 直接作为标记返回
            return;
        }
    }
   
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

    //特殊关键字符无法作为标识符进行分析，需要特别处理,在语法分析前我们将其添加加入字符表
    //这样在源代码中出现关键字后，由于我们提前添加这些字符进入符号表，我们就知道他们是特殊关键字
    src = "char else enum if int return sizeof while "
          "open read close printf malloc memset memcmp exit void main";

    //添加关键字
    i = Char;
    while(i<=While){
        next();
        current_id[Token] = i++;
    }

    //添加本地库函数调用
    i = OPEN;
    while(i<EXIT){
        next();
        current_id[Class] =Sys;
        current_id[Type] = INT;
        current_id[Value] = i++;
    }

    next(); current_id[Token] = Char; // handle void type
    next(); idmain = current_id; // keep track of main


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

    src[i] = 0;//添加结束符
    close(fd);

    program();
    return eval();
}