#include <stdio.h>

int say_hi(int n) {
  for(int i = 0; i < n; i++) {
    puts("Hello, world!");
  }
  return 0;
}

int main(int argc, char** argv) {
  for(int i = 0; i < 5; i++) {
    puts("Hello, world~");
  }
  say_hi(3);
  return 0;
}

