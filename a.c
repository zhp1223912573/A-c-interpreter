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

int basetype;       //基本类型
int expr_type;      //表达式类型

int index_of_bp;     //bp指针在栈上的索引

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

//匹配当前标记是否正确
void match(int tk){
    if(token==tk){
        next();
    }else{
        printf("行(%d):预期为(%d),实际为(%d)\n",line,tk,token);
        exit(-1);
    }
}

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
                        //尝试带入'A'（65）或者'a'（97）就可以理解这里的含义。
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

//enum解析
void enum_declaration(){
    //enum [id] {a=1,b=2,c=3}
    int i;
    i = 0;
    while(token!='}'){
        //标识符声明出错
        if(token!=Id){
            printf("行%d:enum标识符声明出错!",line);
            exit(-1);
        }

        next();
        if(token==Assign){
            next();
            if(token!=Num){
                printf("行%d:enum标识符赋值非数值!",line);
                exit(-1);
            }
            i = token_val;//保存读取的数值
            next();
        }

        current_id[Class] = Num;
        current_id[Type]  = INT;
        current_id[Value] = i;

        if(token==','){
            next();
        }

    }
}

//函数参数解析
void function_parameter(){
    //type func ( ... ) {...}
    //          |解析这|
    int type;
    int params;
    params = 0;

    while(token!=')'){
        //int name..
        type = INT;
        //类型匹配
        if(token== Int){
            match(Int);
        }else if(token==Char){
            type = CHAR;
            match(Char);
        }

        //可能为指针类型
        while(token==Mul){
            type = type + PTR;
            match(Mul);
        }

        //匹配变量名标识符
         if(token!=Id){
            printf("行%d: 变量声明出现错误!\n",line);
            exit(-1);
        }

        //如果符号表中该标识符类型已经存在，说明当前全局声明出现重复
        if(current_id[Class]==Loc){
            printf("行%d: 出现重复局部变量声明！",line);
            exit(-1);
        }

        match(Id);

        //保存本地变量
        current_id[Bclass] = current_id[Class];
        current_id[Class] = Loc;
        current_id[Btype] = current_id[Type];
        current_id[Type] = type;
        current_id[Bvalue] = current_id[Value];
        current_id[Value] = params++;//存放参数位置

        if(token==','){
            match(',');
        }

    }
    index_of_bp = params+1;
}

//函数体接卸
void function_body(){
    //type funcname (...) {...}
    //                    |解析这|

    //{
    // 1.local declaration
    // 2.statement
    //}

    int pos_local ;    //局部变量在栈帧中的位置
    int type;
    pos_local = index_of_bp;

    while(token==Int || token==Char){
        basetype = (token==Int) ? INT:CHAR;
        match(token);

        while(token!=';'){
            type = basetype;
            while(token==Mul){
                match(Mul);
                type = type + PTR;
            }

            if(token!=Id){
                printf("行%d:局部变量声明出错!",line);
                exit(-1);
            }

            //当前符号表中的标识符是局部变量，说明出现重复局部变量
            if(current_id[Type]==Loc){
                printf("行%d:局部变量重复声明!",line);
                exit(-1);
            }

            match(Id);
            
            //保存局部变量，可能存在同名的全局变量，将符号表中全局变量的域用正常域进行填充，
            //后续离开函数时再恢复，保证了局部变量在函数内对全局变量的覆盖
            current_id[Bclass] = current_id[Class];
            current_id[Class] = Loc;
            current_id[Btype] = current_id[Type];
            current_id[Type] = type;
            current_id[Bvalue] = current_id[Value];
            current_id[Value] = ++pos_local;   //当前参数的索引

            if(token==','){
                match(',');
            }
        }

        match(';');
    }

    //保存局部变量在栈帧中的大小
    *text = ENT;
    *text = pos_local - index_of_bp;

    //进行语句分析
    while(token!='}'){
        statement();
    }

    //离开函数
    *text = LEV;
}

/**
 * 
 * int demo(int param_a, int *param_b) {
    int local_1;
    char local_2;

    ...
}
上述函数被调用时栈中状态
 |    ....       | high address
+---------------+
| arg: param_a  |    new_bp + 3
+---------------+
| arg: param_b  |    new_bp + 2
+---------------+
|return address |    new_bp + 1
+---------------+
| old BP        | <- new BP
+---------------+
| local_1       |    new_bp - 1
+---------------+
| local_2       |    new_bp - 2
+---------------+
|    ....       |  low address

*/
//函数声明解析
void function_declaration(){
    //type func_name (...) {...}

    match('(');
    function_parameter();
    match(')');
    match('{');
    function_body();
    //match('}');  不在此处完成函数体的读取，否则外层的循环无法正确检验到函数体的结束，所以我们将结束符号'}'的解析交给外出循环

    //完成函数体的编译，我们需要将在编译过程中被同名局部变量覆盖的全局变量进行恢复
    current_id = symbols;
    //扫描一遍符号表
    while(current_id[Token]){
        if(current_id[Class]==Loc){//发现局部变量
            current_id[Class] = current_id[Bclass];
            current_id[Type]  = current_id[Btype];
            current_id[Value] = current_id[Bvalue];
        }
        current_id = current_id + IdSize;
    }
}

