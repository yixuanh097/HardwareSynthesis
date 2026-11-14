#include "datapath.h"
vector<struct reg> registers(vector<vector<int>> adj_matrix, map<int, int> schedule)
{
    vector<struct reg> reg_list;

    // find all edges
    size_t i, j;
    size_t n = adj_matrix.size();
    for (i = 0; i < n; i++)
    {
        for (j = 0; j < n; j++)
        {
            if (i != j && adj_matrix[i][j] == 1)
            {
                for (int k = schedule[i]; k < schedule[j]; k++)
                {
                    struct reg new_reg;
                    new_reg.in = i;
                    new_reg.out = j;
                    new_reg.enable_time = k + 1;
                    reg_list.push_back(new_reg);
                }
            }
        }
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
void sequencer(int cycle, vector<vector<struct mux>> mux_list, vector<struct reg> reg_list, vector<int> &active_reg, vector<map<int, int>> &active_mux)
{
    // active_reg stores register index that are enabled at time=cycle

    for (size_t i = 0; i < reg_list.size(); i++)
    {
        if (reg_list[i].enable_time == cycle)
        {
            active_reg.push_back(i);
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