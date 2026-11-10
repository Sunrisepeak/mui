module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {

void register_base_components() {
    register_type(component_type<Button>());
    register_type(component_type<Toggle>());
    register_type(component_type<Checkbox>());
    register_type(component_type<Slider>());
    register_type(component_type<TextBox>());
    register_type(component_type<Progress>());
    register_type(component_type<Radio>());
    register_type(component_type<Select>());
    register_type(component_type<SelectPopup>());
    register_type(component_type<SelectOption>());
    register_type(component_type<Tabs>());
    register_type(component_type<Tab>());
    register_type(component_type<Menu>());
    register_type(component_type<MenuItem>());
    register_type(component_type<Dialog>());
    register_type(component_type<Tooltip>());
    register_type(component_type<List>());
    register_type(component_type<ListItem>());
    // The package's own (contract/): status, surfaces, navigation, number input.
    register_type(component_type<Badge>());
    register_type(component_type<Avatar>());
    register_type(component_type<Skeleton>());
    register_type(component_type<Card>());
    register_type(component_type<Accordion>());
    register_type(component_type<Toast>());
    register_type(component_type<Popover>());
    register_type(component_type<Breadcrumb>());
    register_type(component_type<Pagination>());
    register_type(component_type<NumberInput>());
    register_type(component_type<ActorLog>());
    register_type(component_type<AgentPresence>());
    register_type(component_type<Segmented>());
    register_type(component_type<Chip>());
    register_type(component_type<RangeSlider>());
    register_type(component_type<Alert>());
    register_type(component_type<Stepper>());
    register_type(component_type<DatePicker>());
    register_type(component_type<TimePicker>());
    register_type(component_type<ColorPicker>());
    register_type(component_type<SplitView>());
    register_type(component_type<Drawer>());
    register_type(component_type<TreeView>());
    register_type(component_type<DataGrid>());
    register_type(component_type<Combobox>());
    register_type(component_type<TagInput>());
    register_type(component_type<CodeEditor>());
    // W9: what is read and points on — kbd, separator, link, description list, spinner.
    register_type(component_type<Kbd>());
    register_type(component_type<Separator>());
    register_type(component_type<Link>());
    register_type(component_type<DescriptionList>());
    register_type(component_type<Spinner>());
    // W9: what a person gives — rating, otp input, form field, search box, toggle group.
    register_type(component_type<Rating>());
    register_type(component_type<OtpInput>());
    register_type(component_type<FormField>());
    register_type(component_type<SearchBox>());
    register_type(component_type<ToggleGroup>());
    // W9: finding one's way — context menu, hover card, command palette, sidebar, toolbar.
    register_type(component_type<ContextMenu>());
    register_type(component_type<HoverCard>());
    register_type(component_type<CommandPalette>());
    register_type(component_type<Sidebar>());
    register_type(component_type<Toolbar>());
    // W9: what is shown to be looked at — chart, carousel.
    register_type(component_type<Chart>());
    register_type(component_type<Carousel>());
    register_type(component_type<WebView>());
    register_type(component_type<FileDrop>());
}

}  // namespace omui
