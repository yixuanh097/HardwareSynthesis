#ifndef DATAPATH
#define DATAPATH
#include "schedule.h"
// a register holds one value at a time; values whose lifetimes do not overlap share it
struct reg{
    map<int, int>load; // cycle -> node whose value the register loads at the start of that cycle
};

struct mux
{
    map<int, int> in; // input nodes index, cycle used
    int output;       // output to which resource
};

vector<struct reg> registers(vector<vector<int>> adj_matrix, map<int, int> schedule);
vector<int>results(vector<vector<int>>adj_matrix);
vector<vector<struct mux>> multiplexers(vector<vector<int>> adj_matrix, map<int, int> schedule);
void sequencer(int cycle, vector<vector<struct mux>>mux_list, vector<struct reg>reg_list, map<int, int>&active_reg, vector<map<int, int>>&active_mux);
int total_cycle(map<int, int> schedule);
#endif
