// The catalogue's stable addresses, names and purposes, shared by routes and views.
export module omui.site:catalog;
import std;
export namespace omui::site {
struct ComponentInfo { std::string_view id, en, zh, group, purpose_en, purpose_zh; bool featured = false; };
struct ComponentGroup { std::string_view id, en, zh; };
inline constexpr std::array<ComponentGroup, 6> component_groups{{
    {"input", "Input & choice", "输入与选择"}, {"action", "Actions & feedback", "动作与反馈"},
    {"navigation", "Navigation", "导航"}, {"layout", "Layout & containers", "布局与容器"},
    {"data", "Data display", "数据展示"}, {"editing", "Editing & content", "编辑与内容"}
}};
inline constexpr std::array<ComponentInfo, 36> component_catalog{{
    {"button", "Button", "按钮", "action", "Give an action a clear place to begin.", "给操作一个清晰的起点。", true},
    {"toggle", "Toggle", "开关", "input", "Turn a setting on, or leave it off.", "切换一项设置的开与关。"},
    {"checkbox", "Checkbox", "复选框", "input", "Choose independently from a set.", "从一组事项中独立选择。"},
    {"radio", "Radio", "单选", "input", "Make one choice among alternatives.", "在一组候选项中选择一个。"},
    {"slider", "Slider", "滑块", "input", "Find a value by moving through a range.", "在连续范围中调整数值。", true},
    {"number_input", "Number input", "数字输入", "input", "Enter a precise quantity with units.", "输入带有单位的精确数量。"},
    {"text_box", "Text box", "文本框", "input", "A place for a name, a thought, an answer.", "输入一个名字、一段想法或一个答案。", true},
    {"select", "Select", "选择", "input", "Pick from a short, known set of values.", "从已知的有限选项中选择。"},
    {"tabs", "Tabs", "标签页", "navigation", "Move between views of the same subject.", "切换同一主题的不同视图。", true},
    {"breadcrumb", "Breadcrumb", "面包屑", "navigation", "Keep the route back in sight.", "让返回上层的路径始终清晰。"},
    {"pagination", "Pagination", "分页", "navigation", "Travel through a larger collection.", "逐页浏览较大的集合。"},
    {"progress", "Progress", "进度", "action", "Show how much of the work is done.", "显示已经完成了多少工作。"},
    {"badge", "Badge", "徽标", "data", "A small count, state or signal.", "用很小的空间传达数量与状态。"},
    {"avatar", "Avatar", "头像", "data", "Recognise a person and their presence.", "识别一个人及其在线状态。"},
    {"card", "Card", "卡片", "layout", "Bring related content into one object.", "将相关内容组织成一个对象。"},
    {"accordion", "Accordion", "折叠面板", "layout", "Reveal detail when it becomes useful.", "在需要时展开细节。"},
    {"skeleton", "Skeleton", "骨架", "action", "Keep a place for content as it arrives.", "为即将到来的内容保留位置。"},
    {"tooltip", "Tooltip", "提示", "action", "Offer a little context at the point of use.", "在操作位置补充简短说明。"},
    {"popover", "Popover", "弹出层", "layout", "Keep a small task close to its trigger.", "让一项小任务紧邻触发它的对象。"},
    {"toast", "Toast", "消息提示", "action", "Acknowledge an outcome without interrupting.", "在不打断操作的情况下反馈结果。"},
    {"dialog", "Dialog", "对话框", "layout", "Give a decision its own focused space.", "为需要专注的决定提供空间。", true},
    {"segmented", "Segmented control", "分段控件", "input", "Compare a few choices side by side.", "并列呈现少量可切换选项。"},
    {"chip", "Chip", "标签", "input", "Name, select or remove a small item.", "命名、选择或移除一个小项。"},
    {"range_slider", "Range slider", "范围滑块", "input", "Set the lower and upper bounds together.", "同时调整一个范围的上下限。"},
    {"alert", "Alert", "提醒", "action", "Explain what changed and what to do next.", "解释发生了什么，以及接下来可以做什么。"},
    {"stepper", "Stepper", "步骤条", "navigation", "Make a sequence and its current step clear.", "呈现步骤顺序与当前进度。"},
    {"date_picker", "Date picker", "日期选择", "input", "Choose a day for the next plan.", "为下一项计划选择日期。"},
    {"time_picker", "Time picker", "时间选择", "input", "Choose when something begins.", "选择一项安排开始的时间。"},
    {"color_picker", "Colour picker", "颜色选择", "input", "Find an ink, then refine its value.", "选择颜色，再精确调整色值。"},
    {"split_view", "Split view", "分栏", "layout", "Give neighbouring views room to work.", "为相邻视图分配可调整的空间。"},
    {"drawer", "Drawer", "抽屉", "layout", "Bring a supporting task in from the edge.", "从侧边展开一项辅助任务。"},
    {"tree_view", "Tree view", "树", "data", "Navigate the structure inside a collection.", "浏览集合内部的层级结构。"},
    {"data_grid", "Data grid", "数据表格", "data", "Compare records, sort and choose rows.", "对照记录、排序并选择行。", true},
    {"combobox", "Combobox", "组合框", "input", "Search a choice, or enter your own.", "搜索一个选项，或输入自己的内容。", true},
    {"tag_input", "Tag input", "标签输入", "editing", "Build a set of topics one item at a time.", "逐项整理一组主题。"},
    {"code_editor", "Code editor", "代码编辑器", "editing", "Edit a small piece of a larger idea.", "编辑一段可以继续发展的代码。", true}
}};
[[nodiscard]] inline const ComponentInfo* find_component(std::string_view id) {
    for (const auto& c : component_catalog) if (c.id == id) return &c;
    return nullptr;
}
[[nodiscard]] inline std::string component_slug(std::string_view id) {
    std::string slug(id); std::ranges::replace(slug, '_', '-'); return slug;
}
[[nodiscard]] inline const ComponentInfo* component_at(std::string_view slug) {
    for (const auto& c : component_catalog) if (c.id == slug || component_slug(c.id) == slug) return &c;
    return nullptr;
}
}
