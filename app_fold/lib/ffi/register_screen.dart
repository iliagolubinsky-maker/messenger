import 'package:flutter/material.dart';
import 'messenger_controller.dart';
import '../main.dart';

class RegisterScreen extends StatefulWidget {
  const RegisterScreen({super.key});

  @override
  State<RegisterScreen> createState() => _RegisterScreenState();
}

class _RegisterScreenState extends State<RegisterScreen> {
  final _usernameController = TextEditingController();
  final _passwordController = TextEditingController();

  bool _loading = false;
  bool _failed = false;

  late final MessengerController _messenger;

  @override
  void initState() {
    super.initState();
    _messenger = MessengerController();
  }

  Future<void> _attemptRegister() async {
    final username = _usernameController.text.trim();
    final password = _passwordController.text;

    if (username.isEmpty || password.isEmpty) return;

    setState(() {
      _loading = true;
      _failed = false;
    });

    try {
      _messenger.init(username);
      _messenger.register(username, password);

      await Future.delayed(const Duration(milliseconds: 200));

      final success = _messenger.getLogStatus();

      if (success) {
        if (!mounted) return;
        Navigator.pushReplacement(
          context,
          MaterialPageRoute(builder: (_) => ChatListScreen()),
        );
      } else {
        setState(() => _failed = true);
      }
    } catch (_) {
      setState(() => _failed = true);
    } finally {
      setState(() => _loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Register')),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            TextField(
              controller: _usernameController,
              decoration: const InputDecoration(
                labelText: 'Username',
                border: OutlineInputBorder(),
              ),
            ),

            const SizedBox(height: 12),

            TextField(
              controller: _passwordController,
              obscureText: true,
              decoration: const InputDecoration(
                labelText: 'Password',
                border: OutlineInputBorder(),
              ),
            ),

            const SizedBox(height: 16),

            if (_loading)
              const CircularProgressIndicator()
            else
              ElevatedButton(
                onPressed: _attemptRegister,
                child: const Text('Create account'),
              ),

            if (_failed) ...[
              const SizedBox(height: 12),
              const Text(
                'Registration failed',
                style: TextStyle(color: Colors.red),
              ),
            ],
          ],
        ),
      ),
    );
  }
}
