#include <iostream>
#include <string>
#include <stdio.h>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
using namespace std;

//global variables & const

const map <string, string> Register = {
    {"$zero","00000"},
    {"$at", "00001"},
    {"$v0", "00010"},
    {"$v1", "00011"},
    {"$a0", "00100"},
    {"$a1", "00101"},
    {"$a2", "00110"},
    {"$a3", "00111"},
    {"$t0", "01000"},
    {"$t1", "01001"},
    {"$t2", "01010"},
    {"$t3", "01011"},
    {"$t4", "01100"},
    {"$t5", "01101"},
    {"$t6", "01110"},
    {"$t7", "01111"},
    {"$s0", "10000"},
    {"$s1", "10001"},
    {"$s2", "10010"},
    {"$s3", "10011"},
    {"$s4", "10100"},
    {"$s5", "10101"},
    {"$s6", "10110"},
    {"$s7", "10111"},
    {"$t8", "11000"},
    {"$t9", "11001"},
    {"$k0", "11010"},
    {"$k1", "11011"},
    {"$gp", "11100"},
    {"$sp", "11101"},
    {"$fp", "11110"},
    {"$ra", "11111"}
};

//files
ifstream input_mips;
ifstream syscall_input;
ofstream output_file;

void* real_mem = malloc(6 * 1024 * 1024);
void* data_mem = ((char*)real_mem + 0x100000);
int PC = 0x400000;
int* pc = ((int*)real_mem + PC - 4194304);

//registers stored in an array
int tReg[34] = { 0,0,0,0,0,
                0,0,0,0,0,
                0,0,0,0,0,
                0,0,0,0,0,
                0,0,0,0,0,
                0,0,0,0x500000,0xa00000,
                0x500000,0,0,0 };


//address of labels in decimal
map <string, int> Label;

//PC, starting from 0
int cursor = 0;


//read in the data
vector<string> read() {
    string i;
    vector<string>Token;
    while (getline(input_mips, i)) {
        if (i.find_first_not_of(" ") != i.npos) Token.push_back(i);// remove empty lines
    }
    return Token;
}

//clean comments and comment-only lines
vector<string> cleanFile(vector<string> file) {
    string instruction;
    int fileSize = file.size();
    int i = 0;
    vector<string>clean_file;
    while (i < fileSize) {
        instruction = file[i];
        if (instruction.find("#") != instruction.npos) instruction = instruction.substr(0, instruction.find_first_of("#"));
        if (instruction.find_first_not_of(" ") != instruction.npos) {
            clean_file.push_back(instruction);
        }
        i++;
    }
    return clean_file;
}

//split the instructiona and remove the blanks && coma && parenthesis
//for example:
//string:"                     add      $t0,    $t1,    $2"
//vector:["add","$t0","$t1","$t2"]
//string:"                      lw $s0,   24($s3)         "
//vector:["lw","$s0","24","$s3"]
vector<string> split(string instruction) {
    vector<string> Tokenized;
    int cursor = 0;
    while (cursor < instruction.size()) {
        string part = "";
        while (((instruction[cursor] == ' ') || (instruction[cursor] == ',') || (instruction[cursor] == '(') || (instruction[cursor] == ')') \
            || (instruction[cursor] == '\t')) && (cursor < instruction.size())) {
            cursor++;
        }
        while ((instruction[cursor] != ' ') && (instruction[cursor] != ',') && (instruction[cursor] != '(') && (instruction[cursor] != ')')\
            && (instruction[cursor] != '\t') && (cursor < instruction.size())) {
            part += instruction[cursor];
            cursor++;
        }
        if (part != "") Tokenized.push_back(part);
        for (int i = 0; i < Tokenized.size(); i++) {
            if (((Tokenized[i].at(0) == '-') || ((Tokenized[i].at(0) <= '9') && (Tokenized[i].at(0) >= '0'))) && //find a immediate
                i < Tokenized.size() - 1) {// there is another element after the immediate
                string temp = Tokenized[i];
                Tokenized[i] = Tokenized[i + 1];
                Tokenized[i + 1] = temp;
            }
        }
    }
    //check for instructions like "lb $t0, ($t2)" that has parenthesis but no immediate
    if ((instruction.find("(") != instruction.npos) && (Tokenized.back()[0] == '$')) Tokenized.push_back("0");
    return Tokenized;
}


//recursively add 1 to a binary string number
string bin_add(string num, int cursor) {
    if (cursor >= 0) {
        if (num[cursor] == '1') {
            num[cursor] = '0';
            num = bin_add(num, cursor - 1);
        }
        else {
            num[cursor] = '1';
        }
    }
    return num;
}

// convert a binary number string to a decimal int
int unsigned_bin_to_dec(string bin) {
    unsigned int ans = 0;
    for (int i = 0; i < bin.size(); i++) {
        int digit = bin[i] - '0';
        ans = ans * 2 + digit;
    }
    return ans;
}

int signed_bin_to_dec(string num) {
    if (num[0] == '0') return unsigned_bin_to_dec(num);
    for (int j = 0; j < num.size(); j++) {
        if (num[j] == '0') num[j] = '1';
        else num[j] = '0';
    }
    num = bin_add(num, num.size() - 1);
    int abs = unsigned_bin_to_dec(num);
    return ((-1) * abs);
}

//transfer given string of numbers to the signed number of given bit
string transfer_to_signed(string imm, int bit) {
    bool negative = false;
    //check if imm negative
    if (imm[0] == '-') {
        imm = imm.substr(1);
        negative = true;
    }

    //string imm to int type n
    stringstream stream;
    int n;
    stream << imm;
    stream >> n;
    stream.clear();

    //convert n to binary
    int carrier;
    string ans = "";
    while (n != 0) {
        carrier = n % 2;
        if (carrier == 1) ans += "1";
        else ans += "0";
        n = n / 2;
    }

    string rev = "";
    for (int i = ans.size() - 1; i >= 0; i--) rev += ans[i];

    //convert bin to xx-bit binary
    string st = "";
    for (int i = 0; i < bit - rev.size(); i++) {
        st += "0";
    }
    st += rev;

    if (negative == true) {
        for (int j = 0; j < st.size(); j++) {
            if (st[j] == '0') st[j] = '1';
            else st[j] = '0';
        }
        st = bin_add(st, st.size() - 1);// will this work???????????????????????????????
    }
    return st;
}


//return the string version of the (label - current PC)/4
string findLabel(string label_s) {
    int addrFromLabel = Label.at(label_s);
    int dif = (addrFromLabel - (cursor + 4)) / 4;
    stringstream ss;
    string tar;
    ss << dif;
    ss >> tar;
    ss.clear();
    return tar;
}

//return the simulated memory address of the label
string findAdd(string target_s) {
    stringstream ss;
    int lAdd = Label.at(target_s);
    int add = (lAdd + 4194304) / 4;
    string Add;
    ss << add;
    ss >> Add;
    return Add;
}

struct R {
    string instruction;//no empty lines and no comments, potential blanks and coma
    int address;
    string op;
    string rs;
    string rt;
    string rd;
    string shamt;
    string funct;
    vector<string>tokenized;
    string op_s;

    void init() {
        tokenized = split(instruction);
        op_s = tokenized[0];
    }

