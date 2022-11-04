#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<memory.h>

#define int long long

int token ;        //指向当前代码中的token
char *src,*old_src; //指向源代码字符串
int poolsize;       //模拟的text/data/stack大小
int line;           //行号

int *text,      //代码段，存放代码
    *old_text,  //for dump text segement
    *stack;     //栈，处理函数调用过程中的相关数据，如栈帧或者局部变量
char *data;     //数据段，存放初始化后的数据，保存都是字符型

/*虚拟机寄存器
/   通过下四个寄存器保存当前计算机的运行状态
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


//符号表中，标识符的域*/
enum{
    Token,Hash,Name,Class,Type,Value,Bclass,Btype,Bvalue,IdSize
};

//变量和函数的类型*/
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
            while(*src!=0 &&*src!='\n'){
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
                if(current_id[Hash]==hash && !memcmp((char*)current_id[Name],last_pos,src-last_pos)){
                    token  = current_id[Token];
                    return;
                }
                //移动到下一个标识符
                current_id = current_id + IdSize;
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
                if(token_val == '\\'){
                    token_val = *src++;
                    if(token_val == 'n'){
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
            }else if(*src=='*'){
                src++;
                while(*src!=0 && *(src+1)!=0 && (*src!='*' ||*(src+1)!='/')){
                    if(*src=='\n') line++;
                    src++; 
                }
                src++;
                src++;            
            }
            else{
                //除法符号
                token = Div;
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

//匹配当前标记是否正确
void match(int tk){
    if(token==tk){
        next();
    }else{
        printf("行(%d):预期为(%d),实际为(%d)\n",line,tk,token);
        exit(-1);
    }
}

//解析表达式 
/*优先级爬山
    单元运算符优先级高于二元，三元运算符
    在解析过程中，使用stack保存参数变量，而expression函数的递归调用栈来保存操作符
    通过递归函数实现优先级爬山

    用 expression(level) 进行解析的时候，我们其实通过了参数 level 指定了当前的优先级
*/
void expression(int level){

    int *id;    //当前标识符
    int tmp;
    int *addr;
    //表达式有多种表达形式
    //但主要分为两类：单元体(unit)和运算符(operators)
    //例：(char) *a[10] = (int*) func(b>0?10:20);
    //a[10] 是单元体unit *是运算符operator
    //func()也是单元体
    //我们应当先解析单元体和一元运算符,然后再解析二元，三元运算符
    //表达式可以标识为下述形式:

    //1. unit_unary ::= unit | unit unary_op | uary_op unit
    //2. expr ::= unit_uary (bin_op unit_unary ...)


     if (!token) {
            printf("%d: unexpected token EOF of expression\n", line);
            exit(-1);
    }
    /*常量 
    应当直接加载到ax中
     */
    if(token==Num){
        match(Num);

        *++text = IMM;
        *++text = token_val;
        expr_type = INT;//表达式类型
    }
    /*字符串
    支持char *p;
    p = "first line"
        "second line";
    p实质等同于 "firsr linesecond line"
    */
    else if(token=='"'){
        *++text = IMM;
        *++text = token_val;

        //继续匹配，查看是否存在上述匹配原则, 保存剩下的字符串
        match('"');
        while(token=='"'){
            match('"');
        }

        //所有数据都默认为0，在字符串末尾添加'\0',只需将数据向前移动一个位置即可
         data = (char *)(((int)data + sizeof(int)) & (-sizeof(int)));
         expr_type = PTR;

    }
    /*sizeof
    sizeof(int),sizeof(char),sizeof(*)都返回int
    */
   else if(token==Sizeof){
        match(Sizeof);
        match('(');
        expr_type = INT;

        if(token==Int){
            match(Int);
        }
        else if(token==Char){
            match(Char);
            expr_type = CHAR;
        }

        while(token==Mul){
            match(Mul);
            expr_type = expr_type + PTR;
        }

        match(')');

        *++text = IMM;
        *++text = (expr_type==CHAR? sizeof(char):sizeof(int));

        expr_type = INT;
   }
    /**
     * 变量与函数调用
     * 变量：全局，局部变量，enum
     * 
    */
   else if(token==Id){

        match(Id);
        id = current_id;

        //出现左括号说明是一个函数调用
        if(token=='('){
            match('(');
            tmp = 0;//该函数的参数个数
            //读取参数
            while(token!=')'){
                //可能存在：add(add(1,2),2)这种调用情况，所以需要递归调用expression进行表达式解析
                expression(Assign); //参数值被保存在ax中
                
                *++text = PUSH;     //将保存在ax中的参数压入栈
                tmp++;              //参数个数

                if(token==','){
                    match(',');
                }
            }
            match(')');

            //判断当前函数类型
            if(id[Class]==Sys){//系统调用
                *++text = id[Value];//保存系统函数调用地址
            }else if(id[Class]==Fun){//自定义函数
                *++text = CALL;
                *++text = id[Value];//压入在函数解析时放入符号表中的函数地址
            } else {
                printf("行%d:函数调用错误！\n", line);
                exit(-1);
            }

            //生成函数调用过程中，在栈帧中保存了函数参数，函数返回后需要清除这一部分空间
            if(tmp>0){
                *++text = ADJ;
                *++text = tmp;
            }

            expr_type = id[Type];
        }
        //enum类型
        else if(id[Class]==Num){
            //加载enum类型到ax
            *++text = IMM;
            *++text = id[Value];
            expr_type =INT;
        }
        //变量
        else{
            //局部变量
            if(id[Class]==Loc){
                //局部变量存放在栈上，想要得到该数值，
                //index_of_bp等于2，想要得到局部变量，需要index_of_bp减去符号表中当前变量的value
                //当变量为locala时，该value值等于3,这样LEV -1就能得到locala
                /*
                 * 0 a
                 * 1 b
                 * 2 bp     <--index_of_bp
                 * 3 locala
                 * 4 localb
                 */
                *++text = LEA;
                *++text = index_of_bp - id[Value];
            }    
            else if(id[Class]==Glo){
                *++text = IMM;
                *++text = id[Value];
            }
            else{
                printf("行%d:未定义变量!",line);
                exit(-1);
            }
            //将ax中保存的上述变量的地址保存的值读取到ax中
            expr_type = id[Type];
            *++text = (expr_type==Char)?LC:LI;
        }

   }

    /*括号或强制转换*/
    else if(token=='('){
        match('(');

        if(token==Int||token==Char){//类型转换
            tmp = (token==Char)?CHAR:INT;//转换的类型
            match(token);

            while(token==Mul){
                 match(Mul);
                tmp = tmp + PTR;
            }

            match(')');
            expression(Inc);//强转和自增优先级一致

            expr_type = tmp;

        }else{//普通括号
            expression(Assign);
            match(')');
        }
    }

    /*指针取值*/
    else if (token == Mul) {
        // dereference *<addr>
        match(Mul);
        expression(Inc); //取值和自增优先级一致

        if (expr_type >= PTR) {
            expr_type = expr_type - PTR;
        } else {
            printf("行%d: 引用出错\n", line);
            exit(-1);
        }

        //取值
        *++text = (expr_type == CHAR) ? LC : LI;
    }

    /*取值
    一般情况我们获取某个变量的地址，随后调用LI和LC指令获取地址上的值，
    为了取值，我们删除取值操作LI，LC即可
    */
   else if(token == And){
        match(And);
        expression(Inc);
        if(*text==LC||*text==LI){
            text--;
        }else{
            printf("行%d: 取值错误!\n", line);
            exit(-1);
        }

     expr_type = expr_type + PTR;
   }

    /*逻辑取反
        没有直接指令，通过判断变量是否和0相等进行逻辑取反，0代表逻辑false*/
    else if(token=='!'){
        match('!');
        expression(Inc);

        *++text = PUSH;//将变量压入栈顶
        *++text = IMM; //将ax置为0
        *++text = 0;
        *++text = EQ;//比较栈顶和ax是否一致，达到逻辑取反的目的

        expr_type = INT;
    }

    /*按位取反
        没有直接指令，通过异或实现安慰取法 ~a = a xor 0xFFFF*/
    else if(token == '~'){
        match('~');
        expression(Inc);

        *++text = PUSH; //压入变量
        *++text = IMM; //将ax置为-1 = 0xFFFF
        *++text = -1;
        *++text = XOR;

        expr_type = INT;
    }

    /*取正取反
        取正不做任何操作
        取反通过0-x实现*/
    else if(token ==Add){
        match(Add);
        expression(Inc);

        expr_type = INT;
    }
    else if(token ==Sub){
         match(Sub);

        //数值型
        if (token == Num) {
            *++text = IMM;
            *++text = -token_val;
            match(Num);
        } else {
            //直接*-1即可
            *++text = IMM;
            *++text = -1;
            *++text = PUSH;
            expression(Inc);
            *++text = MUL;
        }

        expr_type = INT;
    }

    /**自增自减
      ++p
    */
   else if(token==Inc || token==Dec){
        tmp = token;
        match(token);
        expression(Inc);

        //为了实现自增自减操作，需要使用p的地址两次
        if(*text==LC){//为了避免当前自增自减变量丢失，需要先将当前变量地址压入栈顶，后续自增自减完成后再写入该地址内新值
            *text = PUSH;
            *++text = LC;
        }else if(*text==LI){
            *text = PUSH;
            *++text = LI;
        }else{
            printf("行%d:自增自减出错!",line);
        }

        *++text = PUSH;
        *++text = IMM;
        //需要区分当前变量是否为指针型，是的话增加int长度，不是的话该指为普通变量，无论是int还是char都只加减1
        *++text = (expr_type>PTR)?sizeof(int):sizeof(char);
        *++text = (tmp==Inc)?ADD:SUB;
        *++text = (expr_type==CHAR)?SC:SI;//将变化后的值写入先前压入栈的地址处
   }


    //开始处理二元运算符和后置运算符
    //只有当当前运算符优先级大于传入运算符，才开始
    while (token >= level) {
       
       tmp = expr_type ;
        /**赋值操作 =
         * a = (expression)
         * 读取到a时生成 IMM <addr> ,LI ,
         * 为了将expression赋值给a，需要保存a的地址，并将expression值写入该地址
         * 所以上述指令变为 IMM <addr>, PUSH , (expression()读取expression值),SI/SC(写入expression)
         * 
        */
       if(token==Assign){
            match(Assign);
            if(*text==LC||*text==LI){
                *text = PUSH;//将当前变量a地址压入栈顶
            }else{
                 printf("行%d:赋值操作错误！\n", line);
                exit(-1);
            }
            expression(Assign);
            expr_type = tmp;
            *++text = (expr_type==CHAR)?SC:SI;
       }

        /*三目运算符
         类似if语句
         */
        else if(token==Cond){
            match(Cond);
            *++text = JZ;
            addr = ++text;//预留出false条件下的语句起始位置地址
            expression(Assign);

            if(token==':'){
                match(':');
            } else{
                printf("行%d:三目运算符：丢失!",line);
                exit(-1);
            }

            *addr = (int)(text+3);//写入之前JZ的参数
            *++text = JMP;
            addr = ++text;  //同样保留JMP参数位置
            expression(Cond);//递归解析
            *addr = (int)(text+1);
        }
    
        /*逻辑运算符||和&&
        <expr1> || <expr2>     <expr1> && <expr2>

        ...<expr1>...          ...<expr1>...
        JNZ b                  JZ b
        ...<expr2>...          ...<expr2>...
        b:                     b:
        */
       else if(token==Lor){
            match(Lor);

            *++text = JNZ;
            addr = ++text;
            expression(Lan);
            *addr = (int)(text+1);

            expr_type = INT;

       }else if(token==Lan){
            match(Lan);

            *++text = JZ;
            addr = ++text;
            expression(Or);
            *addr = (int)(text + 1);

            expr_type = INT;
       }

       /*异或
       <expr1> ^ <expr2>

        ...<expr1>...          <- now the result is on ax
        PUSH
        ...<expr2>...          <- now the value of <expr2> is on ax
        XOR
        */
       else if(token==Xor){
            match(Xor);
            *++text = PUSH;
            expression(And);
            *++text = XOR;
            expr_type = INT;
       }
       else if(token==Or){
            match(Or);
            *++text = PUSH;
            expression(Xor);
            *++text = OR;
            expr_type = INT;
       }
        else if(token==And){
            match(And);
            *++text = PUSH;
            expression(Eq);
            *++text = AND;
            expr_type = INT;
       }
        else if(token==Eq){
            match(Eq);
            *++text = PUSH;
            expression(Ne);
            *++text = EQ;
            expr_type = INT;
       }
        else if (token == Ne) {
            // not equal !=
            match(Ne);
            *++text = PUSH;
            expression(Lt);
            *++text = NE;
            expr_type = INT;
        }
        else if (token == Lt) {
            // less than
            match(Lt);
            *++text = PUSH;
            expression(Shl);
            *++text = LT;
            expr_type = INT;
        }
        else if (token == Gt) {
            // greater than
            match(Gt);
            *++text = PUSH;
            expression(Shl);
            *++text = GT;
            expr_type = INT;
        }
        else if (token == Le) {
            // less than or equal to
            match(Le);
            *++text = PUSH;
            expression(Shl);
            *++text = LE;
            expr_type = INT;
        }
        else if (token == Ge) {
            // greater than or equal to
            match(Ge);
            *++text = PUSH;
            expression(Shl);
            *++text = GE;
            expr_type = INT;
        }
        else if (token == Shl) {
            // shift left
            match(Shl);
            *++text = PUSH;
            expression(Add);
            *++text = SHL;
            expr_type = INT;
        }
        else if (token == Shr) {
            // shift right
            match(Shr);
            *++text = PUSH;
            expression(Add);
            *++text = SHR;
            expr_type = INT;
        }

        /*相加
        <expr1> + <expr2>
        普通变量        指针变量
        normal         pointer

        <expr1>        <expr1>
        PUSH           PUSH
        <expr2>        <expr2>     |
        ADD            PUSH        | <expr2> * <unit>
                        IMM <unit>  |
                        MUL         |
                        ADD
        */
       else if(token == Add){
            match(Add);
            *++text = PUSH;
            expression(Mul);

            expr_type = tmp;
            //判断当前expr1是否为指针类型,是的话int型指针需要*4
            if(expr_type>PTR){
                *++text = PUSH;
                *++text = IMM;
                *++text = sizeof(int);
                *++text = MUL;
            }

            *++text = ADD;
       }
       /*
       作指针减法时，如果是两个指针相减（相同类型），则结果是两个指针间隔的元素个数。因此要有特殊的处理。
       */
      else if(token ==Sub){
            match(Sub);
            *++text = PUSH;
            expression(Mul);

            //a - b
            if(tmp>PTR && expr_type==tmp){//两者都是指针类型，求解的两指针间的元素个数
                *++text = SUB;  //栈顶的a与ax中的b相减
                *++text = PUSH; //差值压入栈顶
                *++text = IMM;
                *++text = sizeof(int);//指针大小
                *++text = DIV;  //差值除以sizeof(int),得到元素个数
                expr_type = INT;

            }else if(tmp>PTR){//a为指针类型，b为普通类型，和相加一致
                *++text = PUSH;//把b也压入栈顶
                *++text = IMM;  //压入指针大小
                *++text = sizeof(int);
                *++text = MUL;  //得到指针a要移动的距离
                *++text = SUB;  //减去移动距离
                expr_type = tmp;
            }else{//两者都是普通变量，正常相减即可
                *++text = SUB;
                expr_type = tmp;
            }
        }

       /*乘，除，取模较为简单，按顺序填入指令即可
       */ 
      else if (token == Mul) {
            // multiply
            match(Mul);
            *++text = PUSH;
            expression(Inc);
            *++text = MUL;
            expr_type = tmp;
       }
      else if (token == Div) {
            // divide
            match(Div);
            *++text = PUSH;
            expression(Inc);
            *++text = DIV;
            expr_type = tmp;
        }
      else if (token == Mod) {
            // Modulo
            match(Mod);
            *++text = PUSH;
            expression(Inc);
            *++text = MOD;
            expr_type = tmp;
        } 

      /*后置自增，自减
      */
     else if(token==Inc||token==Dec){
        //实现ax中地址变量的自增或自减，同时需要返回原先值变量

        //这里实现先对ax地址变量的自增或自减，随后再压入该变化后值，再反过来，自增的再自减，自减的再自增
        //既改变了一开始ax中地址中的变量，同时返回了原先值

        if(*text==LI){
            *text = PUSH;
            *++text = LI;
        }else if(*text==LC){
            *text = PUSH;
            *++text = LC;
        }else{
            printf("行%d:后置自增自减出现错误!",line);
            exit(-1);
        }

        *++text = PUSH;
        *++text = IMM;
        //指针移动int长度字节，普通变量变化char（1字节）长度
        *++text = (expr_type>PTR)?sizeof(int):sizeof(char);
        *++text = (token==Inc)? ADD:SUB;    //改变原先变量值
        *++text = (expr_type==CHAR)?SC:SI;  //将变化后的值写入对应地址
        *++text = PUSH;                     //将变化值压入栈中，后续执行相反操作
        *++text = IMM;
         //指针移动int长度字节，普通变量变化char（1字节）长度
        *++text = (expr_type>PTR)?sizeof(int):sizeof(char);
        *++text = (token==Inc)? SUB:ADD;    //对修改后的值执行相反操作，得到自增自减前的数值
        match(token);
     }
    
    /*数组取值操作
        a[10] == *(a+10) 
        按上述进行转换
    */
     else if(token ==Brak){
        match(Brak);
        *++text = PUSH;//压入当前的a
        expression(Assign);
        match(']');

        if(tmp>PTR){
            *++text = PUSH; //压入数组的偏移量
            *++text = IMM;
            *++text = sizeof(int);
            *++text = MUL;  //指针类型偏移量需要乘上指针长度
        }else if(tmp<PTR){
            printf("行%d:预期为指针类型！",line);
            exit(-1);
        }

        expr_type = tmp - PTR;  //获取指针类型
        *++text = ADD;          //a指针地址添加具体偏移量
        *++text = (expr_type==CHAR)?LC:LI;  //读取偏移后的数组位置

     }
    else {
        printf("行%d:编译错误，token = %d\n", line, token);
        exit(-1);
    }
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

        match('(');
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

        match(';');

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
    *++text = ENT;
    *++text = pos_local - index_of_bp;

    //进行语句分析
    while(token!='}'){
        statement();
    }

    //离开函数
    *++text = LEV;
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
        current_id[Value] = i++;

        if(token==','){
            next();
        }

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
        //printf("token is: %c\n",token);
        //全局声明语法分析
        global_declaration();
    }
}


//虚拟机入口，解释目标代码
int eval(){
    //读取指令
    int op,*tmp;
    //循环次数
    int cycle ;
    cycle = 0;
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
        else if(op==SI) *(int*)*sp++ = ax;
        //加载ax中char型值到栈顶、保持地址中
        else if(op==SC)  ax =*(char*)*sp++ = ax;
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
            bp = (int*)*sp++;//更新bp，指向父函数的栈帧基址处
            pc = (int*)*sp++;//pc更新为调用子函数时下一条指令的地址，至此，指令执行流回归正常。
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
   else if (op == EXIT) {
    printf("exit(%d)", *sp); return *sp;}
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
    int *tmp;
    //debug使用
    // int b = Id;
    // printf("%d",b);
    //printf("argc = %d\n", argc);
	// for (int i = 0; i < argc; i++) {
	// 	printf("argv[%d] = %s\n", i, argv[i]);
	// }
    //     argc = 2;
    //    argv[1] = "zushi.c";

    // printf("argc = %d\n", argc);

	// for (int i = 0; i < argc; i++) {
	// 	printf("argv[%d] = %s\n", i, argv[i]);
	// }

    //自举检验
    i = 0;
    while(i<argc){
        printf("argv[%d] = %s\n",i,argv[i]);
        i ++;
    }
    printf("**********************\n");

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
     if (!(symbols = malloc(poolsize))) {
        printf("could not malloc(%d) for symbol table\n", poolsize);
        return -1;
    }

    //初始化三个区域
    memset(text,0,poolsize);
    memset(data,0,poolsize);
    memset(stack,0,poolsize);
    memset(symbols, 0, poolsize);

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
    while(i<=EXIT){
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

    //查看main函数是否加载
    if (!(pc = (int *)idmain[Value])) {
        printf("main()未定义\n");
        return -1;
    }

    //设置栈帧，使main函数返回时可以正常退出
    sp = (int*)((int)stack+poolsize);
    *--sp = EXIT;       //main正常返回
    *--sp = PUSH;       //压入返回值
    tmp = sp;
    *--sp = argc;       
    *--sp = (int)argv;
    *--sp = (int)tmp;

    printf("***start the vm!***\n\n");
    return eval();
}