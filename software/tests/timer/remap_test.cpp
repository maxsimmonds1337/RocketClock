#include <cstdint>
#include <bitset>
#include <iostream>

const int16_t COL_1_ROW_1_ANS = 0b000101000000;

int16_t get_data(int8_t row, int8_t col) {
  uint8_t row_remapped = 64 >> (row-1);
  if(row_remapped == 0) {
    row_remapped = 128;
  }
  
  uint16_t word = ((uint16_t) col << 8) | (row_remapped);

  return word;

}

template <typename T>

bool checkAns(T ans, T expected) {
    if (ans != expected) {
        std::cout << "FAIL";
    } else {
        std::cout << "PASS";
    }

    return ans == expected;
}

struct Coordinate {
    int8_t col;
    int8_t row;
};

Coordinate get_LED(int8_t ledIndex) {
    // Note: If ledIndex is 0-63, row should be index / 8 
    // and col should be index % 8, depending on your mapping.
    int8_t row = ledIndex / 8;
    int8_t col = ledIndex % 8;
    
    std::cout << "row: " << (int)row << " col: " << (int)col << " " << std::endl;
    return {col, row};
}

int main(void) {

    int16_t ans = get_data(1,1);
    std::bitset<16> x(ans);
    std::cout << x;
    std::cout << checkAns(ans, COL_1_ROW_1_ANS);

    Coordinate res = get_LED(2);
    return 0;
}