//语句分析 语句=表达式+;
void statement(){

    // 1. if (...) <statement> [else <statement>]
    // 2. while (...) <statement>
    // 3. { <statement> }
    // 4. return xxx;
    // 5. <empty statement>;
    // 6. expression; (expression end with semicolon)


    int *a,*b;//跳转地址



    //if语句分析
    /**
      if (...) <statement> [else <statement>]

    if (<cond>)                   <cond>
                                 JZ a
    <true_statement>   ===>     <true_statement>
    else:                        JMP b
    a:                           a:
    <false_statement>           <false_statement>
    b:                           b: 

    if语句条件成立就进入true_statement中进行执行，
    不成立需要进入false_statement中，也就是JZ a
    同时，如果true_statement执行完成，为了避免顺序执行false_statement
    需要跳转到最后，也就是JMP b
    */
   if(token==If){

        match(If);
        match('(');
        expression(Assign); //解析条件
        match(')');

        *++text = JZ;   //按照上述分析，加入JZ
        b = ++text;     //保存JZ指令后一位置，等待后续语句解析完成，在该位置填入if语句结束的位置，这样才能在条件不满出时跳出if语句

        statement();    //递归解析

        if(token==Else){
            match(Else);

            //填入JMP b
            *b = (int)(text+3); //这里先填入上述讲解中的a：,也就是false_statement前的a:
            *++text = JMP;        //填入JMP
            b = ++text;         //同样先指向JMP指令后一位置，因为我们还不知道要跳转到的b位置的具体地址，需要等待else中的语句解析完成 

            statement();        //递归解析
        }

        *b = (int)(text+1);     //写入上文预留的填写b位置地址空间
   }

   /**while语句
    * 
   a:                    a:
   while (<cond>)        <cond>
                         JZ b
    <statement>          <statement>
                         JMP a
    b:                   b:
    * 
   */
    else if(token==While){
        match(While);
        a = text+1;     //保留a位置指针

        mathch('(');
        expression(Assign);
        match(')');

        *++text = JZ;
        b = ++text;     //预留JZ参数位置

        statement();    //递归解析

        *++text = JMP;    //写入JMP a
        *++text = (int)a;  

        *b = (int)(text+1); //写入JZ的b
    }

    /**return 语句
     * 遇到return意味函数退出，需要写入LEV指令
    */
   else if(token==Return){
        match(Return);

        if(token!=';'){
            expression(Assign);
        }

        match(";");

        *++text = LEV;
   }

    /**
     {<statement> }
    */
   else if(token=='{'){
        match('{');
        while(token!='}'){
            statement();
        }
        match('}');
   }
   /**
    * <empty statement>
   */
   else if(token==';'){
        match(';');
   }
    /**
     a=b 或者函数调用
    */
   else{
        expression(Assign);
        match(';');
   }

}
//全局声明分析
void global_declaration(){
    /*
    global_declaration ::= function_decl | enum_decl | variable_enum

    funtion_decl ::= type {'*'} id '('  parameter_decl ')' '{' body_decl '}'

    enum_decl    ::= enum [id] '{' id ['=' 'num'] {, id ['=' 'num']}  '}'

    variable_decl::= type {'*'} id {',' {'*'} id} ';'  
    */

   int type;    //变量实际类型
   int i;       //临时变量

   basetype = INT;

    //Enum标记单独处理
   if(token==Enum){
        //enum [id] {a=10,b=20,...}
        match(Enum);
        //存在名称id
        if(token!='{'){
            match(Id);//跳过[id]
        }
        if(token=='{'){
            match('{');
            enum_declaration();
            match('}');
        }

        match(';');
        return ;
   }

    //解析类型信息
    if(token==Int){
        match(Int);
    }
    else if(token==Char){
        match(Char);
        basetype = CHAR;
    }

    //解析逗号分割的变量声明
    while(token!=';'&&token!='}'){
        type = basetype;
        //解析参数类型，可能存在多重指针类型'int*** x'
        while(token==Mul){
            match(Mul);
            type = type + PTR;
        }

        //解析变量名，若变量名不为标识符类型，说明出现错误
        if(token!=Id){
            printf("行%d: 变量声明出现错误!\n",line);
            exit(-1);
        }

        //如果符号表中该标识符类型已经存在，说明当前全局声明出现重复
        if(current_id[Class]){
            printf("行%d: 出现重复全局声明！",line);
            exit(-1);
        }

        //匹配标识符，并声明标识符类型
        match(Id);
        current_id[Type] = type;

        //分析完成，需要开始提前检测当前是一个普通变量还是函数
        if(token=='('){//函数的参数左括号
            current_id[Class] = Fun;          //当前标识符为函数
            current_id[Value] = (int)(text+1);//存放当前函数的起始地址
            function_declaration();
        }else{//全局变量
            current_id[Class] = Glo;          //全局变量
            current_id[Value] = (int)data;    //存放当前变量内存地址
            data = data + sizeof(int);
        }

        //int a,b,c...
        if(token==','){
            match(',');
        }
    }
    next();
}

//语法分析的入口，分析整个c语言程序
void program(){
    next();
    while(token>0){
        printf("token is: %c\n",token);
        //全局声明语法分析
        global_declaration();
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