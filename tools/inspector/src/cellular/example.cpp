module openmui.inspector;
import std;
import openmui;

namespace omui::inspector::cellular {
Value example_project() {
    // An editable recipe. The engine knows only channels, tests and writes.
    const auto parsed=parse_json(R"({
      "version":1,"name":"A path for a spark","size":[64,36],
      "channels":["material","phase","age"],"rate":12,"tick":0,
      "boundary":{"edge":"constant","value":0},"cells":[],
      "rules":[
        {"when":[{"channel":1,"value":[1,1]},{"channel":2,"value":[4,4294967295]}],"write":[{"channel":1,"op":"set","value":2}]},
        {"when":[{"channel":1,"value":[1,1]}],"write":[{"channel":2,"op":"add","value":1}]},
        {"when":[{"channel":0,"value":[1,1]},{"channel":1,"value":[0,0]},
          {"channel":1,"value":[1,1],"offsets":[[-1,0],[1,0],[0,-1],[0,1]],"count":[1,4]}],
          "write":[{"channel":1,"op":"set","value":1},{"channel":2,"op":"set","value":0}]}
      ],
      "palette":[
        {"when":[{"channel":1,"value":[1,1]}],"color":"#ffbf69"},
        {"when":[{"channel":1,"value":[2,2]}],"color":"#48566b"},
        {"when":[{"channel":0,"value":[1,1]}],"color":"#63bfa9"},
        {"when":[{"channel":0,"value":[2,2]}],"color":"#b3c2d3"},
        {"when":[],"color":"#152133"}
      ],
      "brushes":[
        {"id":"spark","label":"Spark","color":"#ffbf69","values":[1,1,0]},
        {"id":"path","label":"Path","color":"#63bfa9","values":[1,0,0]},
        {"id":"wall","label":"Wall","color":"#b3c2d3","values":[2,0,0]},
        {"id":"erase","label":"Erase","color":"#152133","values":[0,0,0]}
      ]
    })");
    Value project=*parsed;constexpr int width=64,height=36,area=width*height;
    std::vector<int> cells(area*3);
    for (int y=5;y<31;++y) for (int x=5;x<59;++x) cells[y*width+x]=1;
    for (int y=5;y<26;++y) cells[y*width+30]=2;
    for (int x=30;x<54;++x) cells[16*width+x]=2;
    cells[area+18*width+8]=1;
    Value::List values;values.reserve(cells.size());for (int cell:cells) values.emplace_back(cell);
    return project.set("cells",Value(std::move(values)));
}
}
