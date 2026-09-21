// Software verified: production PIN renderer/assets; fake monochrome canvas.
#include <cstdint>
#include <cstdio>
#include <cstring>
#define PROGMEM
#define DISP_PIN_SIZE 6
#define SSD1306_WHITE 1
#define SSD1306_BLACK 0
#include "Graphics.h"
struct Canvas {
  uint8_t pixels[64][64] = {};
  int width() { return 64; }
  void drawBitmap(int x, int y, const uint8_t* bytes, int w, int h, int fg, int bg) {
    for (int j=0; j<h; ++j) for (int i=0; i<w; ++i)
      if (x+i >= 0 && x+i < 64 && y+j >= 0 && y+j < 64)
        pixels[y+j][x+i] = (bytes[j*((w+7)/8)+i/8] & (128 >> (i%8))) ? fg : bg;
  }
} disp_area;
#include "pairing_display.h"
static int passes=0, failures=0;
#define CHECK(c,label) do { if(c) ++passes; else { ++failures; fprintf(stderr,"FAIL: %s\n",label); } } while(0)
int main() {
  // Expected positive strokes, independently readable 3x5 glyphs.
  const char* digits[] = {"111101101101111", "010110010010111", "110001010100111",
    "110001010001110", "101101111001001", "111100110001110", "011100111101111",
    "111001010010010", "111101111101111", "111101111001110"};
  for (unsigned digit=0; digit<10; ++digit) {
    draw_pairing_pin(digit * 111111);
    bool correct = true;
    for (int pos=0; pos<6; ++pos) for (int y=0; y<5; ++y) for (int x=0; x<8; ++x) {
      bool expected = x>=2 && x<5 && digits[digit][y*3+x-2]=='1';
      correct &= disp_area.pixels[53+y][7+9*pos+x] == expected;
    }
    CHECK(correct, "six readable light-on-dark digits, including zero");
  }
  draw_pairing_pin(123);
  bool correct = true;
  const unsigned wanted[] = {0,0,0,1,2,3};
  for (int pos=0; pos<6; ++pos) for (int y=0; y<5; ++y) for (int x=0; x<3; ++x)
    correct &= disp_area.pixels[53+y][9+9*pos+x] == (digits[wanted[pos]][y*3+x]=='1');
  CHECK(correct, "PIN preserves leading zeros and digit order");
  FILE* file = fopen("build/host/pairing-display.pbm", "w");
  if (file) {
    fprintf(file,"P1\n64 64\n");
    for (auto& row : disp_area.pixels) { for (auto pixel : row) fprintf(file,"%u ",pixel); fputc('\n',file); }
    fclose(file);
  }
  printf("%d passed, %d failed\n",passes,failures); return failures?1:0;
}
