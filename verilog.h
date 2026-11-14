#ifndef VERILOG
#define VERILOG
#include "datapath.h"
// writes generated/hls_core.v for one schedule
void write_verilog(vector<struct node> dag, map<int, int> schedule, vector<struct reg> reg_list, string name);
#endif
