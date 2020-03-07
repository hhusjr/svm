/*
 * SVM, SLang Stack-based Virtual Machine
 * Write once, run anywhere :)
 * Quite appreciate "CPython Virtual Machine", from which I learnt a lot.
 *
 * Usage:
 * $ g++ svm.cpp -o svm
 * $ svm -r ./helloworld.slb (-v) (-p password) -- Run program (-v: in verbose mode)
 * $ svm -d ./helloworld.slb (-p password) -- Disassembly
 * $ svm -i (-v) -- Interact Mode (-v: in verbose mode)
 * $ svm -a -s ./helloworld.txt -t ./helloworld.slb (-p password) -- Assembly input file
 *
 * @author Junru Shen
 */
#define SVM true
#define MAX_INSTRUCTION_ADDR 1000000

#if __WORDSIZE == 64
typedef long int      int_t;
#else
__extension__
typedef long long int int_t;
#endif
typedef double float_t;
typedef char char_t;

#include <iostream>
#include <fstream>
#include <stack>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <getopt.h>

// Instruction codes
enum instruct_code
  {
   // Load const and name
   LOAD_NULL,
   LOAD_INT = 100,
   LOAD_FLOAT,
   LOAD_CHAR,
   LOAD_NAME,
   BINARY_SUBSCR,
   STORE_SUBSCR,
   STORE_NAME,
   // Build array
   BUILD_ARR,
   // Operators
   BINARY_OP,
   UNARY_OP,
   // Jump
   JMP,
   JMP_TRUE,
   // Push and pop stack frame to the control stack (for function call)
   PUSH,
   RET,
   CALL,
   LOAD_GLOBAL,
   STORE_GLOBAL,
   // Halt
   HALT,
   // Debugging
   PRINTK
  };

// Basic data types
enum basic_data_types
  {
   INT = 0,
   FLOAT,
   CHAR,
   VOID,
   ARRAY
  };

// Slot
struct slot {
  basic_data_types type = VOID;
  int_t int_val;
  float_t float_val;
  char_t char_val;
  std::vector<slot>* array_val;
  basic_data_types arr_element_type = VOID;

  slot(int_t _int_val) : int_val(_int_val), type(INT) {}
  slot(bool bool_val) : int_val(bool_val ? 1 : 0), type(INT) {}
  slot(float_t _float_val) : float_val(_float_val), type(FLOAT) {}
  slot(char_t _char_val) : char_val(_char_val), type(CHAR) {}
  slot(int array_size, basic_data_types _type) {
    if (_type == ARRAY || _type == VOID) {
      // do not support nested array
      return;
    }
    type = ARRAY;
    array_val = new std::vector<slot>;
    array_val->resize(array_size);
    slot fill_slot;
    switch (_type) {
    case INT:
      fill_slot = slot((int_t) 0);
      arr_element_type = INT;
      break;
    case FLOAT:
      fill_slot = slot((float_t) 0.0);
      arr_element_type = FLOAT;
      break;
    case CHAR:
      fill_slot = slot((char) '\0');
      arr_element_type = CHAR;
      break;
    }
    for (int i = 0; i < array_size; i++) {
      (*array_val)[i] = fill_slot;
    }
  }
  slot() {}

  std::string as_string() {
    std::stringstream res;
    std::string ret;
    switch (type) {
    case INT:
      res << int_val << "(int)";
      break;
    case FLOAT:
      res << float_val << "(float)";
      break;
    case CHAR:
      res << char_val << "(char)";
      break;
    case ARRAY:
      res << "array[" << array_val->size() << "]";
      break;
    case VOID:
      res << "(null)";
      break;
    }
    res >> ret;
    return ret;
  }
};

// Instruction = Code + no more than 1 operand
struct instruct {
  instruct_code code;
  std::string operand;
  int address = -1;

  instruct(int _addr, instruct_code _code, std::string _operand) : address(_addr), code(_code), operand(_operand) {}
  instruct(int _addr, instruct_code _code) : address(_addr), code(_code) {}
};

// Stack frame
struct frame {
  std::map<std::string, slot> locals;
  int return_ip;
  std::stack<slot> local_operands;
  frame* caller;
  frame(frame* _caller) : caller(_caller) {}
};

