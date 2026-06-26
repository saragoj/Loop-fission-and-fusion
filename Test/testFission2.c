#include <stdio.h>

int main() {
    
    int n = 10;
    int x = 1;
    int y = 2;

    for(int i=0; i<n; i++){
        if(x>0){
            x++;
        }

        if(y<0){
            y--;
        }else{
            y++;
        }
    }

    return 0;
}