    void interpret() {
        init();
        if (op_s == "add") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "100000";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "addu") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "100001";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "and") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "100100";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "clo") {
            string rs_s = tokenized[2];
            string rd_s = tokenized[1];
            op = "011100";
            shamt = "000000";
            funct = "100001";
            rs = Register.at(rs_s);
            rt = "00000";
            rd = Register.at(rd_s);
        }
        else if (op_s == "clz") {
            string rs_s = tokenized[2];
            string rd_s = tokenized[1];
            op = "011100";
            shamt = "00000";
            funct = "100000";
            rs = Register.at(rs_s);
            rt = "00000";
            rd = Register.at(rd_s);
        }
        else if (op_s == "div") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "000000";
            shamt = "00000";
            funct = "011010";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = "00000";
        }
        else if (op_s == "divu") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "000000";
            shamt = "00000";
            funct = "011011";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = "00000";
        }
        else if (op_s == "mult") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "000000";
            shamt = "00000";
            funct = "011000";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = "00000";
        }
        else if (op_s == "multu") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "000000";
            shamt = "00000";
            funct = "011001";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = "00000";
        }
        else if (op_s == "mul") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "000010";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "madd") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "011100";
            shamt = "00000";
            funct = "000000";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = "00000";
        }
        else if (op_s == "maddu") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "011100";
            shamt = "00000";
            funct = "000001";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = "00000";
        }
        else if (op_s == "msub") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "011100";
            shamt = "00000";
            funct = "000100";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = "00000";
        }
        else if (op_s == "msubu") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "011100";
            shamt = "00000";
            funct = "000101";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = "00000";
        }
        else if (op_s == "nor") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "100111";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "or") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "100101";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "sll") {
            string rt_s = tokenized[2];
            string rd_s = tokenized[1];
            string shamt_s = tokenized[3];
            op = "000000";
            rs = "00000";
            funct = "000000";
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
            shamt = transfer_to_signed(shamt_s, 5);
        }
        else if (op_s == "sllv") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "000100";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "sra") {
            string rt_s = tokenized[2];
            string rd_s = tokenized[1];
            string shamt_s = tokenized[3];
            op = "000000";
            rs = "00000";
            funct = "000011";
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
            shamt = transfer_to_signed(shamt_s, 5);
        }
        else if (op_s == "srav") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "000111";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "srl") {
            string rt_s = tokenized[2];
            string rd_s = tokenized[1];
            string shamt_s = tokenized[3];
            op = "000000";
            rs = "00000";
            funct = "000010";
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
            shamt = transfer_to_signed(shamt_s, 5);
        }
        else if (op_s == "srlv") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "000110";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "sub") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "100010";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "subu") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "100011";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "xor") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "100110";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "slt") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "101010";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "sltu") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[3];
            string rd_s = tokenized[1];
            op = "000000";
            shamt = "00000";
            funct = "101011";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "jalr") {
            string rs_s = tokenized[1];
            string rd_s = "$ra";
            if (tokenized.size() > 2) string rd_s = tokenized[2];
            op = "000000";
            rd = "00000";
            shamt = "00000";
            funct = "001001";
            rs = Register.at(rs_s);
            rd = Register.at(rd_s);
        }
        else if (op_s == "jr") {
            string rs_s = tokenized[1];
            op = "000000";
            rt = "00000";
            rd = "00000";
            shamt = "00000";
            funct = "001000";
            rs = Register.at(rs_s);
        }
        else if (op_s == "teq") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "000000";
            rd = "00000";
            shamt = "00000";
            funct = "110100";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
        }
        else if (op_s == "tne") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "000000";
            rd = "00000";
            shamt = "00000";
            funct = "110110";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
        }
        else if (op_s == "tge") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "000000";
            rd = "00000";
            shamt = "00000";
            funct = "110000";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
        }
        else if (op_s == "tgeu") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "000000";
            rd = "00000";
            shamt = "00000";
            funct = "110001";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
        }
        else if (op_s == "tlt") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "000000";
            rd = "00000";
            shamt = "00000";
            funct = "110010";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
        }
        else if (op_s == "tltu") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            op = "000000";
            rd = "00000";
            shamt = "00000";
            funct = "110011";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
        }
        else if (op_s == "mfhi") {
            string rd_s = tokenized[1];
            op = "000000";
            rs = "00000";
            rt = "00000";
            shamt = "00000";
            funct = "010000";
            rd = Register.at(rd_s);
        }
        else if (op_s == "mflo") {
            string rd_s = tokenized[1];
            op = "000000";
            rs = "00000";
            rt = "00000";
            shamt = "00000";
            funct = "010010";
            rd = Register.at(rd_s);
        }
        else if (op_s == "rs") {
            string rs_s = tokenized[1];
            op = "000000";
            rt = "00000";
            rd = "00000";
            shamt = "00000";
            funct = "010001";
            rs = Register.at(rs_s);
        }
        else if (op_s == "syscall") {
            op = "000000";
            rs = "00000";
            rt = "00000";
            rd = "00000";
            shamt = "00000";
            funct = "001100";
        }




    }

};

struct I {
    string instruction;//no empty lines and no comments, potential blanks and coma
    int address;
    string op;
    string rs;
    string rt;
    string immediate = "";
    string label = "";
    vector<string>tokenized;
    string op_s;

    void init() {
        tokenized = split(instruction);
        op_s = tokenized[0];
    }

