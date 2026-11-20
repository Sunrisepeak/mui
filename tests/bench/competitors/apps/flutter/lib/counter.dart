// counter: "Count: N" and a button that adds one (the counter of tests/bench's core/* scenes).
import 'package:flutter/material.dart';

void main() => runApp(const CounterApp());

class CounterApp extends StatelessWidget {
  const CounterApp({super.key});

  @override
  Widget build(BuildContext context) {
    return const MaterialApp(debugShowCheckedModeBanner: false, home: Scaffold(body: Counter()));
  }
}

class Counter extends StatefulWidget {
  const Counter({super.key});

  @override
  State<Counter> createState() => _CounterState();
}

class _CounterState extends State<Counter> {
  int count = 0;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('Count: $count', style: Theme.of(context).textTheme.headlineSmall),
          const SizedBox(height: 12),
          FilledButton(onPressed: () => setState(() => count += 1), child: const Text('Increment')),
        ],
      ),
    );
  }
}
