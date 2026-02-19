#include <stdio.h>

int main(void) {
  int score = 9;

  if (score > 10) {
    score = score / 2;
  } else {
    score = score + 3;
  }

  return score & 0xff;
}