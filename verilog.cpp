#include "verilog.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <random>

struct operand
{
    string expr;    // Verilog source: inK, rK or a constant
    int node = -1;  // node that computes it
    int input = -1; // primary input index
    int value = 0;  // constant value
};

// bits that tell apart the given number of values
static int bits(size_t values){
    int w = 1;
    while((size_t(1) << w) < values){
        w++;
    }
    return w;
}

// "[w-1:0] " for a bus, nothing for one bit
static string bus(int width){
    return width == 1 ? string("") : "[" + to_string(width - 1) + ":0] ";
}

void write_verilog(vector<struct node> dag, map<int, int> schedule, vector<struct reg> reg_list, string name)
{
    const char *kinds[3] = {"add", "sub", "mul"}, *modules[3] = {"adder", "subtractor", "multiplier"};
    int n = dag.size(), cycles = 0;
    vector<int> kind(n), unit(n), count(3, 0);
    for (int i = 0; i < n; i++)
    {
        if (!schedule.count(i))
        {
            cout << "verilog: " << dag[i].out << " is not scheduled" << endl;
            return;
        }
        kind[i] = dag[i].op == "+" ? 0 : dag[i].op == "-" ? 1 : 2;
        cycles = max(cycles, schedule[i] + 1);
    }

    // in each cycle the k-th operation of a kind runs on unit k of that kind (the
    // binding multiplexers() uses); units are numbered adders, subtractors, multipliers
    for (int c = 0; c < cycles; c++)
    {
        int used[3] = {0, 0, 0};
        for (int i = 0; i < n; i++)
            if (schedule[i] == c)
                unit[i] = used[kind[i]]++;
        for (int k = 0; k < 3; k++)
            count[k] = max(count[k], used[k]);
    }
    vector<string> units, mods;
    for (int k = 0; k < 3; k++)
        for (int j = 0; j < count[k]; j++)
        {
            units.push_back(kinds[k] + to_string(j));
            mods.push_back(modules[k]);
        }
    auto unit_of = [&](int i)
    { return (kind[i] > 0 ? count[0] : 0) + (kind[i] > 1 ? count[1] : 0) + unit[i]; };

    // the task-4 register that holds each value, and the results: values nothing reads,
    // kept in their registers until the done state
    vector<int>reg_of(n, -1);
    for(size_t r = 0; r < reg_list.size(); r++){
        for(const auto &[time, node] : reg_list[r].load){
            reg_of[node] = r;
        }
    }
    vector<int>result = results(adj_matrix(dag));
    vector<bool>is_result(n, false);
    for(int node : result){
        is_result[node] = true;
    }

    // operands: the register that holds the value, a primary input or a constant
    vector<string> inputs;
    vector<vector<operand>> ops(n, vector<operand>(2));
    for (int i = 0; i < n; i++)
        for (int s = 0; s < 2; s++)
        {
            string x = s == 0 ? dag[i].in1 : dag[i].in2;
            operand &o = ops[i][s];
            for (int p = 0; p < n; p++)
                if (dag[p].out == x)
                    o.node = p;
            if(o.node >= 0 && (reg_of[o.node] < 0 || schedule[o.node] >= schedule[i])){
                cout << "verilog: " << dag[i].out << " reads " << x << " before it is computed" << endl;
                return;
            }
            if(o.node >= 0){
                o.expr = "r" + to_string(reg_of[o.node]);
            }
            if (o.node < 0 && isdigit((unsigned char)x[0]))
            {
                o.value = stoi(x) / 10 % 256; // the SSA step appends "0" to constants too
                o.expr = "8'd" + to_string(o.value);
            }
            else if (o.node < 0)
            {
                o.input = find(inputs.begin(), inputs.end(), x) - inputs.begin();
                if (o.input == (int)inputs.size())
                    inputs.push_back(x);
                o.expr = "in" + to_string(o.input);
            }
        }
    vector<int> defaults; // hardcoded input values 2, 3, 4, ...; inputs 0-15 can be loaded from the switches
    for (size_t k = 0; k < inputs.size(); k++)
        defaults.push_back((k + 2) % 256);
    auto evaluate = [&](vector<int> in)
    {
        vector<int> v(n);
        for (int i = 0; i < n; i++)
        {
            int a[2];
            for (int s = 0; s < 2; s++)
            {
                operand &o = ops[i][s];
                a[s] = o.node >= 0 ? v[o.node] : o.input >= 0 ? in[o.input] : o.value;
            }
            v[i] = (kind[i] == 0 ? a[0] + a[1] : kind[i] == 1 ? a[0] - a[1] : a[0] * a[1]) & 255;
        }
        return v;
    };

    // operand multiplexers: each unit input's sources, in the order the control steps use
    // them; select k picks source k, and select 0 holds the input at 0 while the unit is idle
    const char *side[2] = {"_a", "_b"};
    vector<array<vector<string>, 2>>sources(units.size());
    for(int c = 0; c < cycles; c++){
        for(int i = 0; i < n; i++){
            for(int s = 0; schedule[i] == c && s < 2; s++){
                vector<string>&list = sources[unit_of(i)][s];
                if(find(list.begin(), list.end(), ops[i][s].expr) == list.end()){
                    list.push_back(ops[i][s].expr);
                }
            }
        }
    }
    // register input multiplexers: the units a register loads from, in load order
    vector<vector<string>>loads_from(reg_list.size());
    for(size_t r = 0; r < reg_list.size(); r++){
        for(const auto &[time, node] : reg_list[r].load){
            string y = units[unit_of(node)] + "_y";
            if(find(loads_from[r].begin(), loads_from[r].end(), y) == loads_from[r].end()){
                loads_from[r].push_back(y);
            }
        }
    }
    auto position = [](const vector<string>&list, const string &x){ return int(find(list.begin(), list.end(), x) - list.begin()); };

    // control signals from hls_ctrl to hls_datapath: a select for every unit input, and a
    // write-enable for every register plus a select when it loads from more than one unit
    vector<pair<string, int>>controls; // name, width
    for(size_t u = 0; u < units.size(); u++){
        for(int s = 0; s < 2; s++){
            controls.push_back({"sel_" + units[u] + side[s], bits(sources[u][s].size() + 1)});
        }
    }
    for(size_t r = 0; r < reg_list.size(); r++){
        controls.push_back({"en_r" + to_string(r), 1});
        if(loads_from[r].size() > 1){
            controls.push_back({"sel_r" + to_string(r), bits(loads_from[r].size())});
        }
    }
    if(!result.empty()){
        controls.push_back({"done", 1}); // results show on fu_out only in the done state
    }
    string all, wiring;
    for(const auto &[signal, width] : controls){
        all += (all.empty() ? "" : ", ") + signal;
        wiring += ",\n        ." + signal + "(" + signal + ")";
    }

    // hls_core: the controller and the datapath, behind the ports top.v and the testbench use
    filesystem::create_directories("generated");
    ofstream v("generated/hls_core.v");
    v << "// hls_core.v - generated from the " << name << " schedule by ./main --verilog (8-bit datapath)\n"
         "// hls_core connects hls_ctrl, the sequencer FSM, to hls_datapath through selects and write-enables\n"
         "// fu_sel:";
    for(size_t u = 0; u < units.size(); u++){
        v << " " << u << "=" << units[u];
    }
    for(size_t k = 0; k < result.size(); k++){
        v << " " << units.size() + k << "=" << dag[result[k]].out;
    }
    v << "\nmodule hls_core(\n"
         "    input clk, rst,\n"
         "    input step,           // advance one control step\n"
         "    input ld,             // load ld_val into input ld_sel (inputs 0-15)\n"
         "    input [3:0] ld_sel,\n"
         "    input [7:0] ld_val,\n"
         "    input [4:0] fu_sel,   // unit shown on fu_out; past the units, the result registers\n"
         "    output [4:0] cyc,     // control step, "
      << cycles << " = done\n    output [7:0] fu_out\n);\n";
    for(const auto &[signal, width] : controls){
        v << "    wire " << bus(width) << signal << ";\n";
    }
    v << "    hls_ctrl ctrl(\n        .clk(clk), .rst(rst), .step(step), .cyc(cyc)" << wiring << "\n    );\n"
         "    hls_datapath dp(\n        .clk(clk), .rst(rst), .step(step), .ld(ld), .ld_sel(ld_sel), .ld_val(ld_val), .fu_sel(fu_sel)"
      << wiring << ",\n        .fu_out(fu_out)\n    );\nendmodule\n";

    // hls_ctrl: each state sets the selects of the units it uses, and the write-enables and
    // input selects of the registers loaded as it ends
    v << "\n// sequencer FSM: the state is the control step cyc, and every select and write-enable depends only on it\n"
         "module hls_ctrl(\n"
         "    input clk, rst, step,\n"
         "    output reg [4:0] cyc";
    for(const auto &[signal, width] : controls){
        v << ",\n    output reg " << bus(width) << signal;
    }
    v << "\n);\n"
         "    always @(posedge clk)\n"
         "        if(rst)\n"
         "            cyc <= 0;\n"
         "        else if(step && cyc < "
      << cycles << ")\n"
         "            cyc <= cyc + 1;\n\n"
         "    always @* begin\n";
    if(!all.empty()){
        v << "        {" << all << "} = 0;\n";
    }
    v << "        case(cyc)\n";
    for(int c = 0; c < cycles; c++){
        v << "            " << c << ": begin\n";
        for(int i = 0; i < n; i++){
            if(schedule[i] != c){
                continue;
            }
            int u = unit_of(i);
            v << "                ";
            for(int s = 0; s < 2; s++){
                v << "sel_" << units[u] << side[s] << " = " << bits(sources[u][s].size() + 1) << "'d"
                  << position(sources[u][s], ops[i][s].expr) + 1 << "; ";
            }
            v << "// " << dag[i].out << " = " << dag[i].in1 << " " << dag[i].op << " " << dag[i].in2 << "\n";
        }
        for(size_t r = 0; r < reg_list.size(); r++){
            if(!reg_list[r].load.count(c + 1)){
                continue;
            }
            int node = reg_list[r].load.at(c + 1);
            v << "                en_r" << r << " = 1'b1; ";
            if(loads_from[r].size() > 1){
                v << "sel_r" << r << " = " << bits(loads_from[r].size()) << "'d"
                  << position(loads_from[r], units[unit_of(node)] + "_y") << "; ";
            }
            v << "// r" << r << " <= " << dag[node].out << "\n";
        }
        v << "            end\n";
    }
    if(!result.empty()){
        v << "            " << cycles << ": done = 1'b1;\n";
    }
    v << "        endcase\n    end\nendmodule\n";

    // hls_datapath: everything the controller drives; it never sees the control step
    v << "\n// datapath: inputs, shared registers, functional units and multiplexers, driven by hls_ctrl\n"
         "module hls_datapath(\n"
         "    input clk, rst, step, ld,\n"
         "    input [3:0] ld_sel,\n"
         "    input [7:0] ld_val,\n"
         "    input [4:0] fu_sel";
    for(const auto &[signal, width] : controls){
        v << ",\n    input " << bus(width) << signal;
    }
    v << ",\n    output reg [7:0] fu_out\n);\n";
    for(size_t k = 0; k < inputs.size(); k++){
        v << "    reg [7:0] in" << k << " = 8'd" << defaults[k] << "; // " << inputs[k] << "\n";
    }
    for(size_t r = 0; r < reg_list.size(); r++){
        v << "    reg [7:0] r" << r << ";";
        string sep = " // ";
        for(const auto &[time, node] : reg_list[r].load){
            v << sep << dag[node].out << " from cycle " << time << (is_result[node] ? " (result)" : "");
            sep = ", ";
        }
        v << "\n";
    }
    string operands;
    for(string u : units){
        operands += (operands.empty() ? "" : ", ") + u + "_a, " + u + "_b";
    }
    v << "    reg [7:0] " << operands << ";\n";
    for(size_t u = 0; u < units.size(); u++){
        v << "    wire [7:0] " << units[u] << "_y;\n    " << mods[u] << " u_" << units[u] << "(" << units[u] << "_a, "
          << units[u] << "_b, " << units[u] << "_y);\n";
    }
    v << "\n    // operand multiplexers: select 0 holds an idle unit's input at 0\n";
    for(size_t u = 0; u < units.size(); u++){
        for(int s = 0; s < 2; s++){
            string port = units[u] + side[s];
            v << "    always @*\n        case(sel_" << port << ")\n";
            for(size_t k = 0; k < sources[u][s].size(); k++){
                v << "            " << k + 1 << ": " << port << " = " << sources[u][s][k] << ";\n";
            }
            v << "            default: " << port << " = 0;\n        endcase\n";
        }
    }
    v << "\n    // fu_out: a unit's output, or past the units a result register once the run is done\n"
         "    always @*\n        case(fu_sel)\n";
    for(size_t u = 0; u < units.size(); u++){
        v << "            " << u << ": fu_out = " << units[u] << "_y;\n";
    }
    for(size_t k = 0; k < result.size(); k++){
        v << "            " << units.size() + k << ": fu_out = done ? r" << reg_of[result[k]] << " : 0; // " << dag[result[k]].out << "\n";
    }
    v << "            default: fu_out = 0;\n        endcase\n\n"
         "    // the inputs load from the switches; a register loads on the step that ends a\n"
         "    // control step enabling it, from the unit its select picks\n"
         "    always @(posedge clk)\n"
         "        if(rst) begin\n";
    for(size_t k = 0; k < inputs.size(); k++){
        v << "            in" << k << " <= 8'd" << defaults[k] << ";\n";
    }
    v << "        end else begin\n";
    if(!inputs.empty()){
        v << "            if(ld)\n                case(ld_sel)\n";
        for(size_t k = 0; k < inputs.size() && k < 16; k++){
            v << "                    " << k << ": in" << k << " <= ld_val;\n";
        }
        v << "                endcase\n";
    }
    for(size_t r = 0; r < reg_list.size(); r++){
        v << "            if(step && en_r" << r << ")\n";
        if(loads_from[r].size() == 1){
            v << "                r" << r << " <= " << loads_from[r][0] << ";\n";
        }
        else{
            v << "                case(sel_r" << r << ")\n";
            for(size_t k = 0; k + 1 < loads_from[r].size(); k++){
                v << "                    " << k << ": r" << r << " <= " << loads_from[r][k] << ";\n";
            }
            v << "                    default: r" << r << " <= " << loads_from[r].back() << ";\n                endcase\n";
        }
    }
    v << "        end\nendmodule\n";

    // testbench: every operation's result on its unit in its control step and every result
    // register once done, for the hardcoded inputs and for two sets loaded the way the
    // switches load them
    ofstream t("generated/testbench.v");
    t << "`timescale 1ns/1ps\n"
         "module testbench;\n"
         "    reg clk = 0, rst = 1, step = 0, ld = 0;\n"
         "    reg [3:0] ld_sel = 0;\n"
         "    reg [7:0] ld_val = 0;\n"
         "    reg [4:0] fu_sel = 0;\n"
         "    wire [4:0] cyc;\n"
         "    wire [7:0] fu_out;\n"
         "    integer errors = 0;\n"
         "    hls_core dut (.clk(clk), .rst(rst), .step(step), .ld(ld), .ld_sel(ld_sel), .ld_val(ld_val),\n"
         "                  .fu_sel(fu_sel), .cyc(cyc), .fu_out(fu_out));\n"
         "    always #10 clk = ~clk;\n\n"
         "    task check(input [4:0] unit, input [7:0] value);\n"
         "        begin\n"
         "            fu_sel = unit;\n"
         "            #1 if (fu_out !== value) begin\n"
         "                errors = errors + 1;\n"
         "                $display(\"FAIL cycle %0d unit %0d: got %0d, expected %0d\", cyc, unit, fu_out, value);\n"
         "            end\n"
         "        end\n"
         "    endtask\n\n"
         "    task load(input [3:0] slot, input [7:0] value);\n"
         "        begin\n"
         "            ld = 1; ld_sel = slot; ld_val = value;\n"
         "            @(posedge clk) #1 ld = 0;\n"
         "        end\n"
         "    endtask\n\n"
         "    task do_step;\n"
         "        begin\n"
         "            step = 1;\n"
         "            @(posedge clk) #1 step = 0;\n"
         "        end\n"
         "    endtask\n\n"
         "    initial begin\n";
    mt19937 rng(1);
    for (int run = 0; run < 3; run++)
    {
        vector<int> in = defaults;
        for (size_t k = 0; run > 0 && k < in.size() && k < 16; k++)
            in[k] = rng() % 64;
        t << "        // inputs:";
        for (size_t k = 0; k < inputs.size(); k++)
            t << " " << inputs[k] << "=" << in[k];
        t << "\n        rst = 1;\n        @(posedge clk) #1 rst = 0;\n";
        for (size_t k = 0; run > 0 && k < in.size() && k < 16; k++)
            t << "        load(" << k << ", " << in[k] << ");\n";
        vector<int> value = evaluate(in);
        for(size_t k = 0; k < result.size(); k++){
            t << "        check(" << units.size() + k << ", 0); // " << dag[result[k]].out << " shows 00 until done\n";
        }
        for (int c = 0; c < cycles; c++)
        {
            for (int i = 0; i < n; i++)
                if (schedule[i] == c)
                    t << "        check(" << unit_of(i) << ", " << value[i] << "); // cycle " << c << ": " << dag[i].out
                      << "\n";
            t << "        do_step;\n";
        }
        for(size_t k = 0; k < result.size(); k++){
            t << "        check(" << units.size() + k << ", " << value[result[k]] << "); // done: " << dag[result[k]].out << "\n";
        }
    }
    t << "        if (errors == 0) $display(\"PASS\");\n        else $display(\"FAIL: %0d errors\", errors);\n"
         "        $finish;\n    end\nendmodule\n";

    // what the board shows for the hardcoded inputs
    cout << endl << "wrote generated/hls_core.v and generated/testbench.v (" << name << " schedule)" << endl << "SW[4:0] unit:";
    for (size_t u = 0; u < units.size(); u++)
        cout << " " << u << "=" << units[u];
    cout << endl << "SW[4:0] result, once done:";
    for(size_t k = 0; k < result.size(); k++){
        cout << " " << units.size() + k << "=" << dag[result[k]].out;
    }
    cout << endl << "SW[9:6] input:";
    for (size_t k = 0; k < inputs.size() && k < 16; k++)
        cout << " " << k << "=" << inputs[k];
    cout << endl << "expected outputs (hex) for";
    for (size_t k = 0; k < inputs.size(); k++)
        cout << " " << inputs[k] << "=" << defaults[k];
    cout << endl;
    vector<int> value = evaluate(defaults);
    for (int c = 0; c < cycles; c++)
        for (int i = 0; i < n; i++)
            if (schedule[i] == c)
                printf("cycle %d, unit %d: %s = %02X\n", c, unit_of(i), dag[i].out.c_str(), value[i]);
    for(size_t k = 0; k < result.size(); k++){
        printf("done, result %d: %s = %02X\n", int(units.size() + k), dag[result[k]].out.c_str(), value[result[k]]);
    }
}
