#include <stdio.h>
#include <stdlib.h>

#include "debugger/debugger.h"

int main(int argc, char** argv) {
  using namespace kura;
  Debugger debugger;
  return debugger(argc, argv);
}