// Convert string to system int
int _str2int(std::string s) {
  int res;
  std::stringstream ss;
  ss << s;
  ss >> res;
  return res;
}

// Convert string to int
int_t str2int(std::string s) {
  int_t res;
  std::stringstream ss;
  ss << s;
  ss >> res;
  return res;
}

// Convert string to float
float_t str2float(std::string s) {
  float_t res;
  std::stringstream ss;
  ss << s;
  ss >> res;
  return res;
}

// Convert string to char
char_t str2char(std::string s) {
  return (char_t) _str2int(s);
}

// Virtual Machine
class Machine {
private:
  std::vector<instruct> instructs; // Instructions
  int addrs[MAX_INSTRUCTION_ADDR + 1];
  std::stack<frame*> control_stack;
  std::stack<slot> global_operands;
  std::map<std::string, slot> globals;
  int ip = -1;
  bool verbose = false;

public:
  static std::map<std::string, instruct_code> string_inscode_mapping;
  static std::map<instruct_code, int> inscode_param_cnt_mapping;

  static void load_name_code_mapping() {
    string_inscode_mapping["LOAD_NULL"] = LOAD_NULL;
    string_inscode_mapping["LOAD_INT"] = LOAD_INT;
    string_inscode_mapping["LOAD_FLOAT"] = LOAD_FLOAT;
    string_inscode_mapping["LOAD_CHAR"] = LOAD_CHAR;
    string_inscode_mapping["LOAD_NAME"] = LOAD_NAME;
    string_inscode_mapping["STORE_NAME"] = STORE_NAME;
    string_inscode_mapping["JMP"] = JMP;
    string_inscode_mapping["JMP_TRUE"] = JMP_TRUE;
    string_inscode_mapping["BINARY_OP"] = BINARY_OP;
    string_inscode_mapping["UNARY_OP"] = UNARY_OP;
    string_inscode_mapping["HALT"] = HALT;
    string_inscode_mapping["RET"] = RET;
    string_inscode_mapping["PUSH"] = PUSH;
    string_inscode_mapping["CALL"] = CALL;
    string_inscode_mapping["LOAD_GLOBAL"] = LOAD_GLOBAL;
    string_inscode_mapping["STORE_GLOBAL"] = STORE_GLOBAL;
    string_inscode_mapping["BUILD_ARR"] = BUILD_ARR;
    string_inscode_mapping["BINARY_SUBSCR"] = BINARY_SUBSCR;
    string_inscode_mapping["STORE_SUBSCR"] = STORE_SUBSCR;
    string_inscode_mapping["PRINTK"] = PRINTK;
  }

  static void load_param_mapping() {
    inscode_param_cnt_mapping[LOAD_NULL] = 0;
    inscode_param_cnt_mapping[LOAD_INT] = 1;
    inscode_param_cnt_mapping[LOAD_FLOAT] = 1;
    inscode_param_cnt_mapping[LOAD_CHAR] = 1;
    inscode_param_cnt_mapping[LOAD_NAME] = 1;
    inscode_param_cnt_mapping[STORE_NAME] = 1;
    inscode_param_cnt_mapping[JMP] = 1;
    inscode_param_cnt_mapping[JMP_TRUE] = 1;
    inscode_param_cnt_mapping[BINARY_OP] = 1;
    inscode_param_cnt_mapping[UNARY_OP] = 1;
    inscode_param_cnt_mapping[HALT] = 0;
    inscode_param_cnt_mapping[RET] = 0;
    inscode_param_cnt_mapping[PUSH] = 0;
    inscode_param_cnt_mapping[CALL] = 1;
    inscode_param_cnt_mapping[LOAD_GLOBAL] = 0;
    inscode_param_cnt_mapping[STORE_GLOBAL] = 0;
    inscode_param_cnt_mapping[BUILD_ARR] = 1;
    inscode_param_cnt_mapping[BINARY_SUBSCR] = 0;
    inscode_param_cnt_mapping[STORE_SUBSCR] = 0;
    inscode_param_cnt_mapping[PRINTK] = 0;
  }

  Machine() {}

  ~Machine() {
    reset();
  }

  void enable_verbose() {
    verbose = true;
  }

