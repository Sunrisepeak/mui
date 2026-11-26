// The counter of the terminal comparison (F4; ../README.md): a count and two buttons; Tab moves between them, Enter
// presses, Ctrl+C ends. Ink has no mouse: the buttons are focusable boxes, the focused one reversed. Plain JavaScript
// (React.createElement), so it runs on Node as it is and bundles without a JSX step.
import React, {useState} from 'react';
import {render, Box, Text, useFocus, useInput} from 'ink';

const h = React.createElement;

function Button({label, onPress, autoFocus}) {
    const {isFocused} = useFocus({autoFocus});
    useInput((input, key) => { if (isFocused && key.return) onPress(); });
    return h(Box, {borderStyle: 'round', paddingX: 1}, h(Text, {inverse: isFocused}, label));
}

function Counter() {
    const [count, setCount] = useState(0);
    return h(Box, {flexDirection: 'column', padding: 1, gap: 1},
        h(Text, null, `Count ${count}`),
        h(Box, {gap: 1},
            h(Button, {label: '-1', autoFocus: true, onPress: () => setCount(value => Math.max(value - 1, 0))}),
            h(Button, {label: '+1', onPress: () => setCount(value => value + 1)})));
}

render(h(Counter));
