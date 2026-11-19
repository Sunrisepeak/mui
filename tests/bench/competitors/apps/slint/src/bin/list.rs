// list: the 10,000 rows of omui's tests/bench core/list-scroll — a title, a state and a button a row, in Slint's
// ListView (which makes only the rows in view), in the window the harness opens.
slint::slint! {
    import { Button, ListView } from "std-widgets.slint";
    export struct Order { title: string, state: string }
    export component Orders inherits Window {
        title: "Orders";
        preferred-width: 1896px;
        preferred-height: 1030px;
        in property <[Order]> orders;
        ListView {
            for order in orders: HorizontalLayout {
                padding: 8px;
                spacing: 12px;
                Text { text: order.title; vertical-alignment: center; horizontal-stretch: 1; }
                Text { text: order.state; font-size: 12px; vertical-alignment: center; }
                Button { text: "Open"; }
            }
        }
    }
}

fn main() -> Result<(), slint::PlatformError> {
    let window = Orders::new()?;
    let orders: Vec<Order> = (0..10_000)
        .map(|i: u32| Order {
            title: format!("Order {} — {} shares at {}.{:02}", i, 100 + i % 900, 10 + i % 400, i % 100).into(),
            state: if i % 3 == 0 { "filled" } else { "open" }.into(),
        })
        .collect();
    window.set_orders(std::rc::Rc::new(slint::VecModel::from(orders)).into());
    window.run()
}