  void reset() {
    std::vector<instruct>().swap(instructs);
    while (!control_stack.empty()) {
      delete control_stack.top();
      control_stack.pop();
    }
    while (!global_operands.empty()) global_operands.pop();
    globals.clear();
  }

  void add_instruct(instruct ins) {
    instructs.push_back(ins);
    addrs[ins.address] = instructs.size() - 1;
  }

  void run() {
    while (dispatch());
  }

  bool dispatch() {
    if (ip + 1 >= instructs.size()) return false;
    ip++;
    instruct ins = instructs[ip];
    std::stack<slot>* operands;
    operands = control_stack.empty() ? &global_operands : &(control_stack.top()->local_operands);

    switch (ins.code) {
    case PUSH: {
      control_stack.push(new frame(!control_stack.empty() ? control_stack.top() : NULL));
      if (verbose) {
        std::cout << "Frame is pushed into the control stack." << std::endl;
      }
      return true;
    }

    case CALL: {
      control_stack.top()->return_ip = ip + 1;
      if (verbose) {
        std::cout << "Call subroutine defined at address " << ins.operand << ", with return address " << (ip < instructs.size() - 1 ? instructs[ip + 1].address : -1) << "." << std::endl;
      }
      ip = addrs[_str2int(ins.operand)] - 1;
      return true;
    }

    case RET: {
      int to_ip = control_stack.top()->return_ip - 1;
      ip = to_ip;
      if (control_stack.top()->caller == NULL) {
        global_operands.push(operands->top());
      } else {
        control_stack.top()->caller->local_operands.push(operands->top());
      }
      if (verbose) {
        std::cout << "Frame is poped from the control stack. Return to instruct address " << (to_ip < instructs.size() - 1 ? instructs[to_ip + 1].address : -1) << " with return value " << operands->top().as_string() << "." << std::endl;
      }
      control_stack.pop();
      return true;
    }

    case LOAD_NULL: {
      operands->push(slot());
      if (verbose) {
        std::cout << "NULL value (type: void) was loaded to operand stack." << std::endl;
      }
      return true;
    }

    case LOAD_INT: {
      int_t val = str2int(ins.operand);
      operands->push(slot(val));
      if (verbose) {
        std::cout << "Int value " << val << " was loaded to operand stack." << std::endl;
      }
      return true;
    }
    case LOAD_FLOAT: {
      float_t val = str2float(ins.operand);
      operands->push(slot(val));
      if (verbose) {
        std::cout << "Float value " << val << " was loaded to operand stack." << std::endl;
      }
      return true;
    }
    case LOAD_CHAR: {
      char_t val = str2char(ins.operand);
      operands->push(slot(val));
      if (verbose) {
        std::cout << "Char value " << val << " was loaded to operand stack." << std::endl;
      }
      return true;
    }
    case LOAD_NAME: {
      bool from_globals;
      if (control_stack.empty() || !control_stack.top()->locals.count(ins.operand)) {
        operands->push(globals[ins.operand]);
        from_globals = true;
      } else {
        operands->push(control_stack.top()->locals[ins.operand]);
        from_globals = false;
      }
      if (verbose) {
        std::cout << "Loaded name " << ins.operand << " from " <<
          (from_globals ? "Globals" : "Locals")
                  << "." << std::endl;
      }
      return true;
    }
    case STORE_NAME: {
      std::map<std::string, slot>* target;
      slot val;
      bool from_globals;
      if (control_stack.empty()) {
        target = &globals;
        from_globals = true;
      } else {
        target = &(control_stack.top()->locals);
        from_globals = false;
      }
      (*target)[ins.operand] = val = operands->top();
      operands->pop();
      if (verbose) {
        std::cout << "Stored " << val.as_string() << " to name " << ins.operand << " in " <<
          (from_globals ? "Globals" : "Locals")
                  << "." << std::endl;
      }
      return true;
    }
    case JMP: {
      ip = addrs[_str2int(ins.operand)] - 1;
      if (verbose) {
        std::cout << "Jumped to instruction address " << ins.operand << "." << std::endl;
      }
      return true;
    }
    case JMP_TRUE: {
      if (operands->top().int_val) {
        ip = addrs[_str2int(ins.operand)] - 1;
        if (verbose) {
          std::cout << "The condition is true, jumped to instruction address " << ins.operand << "." << std::endl;
        }
      }
      operands->pop();
      return true;
    }
    case UNARY_OP: {
      slot operand = operands->top();
      operands->pop();

      slot res;
      if (ins.operand == "NOT") {
        if (operand.type == INT) {
          res = slot((int_t) (operand.int_val ? 0 : 1));
        }
      }
      if (ins.operand == "NEGATIVE") {
        if (operand.type == INT) {
          res = slot(-operand.int_val);
        } else if (operand.type == FLOAT) {
          res = slot(-operand.float_val);
        }
      }

      operands->push(res);
      if (verbose) {
        std::cout << "Pop " << operand.as_string() << ", calculate with unary operator " << ins.operand << ". Result " << res.as_string() << " is pushed into the stack." << std::endl;
      }
      return true;
    }
    case BINARY_OP: {
      slot right = operands->top();
      operands->pop();
      slot left = operands->top();
      operands->pop();

      slot res;
      if (ins.operand == "PLUS") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val + right.int_val);
        } else if (left.type == INT && right.type == FLOAT) {
          res = slot(left.int_val + right.float_val);
        } else if (left.type == FLOAT && right.type == INT) {
          res = slot(left.float_val + right.int_val);
        } else if (left.type == FLOAT && right.type == FLOAT) {
          res = slot(left.float_val + right.float_val);
        }
      }

