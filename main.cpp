#include "parse.h"
#include <algorithm>
#include <cstring>
#include "schedule.h"
#include "datapath.h"
#include "verilog.h"
using namespace std;
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        cout << "provide filename" << endl;
        return -1;
    }

    vector<string> ops = {"+", "-", "*"};

    bool write_ssa = (argc == 3) && (strcmp(argv[2], "--export-ssa") == 0);
    string verilog = (argc == 4 && strcmp(argv[2], "--verilog") == 0) ? argv[3] : "";
    string filename = argv[1];
    vector<int> constraint(3, 1);

    vector<struct node> dag = ssa(filename, write_ssa, constraint);
    // for (int unit : constraint)
    // {
    //     cout << unit << " ";
    // }
    // cout << endl;
    cout << "node information: " << endl;
    for (const auto &node : dag)
    {
        std::cout << "Operation: " << node.op << ", output: " << node.out << "\n";
    }
    cout << endl;

    cout << "adjacency matrix for DAG:" << endl;
    vector<vector<int>> adj_mat = adj_matrix(dag);
    for (const auto &node : adj_mat)
    {
        for (const auto &node2 : node)
        {
            cout << to_string(node2) << " ";
        }
        cout << endl;
    }
    cout << endl;

    int cycles;
    cout << "ASAP: " << endl;
    map<int, int> asap = ASAP(adj_mat, cycles);
    for (const auto &pair : asap)
    {
        std::cout << dag[pair.first].out << ": " << pair.second << "\n";
    }
    cout << "total cycles: " << cycles << endl
         << endl;

    cout << "ALAP: " << endl;
    map<int, int> alap = ALAP(adj_mat, cycles);
    for (const auto &pair : alap)
    {
        std::cout << dag[pair.first].out << ": " << pair.second << "\n";
    }
    cout << "total cycles: " << cycles << endl
         << endl;

    cout << "force directed schedule: " << endl;
    map<int, int> fds = FDS(adj_mat, cycles, asap, alap);
    for (const auto &pair : fds)
    {
        std::cout << dag[pair.first].out << ": " << pair.second << "\n";
    }
    cout << "total cycles: " << cycles << endl
         << endl;

    cout << "Constrained ASAP: " << endl;
    map<int, int> constrained_asap = ConstrainedASAP(adj_mat, constraint, cycles, asap, alap);
    for (const auto &pair : constrained_asap)
    {
        std::cout << dag[pair.first].out << ": " << pair.second << "\n";
    }
    cout << "total cycles: " << cycles << endl
         << endl;

    cout << "Constrained ALAP: " << endl;
    map<int, int> constrained_alap = ConstrainedALAP(adj_mat, constraint, cycles, asap, alap);
    for (const auto &pair : constrained_alap)
    {
        std::cout << dag[pair.first].out << ": " << pair.second << "\n";
    }
    cout << "total cycles: " << cycles << endl
         << endl;

    // task 4 for the schedule named after --verilog, ASAP when there is none
    map<string, map<int, int>>schedules = {{"asap", asap}, {"alap", alap}, {"fds", fds}, {"casap", constrained_asap}, {"calap", constrained_alap}};
    map<string, string>titles = {{"asap", "ASAP"}, {"alap", "ALAP"}, {"fds", "force directed"}, {"casap", "constrained ASAP"}, {"calap", "constrained ALAP"}};
    string shown = schedules.count(verilog) ? verilog : "asap";
    map<int, int>schedule = schedules[shown];
    vector<int>result = results(adj_mat);

    cout << titles[shown] << " register list" << endl;
    vector<struct reg>reg_list = registers(adj_mat, schedule);
    for(size_t r = 0; r < reg_list.size(); r++){
        cout << "register " << r << " loads";
        string sep = " ";
        for(const auto &[time, node] : reg_list[r].load){
            bool kept = find(result.begin(), result.end(), node) != result.end();
            cout << sep << dag[node].out << " at time " << time << (kept ? " (result)" : "");
            sep = ", ";
        }
        cout << endl;
    }
    cout << endl;

    cout << titles[shown] << " mux list" << endl;
    vector<vector<struct mux>>mux_list = multiplexers(adj_mat, schedule);
    for (size_t i = 0; i < 3; i++)
    {
        cout << ops[i] << endl;
        for (const auto &m : mux_list[i])
        {
            cout << "mux input: " << endl;
            for (const auto &pair : m.in)
            {
                cout << dag[pair.first].out << ", at " << pair.second << endl;
            }
        }
    }
    cout << endl;

    cout << titles[shown] << " sequencer FSM" << endl;
    int last_cycle = total_cycle(schedule);
    map<int, int>active_reg;
    vector<map<int, int>>active_mux;
    // the state after the last cycle is the done state, where the last results are loaded
    for(int i = 0; i <= last_cycle + 1; i++){
        cout << "cycle: " << i << (i > last_cycle ? " (done)" : "") << endl;
        sequencer(i, mux_list, reg_list, active_reg, active_mux);
        for(const auto &[r, node] : active_reg){
            cout << "register " << r << " loads " << dag[node].out << endl;
        }

        for (int k = 0; k < 3; k++)
        {
            cout << "resource " << ops[k] << endl;
            for (const auto &pair : active_mux[k])
            {
                cout << "mux " << pair.first << " is used with input " << dag[pair.second].out << " selected" << endl;
            }
        }
        active_reg.clear();
        active_mux.clear();
    }

    // tasks 5-8: Verilog for the schedule named after --verilog
    if(schedules.count(verilog)){
        write_verilog(dag, schedule, reg_list, verilog);
    }
    else if(!verilog.empty()){
        cout << "unknown schedule " << verilog << " (asap, alap, fds, casap or calap)" << endl;
    }
    return 0;
}