    void interpret() {
        init();
        if (op_s == "addi") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[1];
            string imm = tokenized[3];
            op = "001000";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "addiu") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[1];
            string imm = tokenized[3];
            op = "001001";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "andi") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[1];
            string imm = tokenized[3];
            op = "001100";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);

        }
        else if (op_s == "ori") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[1];
            string imm = tokenized[3];
            op = "001101";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "xori") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[1];
            string imm = tokenized[3];
            op = "001110";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "lui") {
            string rt_s = tokenized[1];
            string imm = tokenized[2];
            op = "001111";
            rs = "00000";
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "slti") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[1];
            string imm = tokenized[3];
            op = "001010";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "sltiu") {
            string rs_s = tokenized[2];
            string rt_s = tokenized[1];
            string imm = tokenized[3];
            op = "001011";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "beq") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            string label_s = findLabel(tokenized[3]);
            op = "000100";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            label = transfer_to_signed(label_s, 16);
        }
        else if (op_s == "bgez") {
            string rs_s = tokenized[1];
            string label_s = findLabel(tokenized[3]);
            op = "000001";
            rt = "00001";
            rs = Register.at(rs_s);
            label = transfer_to_signed(label_s, 16);
        }
        else if (op_s == "bgezal") {
            string rs_s = tokenized[1];
            string label_s = findLabel(tokenized[3]);
            op = "000001";
            rt = "10001";
            label = transfer_to_signed(label_s, 16);
        }
        else if (op_s == "bgtz") {
            string rs_s = tokenized[1];
            string label_s = findLabel(tokenized[3]);
            op = "000111";
            rt = "00000";
            rs = Register.at(rs_s);
            label = transfer_to_signed(label_s, 16);
        }
        else if (op_s == "blez") {
            string rs_s = tokenized[1];
            string label_s = findLabel(tokenized[2]);
            op = "000110";
            rt = "00000";
            rs = Register.at(rs_s);
            label = transfer_to_signed(label_s, 16);
        }
        else if (op_s == "bltzal") {
            string rs_s = tokenized[1];
            string label_s = findLabel(tokenized[3]);
            op = "000001";
            rt = "10000";
            rs = Register.at(rs_s);
            label = transfer_to_signed(label_s, 16);
        }
        else if (op_s == "bltz") {
            string rs_s = tokenized[1];
            string label_s = findLabel(tokenized[3]);
            op = "000001";
            rt = "00000";
            rs = Register.at(rs_s);
            label = transfer_to_signed(label_s, 16);
        }
        else if (op_s == "bne") {
            string rs_s = tokenized[1];
            string rt_s = tokenized[2];
            string label_s = findLabel(tokenized[3]);
            op = "000101";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            label = transfer_to_signed(label_s, 16);
        }
        else if (op_s == "teqi") {
            string rs_s = tokenized[1];
            string imm = tokenized[2];
            op = "000001";
            rt = "01100";
            rs = Register.at(rs_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "tnei") {
            string rs_s = tokenized[1];
            string imm = tokenized[2];
            op = "000001";
            rt = "01110";
            rs = Register.at(rs_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "tgei") {
            string rs_s = tokenized[1];
            string imm = tokenized[2];
            op = "000001";
            rt = "01000";
            rs = Register.at(rs_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "tgeiu") {
            string rs_s = tokenized[1];
            string imm = tokenized[2];
            op = "000001";
            rt = "01001";
            rs = Register.at(rs_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "tlti") {
            string rs_s = tokenized[1];
            string imm = tokenized[2];
            op = "000001";
            rt = "01010";
            rs = Register.at(rs_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "tltiu") {
            string rs_s = tokenized[1];
            string imm = tokenized[2];
            op = "000001";
            rt = "01011";
            rs = Register.at(rs_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "lb") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "100000";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "lbu") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "100100";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "lh") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "100001";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "lhu") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "100101";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "lw") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "100011";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "lwl") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "100010";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "lwr") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "100110";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "ll") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "110000";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "sb") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "101000";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "sh") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "101001";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "sw") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "101011";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "swl") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "101010";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "swr") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "101110";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }
        else if (op_s == "sc") {
            string rt_s = tokenized[1];
            string rs_s = tokenized[2];
            string imm = tokenized[3];
            op = "111000";
            rs = Register.at(rs_s);
            rt = Register.at(rt_s);
            immediate = transfer_to_signed(imm, 16);
        }

    }

};

struct J {
    string instruction;
    string op;
    string target;
    vector<string> tokenized;
    string op_s;

    void init() {
        tokenized = split(instruction);
        op_s = tokenized[0];
    }

    void interpret() {
        init();
        if (op_s == "j") {
            op = "000010";
            if ((tokenized[1].at(0) <= '9') && ('0' <= tokenized[1].at(0))) {
                string target_s = tokenized[1];
                // divide by 4
                int n;
                stringstream ss;
                ss << target_s;
                ss >> n;
                ss.clear();
                n = n / 4;
                ss << n;
                ss >> target_s;
                ss.clear();
                // convert
                target = transfer_to_signed(target_s, 26);
            }
            else {
                string label_s = tokenized[1];
                label_s = findAdd(label_s);
                target = transfer_to_signed(label_s, 26);
            }
        }
        else if (op_s == "jal") {
            op = "000011";
            if ((tokenized[1].at(0) <= '9') && ('0' <= tokenized[1].at(0))) {
                string target_s = tokenized[1];
                //divide by 4
                int n;
                stringstream ss;
                ss << target_s;
                ss >> n;
                ss.clear();
                n = n / 4;
                ss << n;
                ss >> target_s;
                ss.clear();
                //convert
                target = transfer_to_signed(target_s, 26);
            }
            else {
                string label_s = tokenized[1];
                label_s = findAdd(label_s);
                target = transfer_to_signed(label_s, 26);
            }
        }
    }
};

//record label address first and remove the label from the instructions
vector<string> recordLabel(vector<string> Instructions) {
    int i = 0;
    bool text = true;
    vector<string> cl_instructions;
    while ((i < Instructions.size())) {
        string line = Instructions[i];
        if (line.find(".data") != line.npos) {
            text = false;
            cl_instructions.push_back(line);
        }
        else if (line.find(".text") != line.npos) {
            text = true;
            cl_instructions.push_back(line);
        }
        //might be instructions or just label
        else if (text == true) {
            if (line.find_first_of(':') != line.npos) {
                string lab = line.substr(0, line.find_first_of(":"));
                //remove blanks in the right and left of the label
                if (lab[0] == ' ') lab = lab.substr(lab.find_first_not_of(' '));
                if (lab[lab.size() - 1] == ' ') lab = lab.substr(0, lab.find_last_not_of(' ') + 1);
                Label.insert(pair<string, int>(lab, cursor));
                line = line.substr(line.find_first_of(":") + 1);
            }
            if (line.find_first_not_of(" ") != line.npos) {
                cl_instructions.push_back(line);
                cursor += 4;
            }
        }
        else if (text == false) cl_instructions.push_back(line);
        i++;
    }
    return cl_instructions;
}

char findType(string cl_instruction) {
    vector<string> parts = split(cl_instruction);
    string op_s = parts[0];
    if ((op_s == "j") || (op_s == "jal")) return 'J';
    if ((op_s == "addi") || (op_s == "addiu") || (op_s == "andi") || (op_s == "ori") || (op_s == "xori") \
        || (op_s == "lui") || (op_s == "slti") || (op_s == "sltiu") || (op_s == "beq") || (op_s == "bgez")\
        || (op_s == "bgezal") || (op_s == "bgtz") || (op_s == "blez") || (op_s == "bltz") || (op_s == "bltzal")\
        || (op_s == "bne") || (op_s == "teqi") || (op_s == "tnei") || (op_s == "tgei") || (op_s == "tgeiu")\
        || (op_s == "tlti") || (op_s == "tltiu") || (op_s == "lb") || (op_s == "lbu") || (op_s == "lh")\
        || (op_s == "lhu") || (op_s == "lw") || (op_s == "lwl") || (op_s == "lwr") || (op_s == "ll") \
        || (op_s == "sb") || (op_s == "sh") || (op_s == "sw") || (op_s == "swl") || (op_s == "swr") \
        || (op_s == "sc")) return 'I';
    return 'R';
}


//.byte, b1, b2, b3, b4, b5, b6
//.word, w1, w2, w3
//.half, h1, h2, h3
void findData(string line, int storage_type) {
    int count = 0;
    string part;
    int i;
    if (storage_type == 0) i = line.find(".word") + 5;
    else if (storage_type == 1) i = line.find(".byte") + 5;
    else i = line.find(".half") + 5;
    while (i < line.size()) {
        part = "";
        while ((i < line.size()) && (line[i] != ' ') && (line[i] != '\t') && (line[i] != ',')) {
            part += line[i];
            i++;
        }
        if (part != "") {
            if (storage_type == 0) {
                if (part.find("0x") != part.npos) *(int32_t*)data_mem = stoi(part, 0, 16);
                else *(int32_t*)data_mem = stoi(part, 0, 10);
                data_mem = ((int32_t*)data_mem + 1);
                count += 4;
                tReg[30] += 4;
            }
            else if (storage_type == 1) {
                if (part.find("0x") != part.npos) *(int8_t*)data_mem = stoi(part, 0, 16);
                else *(int8_t*)data_mem = stoi(part, 0, 10);
                data_mem = ((int8_t*)data_mem + 1);
                count += 1;
                tReg[30] += 1;
            }
            else {
                if (part.find("0x") != part.npos) *(int16_t*)data_mem = stoi(part, 0, 16);
                else *(int16_t*)data_mem = stoi(part, 0, 10);
                data_mem = ((int16_t*)data_mem + 1);
                count += 2;
                tReg[30] += 2;
            }
        }
        i++;
    }
    int left = 4 - (count % 4);
    if (left != 4) {
        data_mem = ((char*)data_mem + left);
        tReg[30] += left;
    }
}

