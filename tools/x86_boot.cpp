// Batch/debug driver for the PM3WIN.EXE host (see core/pm3/x86host.cpp for the HLE and the debug environment variables).
#include "pm3/x86host.h"
int main(int argc, char** argv) { return pm3::x86host_main(argc, argv, nullptr); }
