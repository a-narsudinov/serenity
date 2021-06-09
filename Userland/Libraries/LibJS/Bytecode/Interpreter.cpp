/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <LibJS/Bytecode/BasicBlock.h>
#include <LibJS/Bytecode/Instruction.h>
#include <LibJS/Bytecode/Interpreter.h>
#include <LibJS/Runtime/GlobalObject.h>

namespace JS::Bytecode {

static Interpreter* s_current;

Interpreter* Interpreter::current()
{
    return s_current;
}

Interpreter::Interpreter(GlobalObject& global_object)
    : m_vm(global_object.vm())
    , m_global_object(global_object)
{
    VERIFY(!s_current);
    s_current = this;
}

Interpreter::~Interpreter()
{
    VERIFY(s_current == this);
    s_current = nullptr;
}

Value Interpreter::run(Executable const& executable)
{
    dbgln_if(JS_BYTECODE_DEBUG, "Bytecode::Interpreter will run unit {:p}", &executable);

    CallFrame global_call_frame;
    if (vm().call_stack().is_empty()) {
        global_call_frame.this_value = &global_object();
        static FlyString global_execution_context_name = "(*BC* global execution context)";
        global_call_frame.function_name = global_execution_context_name;
        global_call_frame.scope = &global_object();
        VERIFY(!vm().exception());
        // FIXME: How do we know if we're in strict mode? Maybe the Bytecode::Block should know this?
        // global_call_frame.is_strict_mode = ???;
        vm().push_call_frame(global_call_frame, global_object());
        VERIFY(!vm().exception());
    }

    auto block = &executable.basic_blocks.first();
    m_register_windows.append(make<RegisterWindow>());
    registers().resize(executable.number_of_registers);

    for (;;) {
        Bytecode::InstructionStreamIterator pc(block->instruction_stream());
        bool will_jump = false;
        bool will_return = false;
        while (!pc.at_end()) {
            auto& instruction = *pc;
            instruction.execute(*this);
            if (m_pending_jump.has_value()) {
                block = m_pending_jump.release_value();
                will_jump = true;
                break;
            }
            if (!m_return_value.is_empty()) {
                will_return = true;
                break;
            }
            ++pc;
        }

        if (will_return)
            break;

        if (pc.at_end() && !will_jump)
            break;
    }

    dbgln_if(JS_BYTECODE_DEBUG, "Bytecode::Interpreter did run unit {:p}", &executable);

    if constexpr (JS_BYTECODE_DEBUG) {
        for (size_t i = 0; i < registers().size(); ++i) {
            String value_string;
            if (registers()[i].is_empty())
                value_string = "(empty)";
            else
                value_string = registers()[i].to_string_without_side_effects();
            dbgln("[{:3}] {}", i, value_string);
        }
    }

    m_register_windows.take_last();

    auto return_value = m_return_value.value_or(js_undefined());
    m_return_value = {};

    // NOTE: The return value from a called function is put into $0 in the caller context.
    if (!m_register_windows.is_empty())
        m_register_windows.last()[0] = return_value;

    if (vm().call_stack().size() == 1)
        vm().pop_call_frame();

    return return_value;
}

}
