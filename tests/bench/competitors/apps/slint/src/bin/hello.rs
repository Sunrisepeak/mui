// hello: a title and a button, nothing else (the same app as omui's, Flutter's and NSH's).
slint::slint! {
    import { Button } from "std-widgets.slint";
    export component Hello inherits Window {
        title: "Hello";
        preferred-width: 800px;
        preferred-height: 600px;
        VerticalLayout {
            alignment: start;
            padding: 24px;
            spacing: 12px;
            Text { text: "Hello, world"; font-size: 24px; }
            HorizontalLayout { alignment: start; Button { text: "Click me"; } }
        }
    }
}

fn main() -> Result<(), slint::PlatformError> {
    Hello::new()?.run()
}
