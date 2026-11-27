// list: the 10,000 rows of omui's tests/bench core/list-scroll — a title, a state and a button a row, built only
// where they show (ListView.builder), in the window the harness opens.
import 'package:flutter/material.dart';

void main() => runApp(const ListApp());

class ListApp extends StatelessWidget {
  const ListApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      home: Scaffold(
        body: ListView.builder(
          itemCount: 10000,
          itemBuilder: (context, i) => Padding(
            padding: const EdgeInsets.all(8),
            child: Row(
              children: [
                Expanded(child: Text('Order $i — ${100 + i % 900} shares at ${10 + i % 400}.${(i % 100).toString().padLeft(2, '0')}')),
                const SizedBox(width: 12),
                Text(i % 3 == 0 ? 'filled' : 'open', style: Theme.of(context).textTheme.bodySmall),
                const SizedBox(width: 12),
                OutlinedButton(onPressed: () {}, child: const Text('Open')),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