//binary strings to be converted to decimal number and stored in memory
void memory_simulation() {
    string intStr;

    //stores the split instruction when in data segment: name + storage_type + value
    //storing comment-free instructios
    vector<string>Instructions;
    Instructions = read();
    Instructions = cleanFile(Instructions);

    //storing label-free instructions (data segment still has label)
    vector<string> cl_instructions = recordLabel(Instructions);
    cursor = 0;
    int i = 0;
    bool text = true;
    while ((i < cl_instructions.size())) {
        string line = cl_instructions[i];
        if (line.find(".data") != line.npos) text = false;
        else if (line.find(".text") != line.npos) text = true;
        else if (text == true) {
            if (findType(line) == 'R') {
                R r_ins;
                r_ins.instruction = line;
                r_ins.interpret();
                intStr = r_ins.op + r_ins.rs + r_ins.rt + r_ins.rd + r_ins.shamt + r_ins.funct;
                *(int*)((char*)real_mem + cursor) = signed_bin_to_dec(intStr);
            }
            else if (findType(line) == 'I') {
                I i_ins;
                i_ins.instruction = line;
                i_ins.interpret();
                if (i_ins.label == "") {
                    intStr = i_ins.op + i_ins.rs + i_ins.rt + i_ins.immediate;
                }
                else intStr = i_ins.op + i_ins.rs + i_ins.rt + i_ins.label;
                *(int*)((char*)real_mem + cursor) = signed_bin_to_dec(intStr);
            }
            else if (findType(line) == 'J') {
                J j_ins;
                j_ins.instruction = line;
                j_ins.interpret();
                intStr = j_ins.op + j_ins.target;
                *(int*)((char*)real_mem + cursor) = signed_bin_to_dec(intStr);
            }
            cursor += 4;
        }
        else if (text == false) {
            int simplify = 0;
            if (line.find(".asciiz") != line.npos) {
                string data = line.substr(line.find_first_of('"') + 1, line.find_last_of('"') - line.find_first_of('"') - 1);
                int j = 0;
                while (j < data.size()) {
                    if ((j < data.size() - 1) && (data[j] == 92)) {
                        switch (data[j + 1])
                        {
                        case 'n':
                            *(char*)data_mem = '\n';
                            j++;
                            simplify++;
                            break;
                        case 't':
                            *(char*)data_mem = '\t';
                            j++;
                            simplify++;
                            break;
                        case 'r':
                            *(char*)data_mem = '\r';
                            j++;
                            simplify++;
                            break;
                        case '0':
                            *(char*)data_mem = '\0';
                            j++;
                            simplify++;
                            break;
                        default:
                            *(char*)data_mem = data[j];
                            break;
                        }
                        tReg[30] += 1;
                        data_mem = ((char*)data_mem + 1);
                    }
                    else {
                        *(char*)data_mem = data[j];
                        tReg[30] += 1;
                        data_mem = ((char*)data_mem + 1);
                    }
                    j++;
                }
                *(char*)data_mem = '\0';//asciiz strings end with \0
                tReg[30] += 1;
                data_mem = ((char*)data_mem + 1);
                int left = 4 - ((data.size() + 1 - simplify) % 4);
                if (left != 4) {
                    tReg[30] += left;
                    data_mem = ((char*)data_mem + left);//make sure strings are stored in 4-byte blocks
                }

            }
            else if (line.find(".ascii") != line.npos) {
                string data = line.substr(line.find_first_of('"') + 1, line.find_last_of('"') - line.find_first_of('"') - 1);
                int j = 0;
                while (j < data.size()) {
                    if ((j < data.size() - 1) && (data[j] == 92)) {
                        switch (data[j + 1])
                        {
                        case 'n':
                            *(char*)data_mem = '\n';
                            j++;
                            simplify++;
                            break;
                        case 't':
                            *(char*)data_mem = '\t';
                            j++;
                            simplify++;
                            break;
                        case 'r':
                            *(char*)data_mem = '\r';
                            j++;
                            simplify++;
                            break;
                        case '0':
                            *(char*)data_mem = '\0';
                            j++;
                            simplify++;
                            break;
                        default:
                            *(char*)data_mem = data[j];
                            break;
                        }
                        tReg[30] += 1;
                        data_mem = ((char*)data_mem + 1);
                    }
                    else {
                        *(char*)data_mem = data[j];
                        tReg[30] += 1;
                        data_mem = ((char*)data_mem + 1);
                    }
                    j++;
                }
                int left = 4 - ((data.size() - simplify) % 4);
                if (left != 4) {
                    tReg[30] += left;
                    data_mem = ((char*)data_mem + left);//make sure strings are stored in 4-byte blocks
                }
            }
            else if (line.find(".word") != line.npos) {
                findData(line, 0);
            }
            else if (line.find(".byte") != line.npos) {
                findData(line, 1);
            }
            else if (line.find(".half") != line.npos) {
                findData(line, 2);
            }
        }
        i++;
    }
}

void add(int* rs, int* rt, int* rd) {
    int n = *rs + *rt;
    if (((*rs > 0) && (*rt > 0) && (n < 0)) || ((*rs < 0) && (*rt < 0) && (n > 0))) {
        cout << "add overflow" << endl;
        exit(0);
    }
    *rd = n;
}

void addu(int* rs, int* rt, int* rd) {
    *rd = *rs + *rt;
}

void addi(int* rs, int* rt, int imm) {
    int n = *rs + imm;
    if (((*rs > 0) && (imm > 0) && (n < 0)) || ((*rs < 0) && (imm < 0) && (n > 0))) {
        cout << "addi overflow" << endl;
        exit(0);
    }
    *rt = n;
}

void addiu(int* rs, int* rt, int imm) {
    *rt = *rs + imm;
}

void And(int* rd, int* rs, int* rt) {
    *rd = (*rs & *rt);
}

void andi(int* rt, int* rs, string imm) {
    string extended = "";
    for (int i = 0; i < 16; i++) {
        extended += '0';
    }
    extended = extended + imm;
    string comparison = transfer_to_signed(to_string(*rs), 32);
    string res = "";
    for (int i = 0; i < 32; i++) {
        if ((comparison[i] == '1') && (extended[i] == '1')) res += '1';
        else res += '0';
    }
    *rt = signed_bin_to_dec(res);
}

void clo(int* rd, int* rs) {
    string bin = transfer_to_signed(to_string(*rs), 32);
    if (bin.find('0') == bin.npos) *rd = 32;
    else *rd = bin.find_first_not_of('1');
}

