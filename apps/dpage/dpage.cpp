#include <cstdio>
#include <cstdlib>
#include "../syscall.h"

extern "C" void main(int argc, char** argv) {
  auto [tick_start, timer_freq] = SyscallGetCurrentTick();

  const char* filename = "/memmap";
  int ch = '\n';
  if (argc >= 3) {
    filename = argv[1];
    ch = atoi(argv[2]);
  }
  FILE* fp = fopen(filename, "r");
  if (!fp) {
    printf("failed to open %s\n", filename);
    exit(1);
  }

  // //buf 要領域を dpaging
  // SyscallResult res = SyscallDemandPages(1, 0);
  // if (res.error) {
  //   exit(1);
  // }
  
  //2MiB aligned 領域を dpaging
  for(int i=0; i<512; i++){
    SyscallResult tmp = SyscallDemandPages(1, 0);
    if (((unsigned long)tmp.value & 0x1ff000) == 0x1ff000) break;
  }
  SyscallResult res = SyscallDemandPages(512, 0);

  char* buf = reinterpret_cast<char*>(res.value);
  char* buf0 = buf;

  size_t total = 0;
  size_t n;
  while ((n = fread(buf, 1, 4096, fp)) == 4096) {
    total += n;
    if(total >= 512 * 4096){
     if (res = SyscallDemandPages(1, 0); res.error) {
       exit(1);
     }
    }
    buf += 4096;
  }
  total += n;
  printf("size of %s = %lu bytes\n", filename, total);

  size_t num = 0;
  for (int i = 0; i < total; ++i) {
    if (buf0[i] == ch) {
      ++num;
    }
  }
  printf("the number of '%c' (0x%02x) = %lu\n", ch, ch, num);

  auto tick_end = SyscallGetCurrentTick();
  printf("elapsed %lu ms\n",
         (tick_end.value - tick_start) * 1000 / timer_freq);
  exit(0);
}
