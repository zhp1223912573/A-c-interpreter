#include<stdio.h>

char * str;

int fibonacci(int i){
     if(i<=1){
        return 1;
     }
     return fibonacci(i-1)+fibonacci(i-2);
}

int main(){
    int i;
    i = 0;
    str = "this is fibonacci.c!";
    printf("%s\n",str);
    
    while(i<=10){
      
           printf("fibonacci(%2d) = %d\n",i,fibonacci(i));
        i = i+1;
    }
    return 10;
}