void clz(int* rd, int* rs) {
    string bin = transfer_to_signed(to_string(*rs), 32);
    if (bin.find('1') == bin.npos) *rd = 32;
    else *rd = bin.find_first_not_of('0');
}

void div(int* rs, int* rt) {
    tReg[32] = (*rs) / (*rt);
    tReg[33] = (*rs) % (*rt);
}

void divu(int* rs, int* rt) {
    unsigned int a = *rs;
    unsigned int b = *rt;
    tReg[32] = (a) / (b);//lo
    tReg[33] = (a) % (b);//hi
}

string long_to_signed_bin(int64_t imm, int bit) {
    uint64_t num = imm;
    cout << "num: " << num << endl;
    //convert n to binary
    int carrier;
    string ans = "";
    while (num != 0) {
        carrier = num % 2;
        if (carrier == 1) ans += "1";
        else ans += "0";
        num = num / 2;
    }

    string rev = "";
    for (int i = ans.size() - 1; i >= 0; i--) rev += ans[i];

    //convert bin to xx-bit binary
    string st = "";
    for (int i = 0; i < bit - rev.size(); i++) {
        st += "0";
    }
    st += rev;
    return st;
}

void mult(int* rs, int* rt) {
    int64_t a = *rs;
    int64_t b = *rt;
    int64_t n = a * b;
    string bin = long_to_signed_bin(n, 64);
    string high = bin.substr(0, 32);
    string low = bin.substr(32);
    tReg[33] = signed_bin_to_dec(high);
    tReg[32] = signed_bin_to_dec(low);
}

void multu(int* rs, int* rt) {
    uint64_t a = *rs;
    uint64_t b = *rt;
    int64_t n = a * b;
    string bin = long_to_signed_bin(n, 64);
    string high = bin.substr(0, 32);
    string low = bin.substr(32);
    tReg[33] = signed_bin_to_dec(high);
    tReg[32] = signed_bin_to_dec(low);
}

void mul(int* rd, int* rs, int* rt) {
    int64_t a = *rs;
    int64_t b = *rt;
    int64_t n = a * b;
    string bin = long_to_signed_bin(n, 64);
    string low = bin.substr(32);
    *rd = signed_bin_to_dec(low);
}

void madd(int* rs, int* rt) {
    int64_t a = *rs;
    int64_t b = *rt;
    int64_t n = a * b;
    string bin = long_to_signed_bin(n, 64);
    string high = bin.substr(0, 32);
    string low = bin.substr(32);
    int h = signed_bin_to_dec(high);
    int l = signed_bin_to_dec(low);
    tReg[33] = tReg[33] + h;
    tReg[32] = tReg[32] + l;
}

void maddu(int* rs, int* rt) {
    uint64_t a = *rs;
    uint64_t b = *rt;
    int64_t n = a * b;
    string bin = long_to_signed_bin(n, 64);
    string high = bin.substr(0, 32);
    string low = bin.substr(32);
    int h = signed_bin_to_dec(high);
    int l = signed_bin_to_dec(low);
    tReg[33] = tReg[33] + h;
    tReg[32] = tReg[32] + l;
}

void msub(int* rs, int* rt) {
    int64_t a = *rs;
    int64_t b = *rt;
    int64_t n = a * b;
    string bin = long_to_signed_bin(n, 64);
    string high = bin.substr(0, 32);
    string low = bin.substr(32);
    int h = signed_bin_to_dec(high);
    int l = signed_bin_to_dec(low);
    tReg[33] = tReg[33] - h;
    tReg[32] = tReg[32] - l;
}

void msubu(int* rs, int* rt) {
    uint64_t a = *rs;
    uint64_t b = *rt;
    int64_t n = a * b;
    string bin = long_to_signed_bin(n, 64);
    string high = bin.substr(0, 32);
    string low = bin.substr(32);
    int h = signed_bin_to_dec(high);
    int l = signed_bin_to_dec(low);
    tReg[33] = tReg[33] - h;
    tReg[32] = tReg[32] - l;
}

void nor(int* rd, int* rs, int* rt) {
    *rd = ~((*rs) | (*rt));
}

void Or(int* rd, int* rs, int* rt) {
    *rt = ((*rs) | (*rd));
}

void ori(int* rt, int* rs, string imm) {
    string extended = "";
    for (int i = 0; i < 16; i++) {
        extended += '0';
    }
    extended = extended + imm;
    string comparison = transfer_to_signed(to_string(*rs), 32);
    string res = "";
    for (int i = 0; i < 32; i++) {
        if ((comparison[i] == '1') || (extended[i] == '1')) res += '1';
        else res += '0';
    }
    *rt = signed_bin_to_dec(res);
}

void sll(int* rd, int* rt, int shamt) {
    *rd = (*rt) << shamt;
}

void sllv(int* rd, int* rt, int* rs) {
    *rd = (*rt) << (*rs & 0x1f);
}

void sra(int* rd, int* rt, int shamt) {
    bool negative = false;
    if (*rt < 0) negative = true;
    *rd = (*rt) >> shamt;
    if (negative == true) {
        string bin = transfer_to_signed(to_string(*rd), 32);
        for (int j = 0; j < shamt; j++) if (bin[j] == '0') bin[j] = '1';
        *rd = signed_bin_to_dec(bin);
    }
}

void srav(int* rd, int* rt, int* rs) {
    bool negative = false;
    if (*rt < 0) negative = true;
    *rd = (*rt) >> (*rs & 0x1f);
    if (negative == true) {
        string bin = transfer_to_signed(to_string(*rd), 32);
        for (int j = 0; j < (*rs & 0x1f); j++) if (bin[j] == '0') bin[j] = '1';
        *rd = signed_bin_to_dec(bin);
    }
}

void srl(int* rd, int* rt, int shamt) {
    *rd = (*rt) >> shamt;
}

void srlv(int* rd, int* rt, int* rs) {
    *rd = (*rt) >> (*rs & 0x1f);
}

void sub(int* rd, int* rs, int* rt) {
    int n = (*rs) - (*rt);
    if (((*rs > 0) && (*rt < 0) && (n < 0)) || ((*rs < 0) && (*rt > 0) && (n > 0))) {
        cout << "sub overflow" << endl;
        exit(0);
    }
    *rd = n;
}

void subu(int* rd, int* rs, int* rt) {
    *rd = (*rs) - (*rt);
}

void Xor(int* rd, int* rs, int* rt) {
    *rd = (*rs) xor (*rt);
}

void xori(int* rt, int* rs, string imm) {
    string extended = "";
    for (int i = 0; i < 16; i++) {
        extended += '0';
    }
    extended = extended + imm;
    string comparison = transfer_to_signed(to_string(*rs), 32);
    string res = "";
    for (int i = 0; i < 32; i++) {
        if (comparison[i] != extended[i]) res += '1';
        else res += '0';
    }
    *rt = signed_bin_to_dec(res);
}

void lui(int* rt, int imm) {
    *rt = imm << 16;
}

void slt(int* rd, int* rs, int* rt) {
    if ((*rs) < (*rt)) *rd = 1;
    else *rd = 0;
}

void sltu(int* rd, int* rs, int* rt) {
    unsigned int a = *rs;
    unsigned int b = *rt;
    if (a < b) *rd = 1;
    else *rd = 0;
}

