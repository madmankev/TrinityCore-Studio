#include "ui/toolbar.h"
namespace wowedit { void Toolbar::setActiveTool(ActiveTool tool) { if (active_ == tool) return; active_ = tool; if (changed_) changed_(tool); } }
