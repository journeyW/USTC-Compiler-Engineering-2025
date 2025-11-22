#include "DeadCode.hpp"
#include "BasicBlock.hpp"
#include "Instruction.hpp"
#include "logging.hpp"
#include <memory>
#include <vector>
#include <unordered_set>
#include <stdexcept>


// 处理流程：两趟处理，mark 标记有用变量，sweep 删除无用指令
void DeadCode::run() {
    bool changed{};
    func_info->run();
    do {
        changed = false;
        for (auto &F : m_->get_functions()) {
            auto func = &F;
            changed |= clear_basic_blocks(func);
            marked.clear();
            mark(func);
            changed |= sweep(func);
        }

        sweep_globally();
    } while (changed);
    LOG_INFO << "dead code pass erased " << ins_count << " instructions";
}

bool DeadCode::clear_basic_blocks(Function *func) {
    bool changed = 0;
    std::vector<BasicBlock *> to_erase;
    for (auto &bb1 : func->get_basic_blocks()) {
        auto bb = &bb1;
        if(bb->get_pre_basic_blocks().empty() && bb != func->get_entry_block()) {
            to_erase.push_back(bb);
            changed = true;
        }
    }
    for (auto &bb : to_erase) {
        for (auto succ_bb : bb->get_succ_basic_blocks()) {
            succ_bb->remove_pre_basic_block(bb);
        }
        bb->erase_from_parent();
        delete bb;
    }
    return changed;
}

void DeadCode::mark(Function *func) {
    for (BasicBlock &bb : func->get_basic_blocks()) {
        for (Instruction &instr : bb.get_instructions()) {

            Instruction *ins = &instr;
            if (!is_critical(ins))
                continue;     // 反转条件提前 continue

            mark(ins);        // 执行关键指令标记
        }
    }
}


void DeadCode::mark(Instruction *ins) {
    // 已经标记过则直接退出
    if (marked.count(ins))
        return;

    // 标记当前指令
    marked[ins] = true;

    // 遍历操作数
    for (Value *op : ins->get_operands()) {
        if (!op)
            continue;

        Instruction *producer = dynamic_cast<Instruction *>(op);
        if (!producer)
            continue;

        mark(producer);
    }
}

bool DeadCode::sweep(Function *func) {
    // 删除无用指令，返回是否有修改
    bool changed = false;

    // 遍历函数中的每个基本块
    for (auto &bb : func->get_basic_blocks()) {
        std::vector<Instruction *> to_delete;

        // 遍历基本块中的指令，收集需要删除的指令
        for (Instruction &instr : bb.get_instructions()) {
            Instruction *ins = &instr;
            // 如果指令没有被标记为 true，则加入删除列表
            if (!marked.count(ins) || !marked[ins]) {
                to_delete.push_back(ins);
            }
        }

        // 删除收集到的指令
        for (Instruction *ins : to_delete) {
            changed = true;

            // 先处理指令的使用者，删除操作数引用
            for (auto &use : ins->get_use_list()) {
                if (Instruction *user_ins = dynamic_cast<Instruction *>(use.val_)) {
                    user_ins->remove_operand(use.arg_no_);
                }
            }

            // 删除指令自身的所有操作数
            ins->remove_all_operands();

            // 从基本块中移除指令并释放内存
            bb.remove_instr(ins);
            ins_count++;
            delete ins;
        }
    }

    return changed;
}


bool DeadCode::is_critical(Instruction *ins) {
    // 判断指令是否为关键指令（不可删除）

    // 1. 如果指令有使用者，则一定是关键指令
    if (!ins->get_use_list().empty()) {
        return true;
    }

    // 2. 如果是函数调用，且函数非纯函数，则为关键指令
    if (ins->is_call()) {
        auto call_inst = static_cast<CallInst *>(ins);
        auto callee = call_inst->func_;
        bool is_pure = false;

        try {
            is_pure = func_info->is_pure_function(callee);
        } catch (const std::out_of_range &) {
            // 如果找不到函数信息，默认认为非纯函数
            is_pure = false;
        }

        // 非纯函数调用是关键指令，纯函数调用可删除
        return !is_pure;
    }

    // 3. 分支、返回和存储指令都是关键指令
    if (ins->is_br() || ins->is_ret() || ins->is_store()) {
        return true;
    }

    // 其他指令可视为非关键指令（可能被删除）
    return false;
}


void DeadCode::sweep_globally() {
    std::vector<Function *> unused_funcs;
    std::vector<GlobalVariable *> unused_globals;
    for (auto &f_r : m_->get_functions()) {
        if (f_r.get_use_list().size() == 0 and f_r.get_name() != "main")
            unused_funcs.push_back(&f_r);
    }
    for (auto &glob_var_r : m_->get_global_variable()) {
        if (glob_var_r.get_use_list().size() == 0)
            unused_globals.push_back(&glob_var_r);
    }
    // changed |= unused_funcs.size() or unused_globals.size();
    for (auto func : unused_funcs)
        m_->get_functions().erase(func);
    for (auto glob : unused_globals)
        m_->get_global_variable().erase(glob);
}
