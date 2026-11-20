// hello: a title and a button, nothing else (the same app as omui's, Slint's and NSH's).
import 'package:flutter/material.dart';

void main() => runApp(const HelloApp());

class HelloApp extends StatelessWidget {
  const HelloApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      home: Scaffold(
        body: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('Hello, world', style: Theme.of(context).textTheme.headlineSmall),
              const SizedBox(height: 12),
              FilledButton(onPressed: () {}, child: const Text('Click me')),
            ],
          ),
        ),
      ),
    );
  }
}
