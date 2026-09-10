#include <iostream>
#include "fonts.h"
using namespace std;


int main(void) {

    // let's  print the first one and see if it looks correct
    for(int bit=1; bit <= 64; bit++) {
        cout << FONT_LOOKUP[0][bit-1];
        if(bit % 8 == 0) {
            cout << endl;
        }
    }
    return 0;
}
