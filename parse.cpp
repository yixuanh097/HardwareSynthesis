#include "parse.h"

using namespace std;

// remove leading or trailing white spaces
string trim(string str)
{
    size_t first = str.find_first_not_of(" \t");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

// input format: A = X*Y, extract A, X, Y, operator
int extract_variable(string line, string &lhs, string &in1, string &in2, string &op)
{
    size_t eq_pos = line.find('=');

    if (eq_pos == string::npos)
    {
        return -1;
    }

    size_t op_pos = line.find_first_of("+-*", eq_pos);
    op = line[op_pos];

    lhs = trim(line.substr(0, eq_pos));

    if (op_pos != string::npos)
    {
        in1 = trim(line.substr(eq_pos + 1, op_pos - (eq_pos + 1)));
        in2 = trim(line.substr(op_pos + 1));
    }
    return 0;
}

int extract_constraint(string line, vector<int> &resource)
{
    size_t add_pos = line.find("+:");
    size_t sub_pos = line.find("-:");
    size_t mult_pos = line.find("*:");
    if (add_pos == string::npos && sub_pos == string::npos && mult_pos == string::npos)
    {
        return -1;
    }
    if (add_pos != string::npos)
    {
        resource[0] = (stoi(trim(line.substr(add_pos + 2))));
    }
    if (sub_pos != string::npos)
        resource[1] = (stoi(trim(line.substr(sub_pos + 2))));
    if (mult_pos != string::npos)
        resource[2] = (stoi(trim(line.substr(mult_pos + 2))));
    return 0;
}

// create single assignment form from a file
vector<struct node> ssa(string filename, bool write, vector<int> &resource)
{
    vector<struct node> dag; // list of all nodes
    ifstream file(filename);
    ofstream outFile;

    if (write)
    {
        outFile.open("ssa.txt");
        if (!outFile.is_open())
        {
            cout << "could not open output file ssa.txt" << endl;
        }
    }

    if (!file.is_open())
    {
        cout << "could not open file " << filename << endl;
    }

    map<string, int> variables; // variable name and counts

    string line;
    string lhs, in1, in2;
    string op;

    while (getline(file, line))
    {
        if (extract_variable(line, lhs, in1, in2, op) == 0)
        {
            // operands first, so X = X + 1 reads the X from before this line. A name not
            // seen yet is a primary input or a constant and gets version 0, so assigning
            // it later makes A1 rather than a second A0.
            variables.emplace(in1, 0);
            in1 += to_string(variables[in1]);
            variables.emplace(in2, 0);
            in2 += to_string(variables[in2]);

            // then the result: version 0 for a new name, the next version otherwise
            if(variables.count(lhs))
                variables[lhs]++;
            else
                variables[lhs] = 0;
            lhs += to_string(variables[lhs]);

            if (write)
            {
                outFile << lhs << " = " << in1 << " " << op << " " << in2 << endl;
            }

            // every output assignment is a node
            struct node new_node = {in1, in2, lhs, op};
            dag.push_back(new_node);
        }

        else if (extract_constraint(line, resource) == 0)
        {
            // cout << "added resource limit" << endl;
        }
    }

    file.close();
    outFile.close();
    return dag;
}
