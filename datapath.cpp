#include "datapath.h"
#include <algorithm>
#include <numeric>

// values nothing reads: the results of the computation
vector<int>results(vector<vector<int>>adj_matrix){
    vector<int>result;
    size_t n = adj_matrix.size();
    for(size_t i = 0; i < n; i++){
        bool read = false;
        for(size_t j = 0; j < n; j++){
            if(i != j && adj_matrix[i][j] == 1){
                read = true;
            }
        }
        if(!read){
            result.push_back(i);
        }
    }
    return result;
}

// a value is loaded into a register at the start of the cycle after the one that computes
// it and is held through the last cycle that reads it; a result is held until the done
// state. Registers are shared with the left-edge algorithm: the values in the order they
// are loaded, each into the first register whose value has had its last read. That uses
// as many registers as the most values alive at once.
vector<struct reg>registers(vector<vector<int>>adj_matrix, map<int, int>schedule){
    int n = adj_matrix.size(), done = total_cycle(schedule) + 1;
    vector<int>last(n, -1); // last cycle that reads each value
    for(int i = 0; i < n; i++){
        for(int j = 0; j < n; j++){
            if(i != j && adj_matrix[i][j] == 1){
                last[i] = max(last[i], schedule[j]);
            }
        }
    }
    for(int node : results(adj_matrix)){
        last[node] = done;
    }

    vector<int>order(n);
    iota(order.begin(), order.end(), 0);
    stable_sort(order.begin(), order.end(), [&](int a, int b){ return schedule[a] < schedule[b]; });
    vector<struct reg>reg_list;
    vector<int>busy; // last cycle that reads the value each register holds now
    for(int node : order){
        int load = schedule[node] + 1;
        size_t r = 0;
        while(r < reg_list.size() && busy[r] >= load){
            r++;
        }
        if(r == reg_list.size()){
            reg_list.emplace_back();
            busy.push_back(0);
        }
        reg_list[r].load[load] = node;
        busy[r] = last[node];
    }
    return reg_list;
}

// get the active nodes in a given cycle
vector<int> active_nodes(map<int, int> schedule, int cycle)
{

    vector<int> active;
    size_t n = schedule.size();
    for (size_t node = 0; node < n; node++)
    {
        if (schedule[node] == cycle)
        {
            active.push_back(node);
        }
    }
    return active;
}

// calculate the total cycle for a given schedule
int total_cycle(map<int, int> schedule)
{
    int cycle = 0;
    for (const auto &pair : schedule)
    {
        if (pair.second > cycle)
        {
            cycle = pair.second;
        }
    }
    return cycle;
}

vector<vector<struct mux>> multiplexers(vector<vector<int>> adj_matrix, map<int, int> schedule)
{
    vector<vector<struct mux>> mux_list(3, vector<struct mux>());
    int cycle = total_cycle(schedule);
    // cout << cycle << endl;
    vector<int> current_resource(3, 0); // the current hardware unit to be utilized

    int i;
    for (i = 0; i <= cycle; i++)
    {
        vector<int> active = active_nodes(schedule, i);
        for (size_t node = 0; node < active.size(); node++)
        {
            int resource = adj_matrix[active[node]][active[node]];

            // need to add in new mux
            if (current_resource[resource] >= mux_list[resource].size())
            {
                // cout << resource << endl;
                struct mux new_mux;
                new_mux.in = map<int, int>();
                new_mux.in.emplace(active[node], i);
                new_mux.output = resource;
                mux_list[resource].push_back(new_mux);
            }
            // resource already exist
            else
            {
                mux_list[resource][current_resource[resource]].in.emplace(active[node], i);
            }
            current_resource[resource]++;
        }
        // reset current resource usage
        for (size_t k = 0; k < 3; k++)
        {
            current_resource[k] = 0;
        }
    }

    // remove all mux with only one input
    for (size_t k = 0; k < 3; k++)
    {
        //erase moves the next mux into slot i, 
        //so i only advances past a mux that stays
        for(size_t i = 0; i < mux_list[k].size();){
            if(mux_list[k][i].in.size() <= 1){
                mux_list[k].erase(mux_list[k].begin() + i);
            }
            else{
                i++;
            }
        }
    }
    return mux_list;
}

// organize mux and reg events by cycle
void sequencer(int cycle, vector<vector<struct mux>>mux_list, vector<struct reg>reg_list, map<int, int>&active_reg, vector<map<int, int>>&active_mux){
    // active_reg maps each register loaded at the start of this cycle to the node it loads
    for(size_t r = 0; r < reg_list.size(); r++){
        if(reg_list[r].load.count(cycle)){
            active_reg[r] = reg_list[r].load[cycle];
        }
    }

    // active mux stores mux index and selected input pair
    for (int i = 0; i < 3; i++)
    {
        map<int, int> m;
        for (int j = 0; j < mux_list[i].size(); j++)
        {
            for (const auto &pair : mux_list[i][j].in)
            {
                if (pair.second == cycle)
                {
                    m.emplace(j, pair.first);
                }
            }
        }
        active_mux.push_back(m);
    }
}