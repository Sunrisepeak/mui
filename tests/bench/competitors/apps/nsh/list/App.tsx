// list: the 10,000-row list of omui's tests/bench core/list-scroll in NSH — three parts a row (a title, a state, a
// button), virtual (`List`, NSH's recycled list), in the window the harness opens (omui-devtools bench apps).
import { List, mount, Text, View } from "nsh";

const orders = Array.from({ length: 10_000 }, (_, i) => ({
  id: i,
  title: `Order ${i} — ${100 + (i % 900)} shares at ${10 + (i % 400)}.${String(i % 100).padStart(2, "0")}`,
  state: i % 3 === 0 ? "filled" : "open",
}));

function App() {
  return (
    <List each={orders} estimatedItemSize={36} style={{ flex: 1, backgroundColor: "#ffffff" }}>
      {(order) => (
        <View style={{ flexDirection: "row", alignItems: "center", padding: 8, gap: 12 }}>
          <Text style={{ flex: 1, fontSize: 14 }}>{order().title}</Text>
          <Text style={{ fontSize: 12, color: "#6e7781" }}>{order().state}</Text>
          <View style={{ paddingHorizontal: 12, paddingVertical: 4, borderRadius: 6, borderWidth: 1, borderColor: "#d0d7de" }}>
            <Text style={{ fontSize: 13 }}>Open</Text>
          </View>
        </View>
      )}
    </List>
  );
}

mount(App);
