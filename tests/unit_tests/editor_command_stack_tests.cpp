#include "editors/common/CommandStack.h"

#include <iostream>
#include <stdexcept>

namespace
{
void Expect(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main()
{
    try
    {
        we::CommandStack history;
        int value = 0;

        // CommandStack records operations after callers apply them, matching the
        // World Editor's DB/ADT placement workflow.
        history.BeginMacro("Grid placement");
        value += 2;
        history.Push(we::MakeCommand([&] { value -= 2; }, [&] { value += 2; }, "Place A"));
        value *= 3;
        history.Push(we::MakeCommand([&] { value /= 3; }, [&] { value *= 3; }, "Place B"));
        history.EndMacro();

        Expect(history.UndoDepth() == 1, "grid commands should be one undo entry");
        Expect(value == 6, "commands remain applied while recording a macro");
        history.Undo();
        Expect(value == 0, "compound undo must reverse child order");
        history.Redo();
        Expect(value == 6, "compound redo must preserve child order");

        history.BeginMacro("Outer");
        value += 1;
        history.Push(we::MakeCommand([&] { value -= 1; }, [&] { value += 1; }, "Outer A"));
        history.BeginMacro("Inner");
        value += 4;
        history.Push(we::MakeCommand([&] { value -= 4; }, [&] { value += 4; }, "Inner A"));
        history.EndMacro();
        history.EndMacro();
        Expect(history.UndoDepth() == 2 && value == 11, "nested macros should collapse into outer command");
        history.Undo();
        Expect(value == 6, "nested macro undo should restore previous state");

        std::cout << "Editor CommandStack macro tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Editor CommandStack test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