void slti(int* rt, int* rs, string imm) {
    int extended = signed_bin_to_dec(imm);
    if ((*rs) < extended) *rt = 1;
    else *rt = 0;
}

void sltiu(int* rt, int* rs, string imm) {
    int temp = signed_bin_to_dec(imm);
    unsigned extended = temp;
    unsigned int b = *rs;
    if (b < extended) *rt = 1;
    else *rt = 0;
}

void beq(int* rs, int* rt, string offset) {
    if (*rs == *rt) {
        int label = signed_bin_to_dec(offset);//signed
        label = label << 2;
        PC += label;
        pc = (int*)((char*)real_mem + PC - 4194304);
    }
}

void bgez(int* rs, string offset) {
    if (*rs >= 0) {
        int label = signed_bin_to_dec(offset);//signed
        label = label << 2;
        PC = PC + label;
        pc = (int*)((char*)real_mem + PC - 4194304);
    }
}

void bgezal(int* rs, string offset) {
    if (*rs >= 0) {
        int label = signed_bin_to_dec(offset);//signed
        label = label << 2;
        tReg[31] = PC;// store the address of the next instruction
        PC = PC + label;
        pc = (int*)((char*)real_mem + PC - 4194304);
    }
}

void bgtz(int* rs, string offset) {
    if (*rs > 0) {
        int label = signed_bin_to_dec(offset);//signed
        label = label << 2;
        PC = PC + label;
        pc = (int*)((char*)real_mem + PC - 4194304);
    }
}

void blez(int* rs, string offset) {
    if (*rs <= 0) {
        int label = signed_bin_to_dec(offset);//signed
        label = label << 2;
        PC = PC + label;
        pc = (int*)((char*)real_mem + PC - 4194304);
    }
}

void bltzal(int* rs, string offset) {
    if (*rs < 0) {
        int label = signed_bin_to_dec(offset);//signed
        label = label << 2;
        tReg[31] = PC;// store the address of the next instruction
        PC = PC + label;
        pc = (int*)((char*)real_mem + PC - 4194304);
    }
}

void bltz(int* rs, string offset) {
    if (*rs < 0) {
        int label = signed_bin_to_dec(offset);//signed
        label = label << 2;
        PC = PC + label;
        pc = (int*)((char*)real_mem + PC - 4194304);
    }
}

void bne(int* rs, int* rt, string offset) {
    if (*rs != *rt) {
        int label = signed_bin_to_dec(offset);//signed
        label = label << 2;
        PC = PC + label;
        pc = (int*)((char*)real_mem + PC - 4194304);
    }
}

void j(string target) {
    PC = PC & 0xf0000000;
    int off = stoi(target, 0, 2);
    off = off << 2;
    PC += off;
    pc = (int*)((char*)real_mem + PC - 4194304);
}

void jal(string target) {
    tReg[31] = PC;
    PC = PC & 0xf0000000;
    int off = stoi(target, 0, 2);
    off = off << 2;
    PC += off;
    pc = (int*)((char*)real_mem + PC - 4194304);
}

void jalr(int* rs, int* rd = &(tReg[31])) {
    *rd = PC;
    PC = *rs;
    pc = (int*)((char*)real_mem + PC - 4194304);
}

void jr(int* rs) {
    PC = *rs;
    pc = (int*)((char*)real_mem + PC - 4194304);
}

void teq(int* rs, int* rt) {
    if (*rs == *rt) {
        cout << "teq trap" << endl;
        exit(0);
    }
}

void teqi(int* rs, string imm) {
    int ext = signed_bin_to_dec(imm);
    if (*rs == ext) {
        cout << "teqi trap" << endl;
        exit(0);
    }
}

void tne(int* rs, int* rt) {
    if (*rs != *rt) {
        cout << "tne trap" << endl;
        exit(0);
    }
}

void tnei(int* rs, string imm) {
    int ext = signed_bin_to_dec(imm);
    if (*rs != ext) {
        cout << "tnei trap" << endl;
        exit(0);
    }
}

void tge(int* rs, int* rt) {
    if (*rs >= *rt) {
        cout << "tge trap" << endl;
        exit(0);
    }
}

void tgeu(int* rs, int* rt) {
    unsigned int a = *rs;
    unsigned int b = *rt;
    if (a >= b) {
        cout << "tgeu trap" << endl;
        exit(0);
    }
}

void tgei(int* rs, string imm) {
    int ext = signed_bin_to_dec(imm);
    if (*rs >= ext) {
        cout << "tgei trap" << endl;
        exit(0);
    }
}

void tgeiu(int* rs, string imm) {
    int temp = signed_bin_to_dec(imm);
    unsigned ext = temp;
    unsigned int a = *rs;
    if (a >= ext) {
        cout << "tgei trap" << endl;
        exit(0);
    }
}

void tlt(int* rs, int* rt) {
    if (*rs < *rt) {
        cout << "tlt trap" << endl;
        exit(0);
    }
}

void tltu(int* rs, int* rt) {
    unsigned int a = *rs;
    unsigned int b = *rt;
    if (rs < rt) {
        cout << "tltu trap" << endl;
        exit(0);
    }
}

void tlti(int* rs, string imm) {
    int ext = signed_bin_to_dec(imm);
    if (*rs < ext) {
        cout << "tlti trap" << endl;
        exit(0);
    }
}

void tltiu(int* rs, string imm) {
    int temp = signed_bin_to_dec(imm);
    unsigned int ext = temp;
    unsigned int a = *rs;
    if (a <= ext) {
        cout << "tltiu trap" << endl;
        exit(0);
    }
}

void lb(int* rt, int* rs, string offset) {
    int add = (*rs) + signed_bin_to_dec(offset);
    int8_t temp = *((int8_t*)real_mem + add - 4194304);
    int extended = temp;
    *rt = extended;
}

void lbu(int* rt, int* rs, string offset) {// zero extension
    int add = (*rs) + signed_bin_to_dec(offset);
    uint8_t temp = *((int8_t*)real_mem + add - 4194304);
    int ext = temp;
    *rt = ext;
}

void lh(int* rt, int* rs, string offset) {
    int add = (*rs) + signed_bin_to_dec(offset);
    int16_t temp = *(int16_t*)((char*)real_mem + add - 4194304);
    int ext = temp;
    *rt = ext;
}

void lhu(int* rt, int* rs, string offset) {
    int add = (*rs) + signed_bin_to_dec(offset);
    uint16_t temp = *(int16_t*)((char*)real_mem + add - 4194304);
    int ext = temp;
    *rt = ext;
}

void lw(int* rt, int* rs, string offset) {
    int add = (*rs) + signed_bin_to_dec(offset);
    int32_t temp = *(int32_t*)((char*)real_mem + add - 0x400000);
    *rt = temp;
}

void lwl(int* rt, int* rs, string offset) {
    int add = (*rs) + signed_bin_to_dec(offset);
    int n = add & 4;
    int word = *(int32_t*)((char*)real_mem + add - 4194304 - n);
    string bin = transfer_to_signed(to_string(word), 32);
    string left = bin.substr(32 - (n + 1) * 8);
    string reg = transfer_to_signed(to_string(*rt), 32);
    for (int i = 0; i < left.size(); i++) {
        reg[i] = left[i];
    }
    *rt = signed_bin_to_dec(reg);
}

