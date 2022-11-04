
int a ; 


int add(int a,int b){
    int c;
    c = a+b;
    printf("%d + %d = %d\n",a,b,c);
    return c;
}

int main(){
    int a,b,c;
    a = 1;
    b = 2;

    printf("a + b = c\n");
    c = add(a,b);

    while(c>0){
        printf("Dec c util c is 0,c:%d\n",c);
        c--;
    }

    if(c==0){
        printf("Now, c is %d\n",c);
    }

    a>b ? printf("a gt b\n"):printf("b gt a\n");
    return c;

}