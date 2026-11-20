// counter: "Count: N" and a button that adds one (the counter of tests/bench's core/* scenes).
slint::slint! {
    import { Button } from "std-widgets.slint";
    export component Counter inherits Window {
        title: "Counter";
        preferred-width: 800px;
        preferred-height: 600px;
        in-out property <int> count: 0;
        VerticalLayout {
            alignment: start;
            padding: 24px;
            spacing: 12px;
            Text { text: "Count: " + count; font-size: 24px; }
            HorizontalLayout { alignment: start; Button { text: "Increment"; clicked => { count += 1; } } }
        }
    }
}

fn main() -> Result<(), slint::PlatformError> {
    Counter::new()?.run()
}