void lwr(int* rt, int* rs, string offset) {
    int add = (*rs) + signed_bin_to_dec(offset);
    int n = add % 4;
    int word = *(int32_t*)((char*)real_mem + add - 4194304 - n);
    string bin = transfer_to_signed(to_string(word), 32);
    string right = bin.substr(0, 32 - n * 8);
    string reg = transfer_to_signed(to_string(*rt), 32);
    for (int i = 0; i < right.size(); i++) {
        reg[i + n * 8] = right[i];
    }
    *rt = signed_bin_to_dec(reg);
}

void ll(int* rt, int* rs, string offset) {
    lw(rt, rs, offset);
}

void sb(int* rt, int* rs, string offset) {
    int add = (*rs) + signed_bin_to_dec(offset);
    int8_t* temp = (int8_t*)((char*)real_mem + add - 4194304);
    string reg = transfer_to_signed(to_string(*rt), 32);
    string low = reg.substr(24);
    int8_t dec = signed_bin_to_dec(low);
    *temp = dec;
}

void sh(int* rt, int* rs, string offset) {
    int add = (*rs) + signed_bin_to_dec(offset);
    int16_t* temp = (int16_t*)((char*)real_mem + add - 4194304);
    string reg = transfer_to_signed(to_string(*rt), 32);
    string low = reg.substr(16);
    int8_t dec = signed_bin_to_dec(low);
    *temp = dec;
}

void sw(int* rt, int* rs, string offset) {
    int add = (*rs) + signed_bin_to_dec(offset);
    int32_t* temp = (int32_t*)((char*)real_mem + add - 4194304);
    *temp = *rt;
}

void swl(int* rt, int* rs, string offset) {
    int add = (*rs) + signed_bin_to_dec(offset);
    int n = add % 4;
    int32_t* temp = (int32_t*)((char*)real_mem + add - 4194304 - n);
    string bin = transfer_to_signed(to_string(*temp), 32);
    string word = transfer_to_signed(to_string(*rt), 32);
    string left = word.substr(0, (n + 1) * 8);
    for (int i = 0; i < left.size(); i++) {
        bin[32 - (left.size()) + i] = left[i];
    }
    *temp = signed_bin_to_dec(bin);
}

void swr(int* rt, int* rs, string offset) {
    int add = (*rs) + signed_bin_to_dec(offset);
    int n = add % 4;
    int32_t* temp = (int32_t*)((char*)real_mem + add - 4194304 - n);
    string bin = transfer_to_signed(to_string(*temp), 32);
    string word = transfer_to_signed(to_string(*rt), 32);
    string right = word.substr(n * 8);
    for (int i = 0; i < right.size(); i++) {
        bin[i] = right[i];
    }
    *temp = signed_bin_to_dec(bin);
}
void sc(int* rt, int* rs, string offset) {
    sw(rt, rs, offset);
}
void mfhi(int* rd) {
    *rd = tReg[33];
}

void mflo(int* rd) {
    *rd = tReg[32];
}

void mthi(int* rs) {
    tReg[33] = *rs;
}

void mtlo(int* rs) {
    tReg[32] = *rs;
}

void syscall() {
    if (tReg[2] == 1) {
        output_file << tReg[4];
    }
    else if (tReg[2] == 4) {
        char* temp = ((char*)real_mem + tReg[4] - 0x400000);
        while (*temp != '\0') {
            output_file << *temp;
            temp++;
        }
    }
    else if (tReg[2] == 5) {
        string temp;
        getline(syscall_input, temp);
        tReg[2] = stoi(temp);
    }
    else if (tReg[2] == 8) {
        string temp;
        getline(syscall_input, temp);
        char* tptr = (char*)((char*)real_mem + tReg[4] - 0x400000);
        int length = min(tReg[5], (int)temp.size());
        for (int i = 0; i < length; i++) {
            *tptr = temp[i];
            tptr++;
        }
        if (length < tReg[5]) {
            for (int i = (int)temp.size() - 1; i < length; i++) {
                *tptr = '\0';
                tptr++;
            }
        }
    }
    else if (tReg[2] == 9) {
        tReg[2] = tReg[30];
        tReg[30] += tReg[4];
        if (tReg[4] % 4 != 0) tReg[30] += 4 - (tReg[4] % 4);
    }
    else if (tReg[2] == 10) {
        exit(0);
    }
    else if (tReg[2] == 11) {
        char temp = tReg[4];
        output_file << temp;
    }
    else if (tReg[2] == 12) {
        char temp;
        syscall_input.get(temp);
        tReg[2] = temp;
    }
    else if (tReg[2] == 13) {
        char* fileName = ((char*)real_mem + tReg[4] - 0x400000);
        int flags = tReg[5];
        int mode = tReg[6];
        int fileDes = open(fileName, flags, mode);
        tReg[2] = fileDes;
    }
    else if (tReg[2] == 14) {
        int fileDes = tReg[4];
        char* buffer = ((char*)real_mem + tReg[5] - 0x400000);
        int length = tReg[6];
        tReg[2] = read(fileDes, buffer, length);
    }
    else if (tReg[2] == 15) {
        int fileDes = tReg[4];
        char* buffer = ((char*)real_mem + tReg[5] - 0x400000);
        int length = tReg[6];
        tReg[2] = write(fileDes, buffer, length);
    }
    else if (tReg[2] == 16) {
        close(tReg[4]);
    }
    else if (tReg[2] == 17) {
        exit(tReg[4]);
    }

}

