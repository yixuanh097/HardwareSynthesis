#include "verilog.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

struct operand
{
    string expr;    // Verilog source: inK, rK or a constant
    int node = -1;  // node that computes it
    int input = -1; // primary input index
    int value = 0;  // constant value
};

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

    // operands: the task-4 register that carries the value into this cycle, a primary input or a constant
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
            for (size_t r = 0; o.node >= 0 && r < reg_list.size(); r++)
                if (reg_list[r].in == o.node && reg_list[r].out == i && reg_list[r].enable_time == schedule[i])
                    o.expr = "r" + to_string(r);
            if (o.node >= 0 && o.expr.empty())
            {
                cout << "verilog: " << dag[i].out << " reads " << x << " before it is computed" << endl;
                return;
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

    filesystem::create_directories("generated");
    ofstream v("generated/hls_core.v");
    v << "// hls_core.v - generated from the " << name << " schedule by ./main --verilog (8-bit datapath)\n// fu_sel:";
    for (size_t u = 0; u < units.size(); u++)
        v << " " << u << "=" << units[u];
    v << "\nmodule hls_core (\n"
         "    input clk, rst,\n"
         "    input step,           // advance one control step\n"
         "    input ld,             // load ld_val into input ld_sel (inputs 0-15)\n"
         "    input [3:0] ld_sel,\n"
         "    input [7:0] ld_val,\n"
         "    input [4:0] fu_sel,   // functional unit shown on fu_out\n"
         "    output reg [4:0] cyc, // control step, "
      << cycles << " = done\n"
         "    output reg [7:0] fu_out,\n"
         "    output fu_busy        // the unit on fu_out computes something in this control step\n);\n";
    for (size_t k = 0; k < inputs.size(); k++)
        v << "    reg [7:0] in" << k << " = 8'd" << defaults[k] << "; // " << inputs[k] << "\n";
    for (size_t r = 0; r < reg_list.size(); r++)
        v << "    reg [7:0] r" << r << "; // register " << r << ": " << dag[reg_list[r].in].out << " -> "
          << dag[reg_list[r].out].out << "\n";
    string operands;
    for (string u : units)
        operands += (operands.empty() ? "" : ", ") + u + "_a, " + u + "_b";
    v << "    reg [7:0] " << operands << ";\n";
    // one bit per fu_sel value, so a number with no unit reads as idle
    v << "    reg [31:0] busy; // bit u: unit u computes something in this control step\n";
    for (size_t u = 0; u < units.size(); u++)
        v << "    wire [7:0] " << units[u] << "_y;\n    " << mods[u] << " u_" << units[u] << " (" << units[u] << "_a, "
          << units[u] << "_b, " << units[u] << "_y);\n";

    v << "\n    // multiplexers: the operands of every unit in the current control step\n    always @* begin\n        {"
      << operands << "} = 0;\n        busy = 0;\n        case (cyc)\n";
    for (int c = 0; c < cycles; c++)
    {
        v << "            " << c << ": begin\n";
        for (int i = 0; i < n; i++)
            if (schedule[i] == c)
                v << "                " << units[unit_of(i)] << "_a = " << ops[i][0].expr << "; " << units[unit_of(i)]
                  << "_b = " << ops[i][1].expr << "; busy[" << unit_of(i) << "] = 1; // " << dag[i].out << " = "
                  << dag[i].in1 << " " << dag[i].op << " " << dag[i].in2 << "\n";
        v << "            end\n";
    }
    v << "        endcase\n    end\n\n    always @*\n        case (fu_sel)\n";
    for (size_t u = 0; u < units.size(); u++)
        v << "            " << u << ": fu_out = " << units[u] << "_y;\n";
    v << "            default: fu_out = 0;\n        endcase\n    assign fu_busy = busy[fu_sel];\n\n"
         "    // sequencer: each step ends a control step and loads the registers enabled then\n"
         "    always @(posedge clk)\n        if (rst) begin\n            cyc <= 0;\n";
    for (size_t k = 0; k < inputs.size(); k++)
        v << "            in" << k << " <= 8'd" << defaults[k] << ";\n";
    v << "        end else begin\n";
    if (!inputs.empty())
    {
        v << "            if (ld)\n                case (ld_sel)\n";
        for (size_t k = 0; k < inputs.size() && k < 16; k++)
            v << "                    " << k << ": in" << k << " <= ld_val;\n";
        v << "                endcase\n";
    }
    v << "            if (step && cyc < " << cycles << ") begin\n                cyc <= cyc + 1;\n";
    string load_cases;
    for (int c = 0; c < cycles; c++)
    {
        string loads;
        for (size_t r = 0; r < reg_list.size(); r++)
        {
            if (reg_list[r].enable_time != c + 1)
                continue;
            // the first register of a chain takes the unit output, the next ones the register before
            string from = units[unit_of(reg_list[r].in)] + "_y";
            for (size_t q = 0; q < reg_list.size(); q++)
                if (reg_list[q].in == reg_list[r].in && reg_list[q].out == reg_list[r].out && reg_list[q].enable_time == c)
                    from = "r" + to_string(q);
            loads += " r" + to_string(r) + " <= " + from + ";";
        }
        if (!loads.empty())
            load_cases += "                    " + to_string(c) + ": begin" + loads + " end\n";
    }
    if (!load_cases.empty())
        v << "                case (cyc)\n" << load_cases << "                endcase\n";
    v << "            end\n        end\nendmodule\n";

    // what the board shows for the hardcoded inputs
    cout << endl << "wrote generated/hls_core.v (" << name << " schedule)" << endl << "SW[4:0] unit:";
    for (size_t u = 0; u < units.size(); u++)
        cout << " " << u << "=" << units[u];
    cout << endl << "SW[9:6] input:";
    for (size_t k = 0; k < inputs.size() && k < 16; k++)
        cout << " " << k << "=" << inputs[k];
    cout << endl << "expected unit outputs (hex) for";
    for (size_t k = 0; k < inputs.size(); k++)
        cout << " " << inputs[k] << "=" << defaults[k];
    cout << endl;
    vector<int> value = evaluate(defaults);
    for (int c = 0; c < cycles; c++)
        for (int i = 0; i < n; i++)
            if (schedule[i] == c)
                printf("cycle %d, unit %d: %s = %02X\n", c, unit_of(i), dag[i].out.c_str(), value[i]);
    cout << "units not listed for a cycle show --" << endl;
}
