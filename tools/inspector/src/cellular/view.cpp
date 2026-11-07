module omui.inspector;
import std;
import omui;

namespace omui::inspector::cellular {
namespace {
void invoke(const std::shared_ptr<Session>& s,std::string_view op,Value args={}) {
    if (!s->runtime) return;
    const auto result=s->runtime->invoke("cellular."+std::string(op),{.args=std::move(args)});
    if (result.exit_code()!=0) {
        s->problem.set(result.ndjson());
    }
}
struct Grid {
    float cell,x,y;
};
Grid grid(Size size,Size shape) {
    const float cell=std::min(size.width/shape.width,size.height/shape.height);
    return {cell,(size.width-cell*shape.width)*.5f,(size.height-cell*shape.height)*.5f};
}
std::string brush_name(std::string_view label,bool zh) {
    if (zh) {
        if (label=="Spark") return "火种";
        if (label=="Path") return "路径";
        if (label=="Wall") return "屏障";
        if (label=="Erase") return "擦除";
    }
    return std::string(label);
}
}
void add_verbs(AppSpec& a,const std::shared_ptr<Session>& s) {
    // The verbs are the catalog's (inspector.ops.omui.toml).
    for (const auto& entry:catalog) {
        const auto op=entry.operation.name.substr(std::string_view("cellular.").size());
        a.verbs.push_back({verb_of(entry),[s,op](const Value& values){return s->perform(op,values);}});
    }
}

Element view(const std::shared_ptr<Session>& s,float width,bool zh) {
    const auto say=[zh](std::string_view en,std::string_view cn){return std::string(zh?cn:en);};
    auto canvas=painter({.id="field",.width=Dim(mlibs::layout::pct(100)),
        .label=say("Material field. Arrow keys move the cursor; Space paints the selected brush.",
                   "材料画布。方向键移动光标，空格使用选中画笔。"),.focusable=true,
        .paint=[s](PaintContext& ctx) {
            const auto shape=s->shape();const auto g=grid(ctx.size,shape);
            ctx.list.fill_rect({0,0,ctx.size.width,ctx.size.height},Color::hex(0x152133));
            s->draw_field(ctx.list,ctx.services.images(),{g.x,g.y,g.cell*shape.width,g.cell*shape.height});
            if (ctx.state.focused && ctx.state.focus_visible) {
                const float x=g.x+static_cast<float>(s->cursor_x.peek())*g.cell,y=g.y+static_cast<float>(s->cursor_y.peek())*g.cell;
                const auto white=Color::white();const float edge=std::min(2.0f,g.cell*.25f);
                ctx.list.fill_rect({x,y,g.cell,edge},white);ctx.list.fill_rect({x,y+g.cell-edge,g.cell,edge},white);
                ctx.list.fill_rect({x,y,edge,g.cell},white);ctx.list.fill_rect({x+g.cell-edge,y,edge,g.cell},white);
            }
        },
        .on_event=[s](EventContext& ctx,const InputEvent& event) {
            const auto bounds=ctx.services.bounds(ctx.node);
            const auto shape=s->shape();const auto g=grid({bounds.width,bounds.height},shape);
            bool paint=false;
            if (const auto* p=std::get_if<PointerEvent>(&event);p && p->kind==PointerKind::down && p->button==MouseButton::left) {
                const float x=(ctx.local.x-g.x)/g.cell,y=(ctx.local.y-g.y)/g.cell;
                if (x<0 || y<0 || x>=shape.width || y>=shape.height) return false;
                s->cursor_x.set(static_cast<std::int64_t>(x));s->cursor_y.set(static_cast<std::int64_t>(y));paint=true;
            }
            if (const auto* key=std::get_if<KeyEvent>(&event);key && key->kind==KeyKind::down) {
                auto x=s->cursor_x.peek(),y=s->cursor_y.peek();
                if (key->key==Key::left) --x;else if (key->key==Key::right) ++x;
                else if (key->key==Key::up) --y;else if (key->key==Key::down) ++y;
                else if (key->key==Key::space) paint=true;else return false;
                s->cursor_x.set(std::clamp<std::int64_t>(x,0,static_cast<std::int64_t>(shape.width)-1));
                s->cursor_y.set(std::clamp<std::int64_t>(y,0,static_cast<std::int64_t>(shape.height)-1));
            } else if (!paint) return false;
            if (paint) invoke(s,"paint",Value{}.set("x",s->cursor_x.peek()).set("y",s->cursor_y.peek()).set("radius",1).set("brush",s->brush.peek()));
            ctx.repaint();return true;
        }});
    canvas.prop("aspect_ratio",Source(Value(64.0/36))).prop("min_height",Source(Value(150.0))).prop("max_height",Source(Value(480.0)));
    auto controls=dynamic([s,say] {
        (void)s->changed.get();const auto state=s->snapshot();
        const bool playing=state.get("playing")->as_bool();
        const auto frame=state.get("frame")->as_int(),earliest=state.get("earliest")->as_int();
        return std::vector<Element>{row({.id="transport",.gap=8,.wrap="wrap"},{
            button({.id="play",.text=playing?say("Pause","暂停"):say("Play","播放"),.variant=ButtonVariant::primary,
                .on_activated=[s,playing]{invoke(s,"play",Value{}.set("enabled",!playing));}}),
            button({.id="step",.text=say("Step","单步"),.on_activated=[s]{invoke(s,"advance",Value{}.set("count",1));}}),
            button({.id="back",.disabled=frame<=earliest,.text=say("Back","回退"),
                .on_activated=[s,frame,earliest]{invoke(s,"seek",Value{}.set("frame",std::max(earliest,frame-1)));}}),
            button({.id="reset",.text=say("Reset","重置"),.variant=ButtonVariant::ghost,.on_activated=[s]{invoke(s,"reset");}})}),
            text({.id="time",.text=std::format("{} {}   ·   {} {}",say("Tick","模拟步"),state.get("tick")->as_int(),
                say("History","历史帧"),frame),.style="caption",.color="text_muted"})};
    });
    auto brushes=dynamic([s,zh] {
        (void)s->changed.get();const auto selected=s->brush.get();const auto state=s->snapshot();
        std::vector<Element> buttons;
        for (const auto& brush:*state.get("brushes")->list()) {
            const auto id=std::string(brush.get("id")->as_string());
            buttons.push_back(button({.id=id,.text=brush_name(brush.get("label")->as_string(),zh),
                .variant=id==selected?ButtonVariant::primary:ButtonVariant::secondary,.on_activated=[s,id]{s->brush.set(id);}}));
        }
        return std::vector<Element>{row({.id="brushes",.gap=8,.wrap="wrap"},std::move(buttons))};
    });
    auto editor=dynamic([s,say,width] {
        if (!s->details.get()) return std::vector<Element>{};
        return std::vector<Element>{column({.id="details",.width=Dim(mlibs::layout::pct(100)),.gap=12},{
            text({.id="guide",.text=say("Change the rules, palette or brushes. Apply starts a new history from the field you see.",
                "修改规则、调色板或画笔。应用后，以当前画布开始新的历史。"),.style="body",.max_width=Dim(width)}),
            code_editor({.id="config",.label=say("Rules and palette","规则与调色板"),.language=CodeEditorLanguage::json,
                .lines=10,.text=s->draft,.width=Dim(mlibs::layout::pct(100))}),
            row({.id="actions",.gap=8,.wrap="wrap"},{
                button({.id="apply",.text=say("Apply","应用"),.variant=ButtonVariant::primary,.on_activated=[s] {
                    const auto& text=s->draft.peek();
                    if (text.size()>2*1024*1024) {s->problem.set("Configuration is too large");return;}
                    const auto parsed=parse_json(text);
                    if (!parsed) {s->problem.set(parsed.error().text());return;}
                    invoke(s,"configure",Value{}.set("config",*parsed));
                }}),
                button({.id="copy",.text=say("Copy project","复制工程"),.on_activated=[s] {
                    if (s->runtime) {
                        const auto result=s->runtime->invoke("cellular.project");
                        if (result.exit_code()==0 && result.data_value()) s->runtime->set_clipboard_text(result.data_value()->json());
                    }
                }})})})};
    });
    return column({.id="cellular",.width=Dim(mlibs::layout::pct(100)),.max_width=Dim(width),.gap=16},{
        text({.id="eyebrow",.text=say("REACTION LAB","反应实验室"),.style="label",.color="accent"}),
        text({.id="title",.text=say("Make room for a spark.","给火种，留一条路。"),.style="title",.max_width=Dim(width)}),
        text({.id="intro",.text=say("Draw a path, place a barrier, then watch what changes. Every step can be revisited.",
            "画一条路径，放一道屏障，再看看变化。每一步都可以回看。"),.style="body",.max_width=Dim(width)}),
        std::move(canvas),std::move(controls),std::move(brushes),
        text({.id="cursor",.text=computed([s,say] {return std::format("{} ({}, {})",say("Brush position","画笔位置"),
            s->cursor_x.get(),s->cursor_y.get());}),.style="caption",.color="text_muted"}),
        button({.id="more",.text=say("Rules & project","规则与工程"),.variant=ButtonVariant::ghost,
            .on_activated=[s] {
                const bool open=!s->details.peek();
                if (open && s->draft.peek().empty()) {const auto config=s->perform("config");if (config)s->draft.set(config->json());}
                s->details.set(open);
            },.align_self="start"}),
        text({.id="problem",.text=s->problem,.style="caption",.max_width=Dim(width)}),std::move(editor)});
}
AppSpec app(const std::shared_ptr<Session>& s,bool zh) {
    AppSpec a;
    a.id="org.openmui.inspector.cellular";a.title=zh?"omui · 反应实验室":"omui · Reaction lab";a.size={960,800};
    a.description="Inspect and edit cellular rules; all operations and real headless pixels are available to Agents.";
    a.setup=[s](Runtime& rt){s->runtime=&rt;register_base_components();};
    a.view=[s,zh] {
        return scroll({.id="workspace",.width=Dim(mlibs::layout::pct(100)),.height=Dim(mlibs::layout::pct(100)),.padding=Edges(20)},
            {view(s,960,zh)});
    };
    add_verbs(a,s);return a;
}
}