      if (ins.operand == "SUB") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val - right.int_val);
        } else if (left.type == INT && right.type == FLOAT) {
          res = slot(left.int_val - right.float_val);
        } else if (left.type == FLOAT && right.type == INT) {
          res = slot(left.float_val - right.int_val);
        } else if (left.type == FLOAT && right.type == FLOAT) {
          res = slot(left.float_val - right.float_val);
        }
      }

      if (ins.operand == "PROD") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val * right.int_val);
        } else if (left.type == INT && right.type == FLOAT) {
          res = slot(left.int_val * right.float_val);
        } else if (left.type == FLOAT && right.type == INT) {
          res = slot(left.float_val * right.int_val);
        } else if (left.type == FLOAT && right.type == FLOAT) {
          res = slot(left.float_val * right.float_val);
        }
      }

      if (ins.operand == "MOD") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val % right.int_val);
        }
      }

      if (ins.operand == "DIV") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val / right.int_val);
        } else if (left.type == INT && right.type == FLOAT) {
          res = slot(left.int_val / right.float_val);
        } else if (left.type == FLOAT && right.type == INT) {
          res = slot(left.float_val / right.int_val);
        } else if (left.type == FLOAT && right.type == FLOAT) {
          res = slot(left.float_val / right.float_val);
        }
      }

      if (ins.operand == "AND") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val & right.int_val);
        }
      }

      if (ins.operand == "OR") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val | right.int_val);
        }
      }

      if (ins.operand == "SHL") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val << right.int_val);
        }
      }

      if (ins.operand == "SHR") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val >> right.int_val);
        }
      }

      if (ins.operand == "XOR") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val ^ right.int_val);
        }
      }

      if (ins.operand == "LT") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val < right.int_val);
        } else if (left.type == INT && right.type == FLOAT) {
          res = slot(left.int_val < right.float_val);
        } else if (left.type == FLOAT && right.type == INT) {
          res = slot(left.float_val < right.int_val);
        } else if (left.type == FLOAT && right.type == FLOAT) {
          res = slot(left.float_val < right.float_val);
        }
      }

      if (ins.operand == "LTE") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val <= right.int_val);
        } else if (left.type == INT && right.type == FLOAT) {
          res = slot(left.int_val <= right.float_val);
        } else if (left.type == FLOAT && right.type == INT) {
          res = slot(left.float_val <= right.int_val);
        } else if (left.type == FLOAT && right.type == FLOAT) {
          res = slot(left.float_val <= right.float_val);
        }
      }

      if (ins.operand == "GT") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val > right.int_val);
        } else if (left.type == INT && right.type == FLOAT) {
          res = slot(left.int_val > right.float_val);
        } else if (left.type == FLOAT && right.type == INT) {
          res = slot(left.float_val > right.int_val);
        } else if (left.type == FLOAT && right.type == FLOAT) {
          res = slot(left.float_val > right.float_val);
        }
      }

      if (ins.operand == "GTE") {
        if (left.type == INT && right.type == INT) {
          res = slot(left.int_val >= right.int_val);
        } else if (left.type == INT && right.type == FLOAT) {
          res = slot(left.int_val >= right.float_val);
        } else if (left.type == FLOAT && right.type == INT) {
          res = slot(left.float_val >= right.int_val);
        } else if (left.type == FLOAT && right.type == FLOAT) {
          res = slot(left.float_val >= right.float_val);
        }
      }

      operands->push(res);
      if (verbose) {
        std::cout << "Pop " << left.as_string() << " and " << right.as_string() << ", calculate with binary operator " << ins.operand << ". Result " << res.as_string() << " is pushed into the stack." << std::endl;
      }
      return true;
    }
    case HALT: {
      if (verbose) {
        std::cout << "Program received HALT signal, terminating..." << std::endl;
      }
      return false;
    }
    case PRINTK: {
      std::cout << operands->top().as_string() << std::endl;
      operands->pop();
      return true;
    }
    case STORE_GLOBAL: {
      slot val = operands->top();
      operands->pop();
      global_operands.push(val);
      if (verbose) {
        std::cout << "Pushed local value " << val.as_string() << " into global operands." << std::endl;
      }
      return true;
    }
    case LOAD_GLOBAL: {
      slot val = global_operands.top();
      global_operands.pop();
      operands->push(val);
      if (verbose) {
        std::cout << "Pushed global value " << val.as_string() << " into local operands." << std::endl;
      }
      return true;
    }
    case BUILD_ARR: {
      basic_data_types type;
      if (ins.operand == "INT") {
        type = INT;
      } else if (ins.operand == "FLOAT") {
        type = FLOAT;
      } else if (ins.operand == "CHAR") {
        type = CHAR;
      }
      int val = operands->top().int_val;
      operands->pop();
      operands->push(slot(val, type));
      if (verbose) {
        std::cout << "Built array " << ins.operand << "[" << val << "]." << std::endl;
      }
      return true;
    }
    case BINARY_SUBSCR: {
      /*
       * e.g.
       * LOAD_NAME a
       * LOAD_INT 4
       * BINARY_SUBSCR
       */
      int subscr = operands->top().int_val;
      operands->pop();
      slot target = operands->top();
      operands->pop();
      operands->push((*target.array_val)[subscr]);
      if (verbose) {
        std::cout << "Loaded element with index " << subscr << " of the array."  << std::endl;
      }
      return true;
    }
    case STORE_SUBSCR: {
      /*
       * e.g.
       * LOAD_NAME a
       * LOAD_INT 4
       * LOAD_INT 5
       * a[4] = 5;
       */
      slot val = operands->top();
      operands->pop();
      int subscr = operands->top().int_val;
      operands->pop();
      slot target = operands->top();
      operands->pop();
      switch (target.type) {
      case INT:
        (*target.array_val)[subscr].int_val = val.int_val;
        break;
      case FLOAT:
        (*target.array_val)[subscr].float_val = val.float_val;
        break;
      case CHAR:
        (*target.array_val)[subscr].char_val = val.char_val;
        break;
      }
      if (verbose) {
        std::cout << "Changed element with index " << subscr << " of the array to " << val.as_string() << "."  << std::endl;
      }
      return true;
    }
    }
    return false;
  }
};