void simulation() {
    int opera = 0;
    while (*pc != 0) {
        string code = transfer_to_signed(to_string(*pc), 32);
        PC += 4;
        pc = (int*)((char*)real_mem + PC - 0x400000);
        string op = code.substr(0, 6);
        opera += 1;
        if (op == "000000") {
            string funct = code.substr(26);
            if (funct == "100000") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                add(rs, rt, rd);
            }
            else if (funct == "100001") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                addu(rs, rt, rd);
            }
            else if (funct == "100100") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                And(rd, rs, rt);
            }
            else if (funct == "011010") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                div(rs, rt);
            }
            else if (funct == "011011") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                divu(rs, rt);
            }
            else if (funct == "011000") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                mult(rs, rt);
            }
            else if (funct == "011001") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                multu(rs, rt);
            }
            else if (funct == "100111") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                nor(rd, rs, rt);
            }
            else if (funct == "100101") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                Or(rd, rs, rt);
            }
            else if (funct == "000000") {
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                string shamt_b = code.substr(21, 5);
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                int shamt = stoi(shamt_b, 0, 2);
                sll(rd, rt, shamt);
            }
            else if (funct == "000100") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                sllv(rd, rt, rs);
            }
            else if (funct == "000011") {
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                string shamt_b = code.substr(21, 5);
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                int shamt = stoi(shamt_b, 0, 2);
                sra(rd, rt, shamt);
            }
            else if (funct == "000111") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                srav(rd, rt, rs);
            }
            else if (funct == "000111") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                srav(rd, rt, rs);
            }
            else if (funct == "000010") {
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                string shamt_b = code.substr(21, 5);
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                int shamt = stoi(shamt_b, 0, 2);
                srl(rd, rt, shamt);
            }
            else if (funct == "000110") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                srlv(rd, rt, rs);
            }
            else if (funct == "100010") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                sub(rd, rs, rt);
            }
            else if (funct == "100011") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                subu(rd, rs, rt);
            }
            else if (funct == "100110") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                Xor(rd, rs, rt);
            }
            else if (funct == "101010") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                slt(rd, rs, rt);
            }
            else if (funct == "101011") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                sltu(rd, rs, rt);
            }
            else if (funct == "001001") {
                string rs_b = code.substr(6, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                jalr(rs, rd);
            }
            else if (funct == "001000") {
                string rs_b = code.substr(6, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                jr(rs);
            }
            else if (funct == "110100") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                teq(rs, rt);
            }
            else if (funct == "110110") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                tne(rs, rt);
            }
            else if (funct == "110000") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                tge(rs, rt);
            }
            else if (funct == "110001") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                tgeu(rs, rt);
            }
            else if (funct == "110010") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                tlt(rs, rt);
            }
            else if (funct == "110011") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                tltu(rs, rt);
            }
            else if (funct == "010000") {
                string rd_b = code.substr(16, 5);
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                mfhi(rd);
            }
            else if (funct == "010010") {
                string rd_b = code.substr(16, 5);
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                mflo(rd);
            }
            else if (funct == "010001") {
                string rs_b = code.substr(6, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                mthi(rs);
            }
            else if (funct == "010011") {
                string rs_b = code.substr(6, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                mtlo(rs);
            }
            else if (funct == "001100") {
                syscall();
            }
        }
        else if (op == "001000") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string imm_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            int imm = signed_bin_to_dec(imm_b);
            addi(rs, rt, imm);
        }
        else if (op == "001010") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string imm_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            slti(rt, rs, imm_b);
        }
        else if (op == "001011") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string imm_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            sltiu(rt, rs, imm_b);
        }
        else if (op == "000100") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            beq(rs, rt, off_b);
        }
        else if (op == "000001") {
            string rt_b = code.substr(11, 5);
            if (rt_b == "00001") {
                string rs_b = code.substr(6, 5);
                string off_b = code.substr(16);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                bgez(rs, off_b);
            }
            else if (rt_b == "10001") {
                string rs_b = code.substr(6, 5);
                string off_b = code.substr(16);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                bgezal(rs, off_b);
            }
            else if (rt_b == "10000") {
                string rs_b = code.substr(6, 5);
                string off_b = code.substr(16);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                bltzal(rs, off_b);
            }
            else if (rt_b == "00000") {
                string rs_b = code.substr(6, 5);
                string off_b = code.substr(16);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                bltz(rs, off_b);
            }
            else if (rt_b == "01100") {
                string rs_b = code.substr(6, 5);
                string imm_b = code.substr(16);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                teqi(rs, imm_b);
            }
            else if (rt_b == "01110") {
                string rs_b = code.substr(6, 5);
                string imm_b = code.substr(16);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                tnei(rs, imm_b);
            }
            else if (rt_b == "01000") {
                string rs_b = code.substr(6, 5);
                string imm_b = code.substr(16);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                tgei(rs, imm_b);
            }
            else if (rt_b == "01001") {
                string rs_b = code.substr(6, 5);
                string imm_b = code.substr(16);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                tgeiu(rs, imm_b);
            }
            else if (rt_b == "01010") {
                string rs_b = code.substr(6, 5);
                string imm_b = code.substr(16);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                tlti(rs, imm_b);
            }
            else if (rt_b == "01011") {
                string rs_b = code.substr(6, 5);
                string imm_b = code.substr(16);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                tltiu(rs, imm_b);
            }
        }
        else if (op == "100000") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            lb(rt, rs, off_b);
        }
        else if (op == "100100") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            lbu(rt, rs, off_b);
        }
        else if (op == "100001") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            lh(rt, rs, off_b);
        }
        else if (op == "100101") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            lhu(rt, rs, off_b);
        }
        else if (op == "100011") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            lw(rt, rs, off_b);
        }
        else if (op == "100010") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            lwl(rt, rs, off_b);
        }
        else if (op == "100110") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            lwr(rt, rs, off_b);
        }
        else if (op == "110000") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            ll(rt, rs, off_b);
        }
        else if (op == "101000") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            sb(rt, rs, off_b);
        }
        else if (op == "101001") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            sh(rt, rs, off_b);
        }
        else if (op == "101011") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            sw(rt, rs, off_b);
        }
        else if (op == "101010") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            swl(rt, rs, off_b);
        }
        else if (op == "101110") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            swr(rt, rs, off_b);
        }
        else if (op == "111000") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            sc(rt, rs, off_b);
        }
        else if (op == "000101") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            bne(rs, rt, off_b);
        }
        else if (op == "000111") {
            string rs_b = code.substr(6, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            bgtz(rs, off_b);
        }
        else if (op == "000110") {
            string rs_b = code.substr(6, 5);
            string off_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            blez(rs, off_b);
        }
        else if (op == "001111") {
            string rt_b = code.substr(11, 5);
            string imm_b = code.substr(16);
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            int imm = signed_bin_to_dec(imm_b);
            lui(rt, imm);
        }
        else if (op == "001110") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string imm_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            xori(rt, rs, imm_b);
        }
        else if (op == "001001") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string imm_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            int imm = signed_bin_to_dec(imm_b);
            addiu(rs, rt, imm);
        }
        else if (op == "001100") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string imm_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            andi(rt, rs, imm_b);
        }
        else if (op == "001101") {
            string rs_b = code.substr(6, 5);
            string rt_b = code.substr(11, 5);
            string imm_b = code.substr(16);
            int* rs = &tReg[stoi(rs_b, 0, 2)];
            int* rt = &tReg[stoi(rt_b, 0, 2)];
            ori(rt, rs, imm_b);
        }
        else if (op == "011100") {
            string funct = code.substr(26);
            if (funct == "100001") {
                string rs_b = code.substr(6, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                clo(rd, rs);
            }
            else if (funct == "100000") {
                string rs_b = code.substr(6, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                clz(rd, rs);
            }
            else if (funct == "000010") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                string rd_b = code.substr(16, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                int* rd = &tReg[stoi(rd_b, 0, 2)];
                mul(rd, rs, rt);
            }
            else if (funct == "000000") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                madd(rs, rt);
            }
            else if (funct == "000001") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                maddu(rs, rt);
            }
            else if (funct == "000100") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                msub(rs, rt);
            }
            else if (funct == "000101") {
                string rs_b = code.substr(6, 5);
                string rt_b = code.substr(11, 5);
                int* rs = &tReg[stoi(rs_b, 0, 2)];
                int* rt = &tReg[stoi(rt_b, 0, 2)];
                msubu(rs, rt);
            }
        }
        else if (op == "000010") {
            string target_s = code.substr(6);
            j(target_s);
        }
        else if (op == "000011") {
            string target_s = code.substr(6);
            jal(target_s);
        }

    }
}

int main(int argc, char** argv) {
    input_mips.open(argv[1]);
    syscall_input.open(argv[2]);
    output_file.open(argv[3]);
    memory_simulation();
    simulation();
    input_mips.close();
    syscall_input.close();
    output_file.close();
    free(real_mem);
    return 0;
}