std::map<std::string, instruct_code> Machine::string_inscode_mapping;
std::map<instruct_code, int> Machine::inscode_param_cnt_mapping;

void interpret(std::istream &is, bool verbose, bool in_interact) {
  Machine machine = Machine();
  if (verbose) {
    machine.enable_verbose();
  }
  int addr;
  while (is >> addr) {
    if (in_interact && addr == -1) {
      machine.run();
      continue;
    }
    instruct_code ins;
    if (in_interact) {
      std::string ins_str;
      is >> ins_str;
      ins = Machine::string_inscode_mapping[ins_str];
    } else {
      int ins_tmp;
      is >> ins_tmp;
      ins = instruct_code(ins_tmp);
    }
    int param_number = Machine::inscode_param_cnt_mapping[ins];
    if (param_number) {
      std::string param;
      is >> param;
      machine.add_instruct(instruct(addr, ins, param));
    } else {
      machine.add_instruct(instruct(addr, ins));
    }
  }
  if (!in_interact) machine.run();
}

void interact(bool verbose) {
  interpret(std::cin, verbose, true);
};

void assemble(std::string raw_file_path, std::string out_file_path, std::string password) {
  std::ifstream raw_file(raw_file_path, std::ios::in);
  std::ofstream out_file(out_file_path, std::ios::out | std::ios::trunc);

  std::stringstream buf;
  int addr;
  buf << "80JF34R9S ";
  while (raw_file >> addr) {
    std::string ins_str;
    raw_file >> ins_str;
    instruct_code ins = Machine::string_inscode_mapping[ins_str];
    int param_number = Machine::inscode_param_cnt_mapping[ins];
    buf << addr << " " << ins << " ";
    if (param_number) {
      std::string param;
      raw_file >> param;
      buf << param << " ";
    }
  }

  std::string s = buf.str();
  int len = password.length();
  if (len) {
    for (int i = 0; i < s.length(); i++) s[i] ^= password[i % len];
  }
  out_file << s;

  raw_file.close();
  out_file.close();
}

void run(std::string input_file_path, bool verbose, std::string password) {
  std::ifstream input_file(input_file_path, std::ios::in);
  std::string content((std::istreambuf_iterator<char>(input_file)),
                      (std::istreambuf_iterator<char>()));
  int len = password.length();
  if (len) {
    for (int i = 0; i < content.length(); i++) content[i] ^= password[i % len];
  }
  std::stringstream ss(content);
  std::string hd;
  ss >> hd;
  if (hd != "80JF34R9S") return;
  interpret(ss, verbose, false);
};

void disassemble(std::string input_file_path, std::string password) {
  std::ifstream input_file(input_file_path, std::ios::in);
  std::string content((std::istreambuf_iterator<char>(input_file)),
                      (std::istreambuf_iterator<char>()));
  std::map<instruct_code, std::string> code_name_mapping;
  for (auto x : Machine::string_inscode_mapping) {
    code_name_mapping[x.second] = x.first;
  }
  int len = password.length();
  if (len) {
    for (int i = 0; i < content.length(); i++) content[i] ^= password[i % len];
  }
  std::stringstream ss(content);
  std::string hd;
  ss >> hd;
  if (hd != "80JF34R9S") return;
  int addr;
  while (ss >> addr) {
    std::cout << addr << " ";
    int ins_tmp;
    instruct_code ins;
    ss >> ins_tmp;
    ins = instruct_code(ins_tmp);
    std::cout << code_name_mapping[ins] << " ";
    int param_number = Machine::inscode_param_cnt_mapping[ins];
    if (param_number) {
      std::string param;
      ss >> param;
      std::cout << param << " ";
    }
    std::cout << std::endl;
  }
};

int main(int argc, char* argv[])
{
  Machine::load_param_mapping();
  int opt;
  enum run_mode {
                 RUN,
                 INTERACT,
                 DISASSEMBLE,
                 ASSEMBLE
  };
  run_mode rm;
  char const *optstring = "r:d:aivs:t:p:";
  std::string input_path;
  std::string output_path;
  std::string password;
  bool verbose;
  int o;
  while ((o = getopt(argc, argv, optstring)) != -1) {
    switch (o) {
    case 'r':
      rm = RUN;
      input_path.assign(optarg);
      break;
    case 'i':
      rm = INTERACT;
      break;
    case 'd':
      rm = DISASSEMBLE;
      input_path.assign(optarg);
      break;
    case 'a':
      rm = ASSEMBLE;
      break;
    case 'v':
      verbose = true;
      break;
    case 's':
      input_path.assign(optarg);
      break;
    case 't':
      output_path.assign(optarg);
      break;
    case 'p':
      password.assign(optarg);
      break;
    }
  }
  switch (rm) {
  case RUN:
    run(input_path, verbose, password);
    break;
  case INTERACT:
    Machine::load_name_code_mapping();
    interact(verbose);
    break;
  case ASSEMBLE:
    Machine::load_name_code_mapping();
    assemble(input_path, output_path, password);
    break;
  case DISASSEMBLE:
    Machine::load_name_code_mapping();
    disassemble(input_path, password);
    break;
  }
}
