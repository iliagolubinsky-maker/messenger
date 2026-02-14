import 'package:flutter/material.dart';
import 'messenger_controller.dart';
import 'register_screen.dart';
import '../main.dart';

class LoginScreen extends StatefulWidget {
  const LoginScreen({super.key});

  @override
  State<LoginScreen> createState() => _LoginScreenState();
}

class _LoginScreenState extends State<LoginScreen> {
  final _usernameController = TextEditingController();
  final _passwordController = TextEditingController();

  bool _loginFailed = false;
  bool _loading = false;

  late final MessengerController _messenger;

  @override
  void initState() {
    super.initState();
    _messenger = MessengerController();
  }

  Future<void> _attemptLogin() async {
    final username = _usernameController.text.trim();
    final password = _passwordController.text;

    if (username.isEmpty || password.isEmpty) return;

    setState(() {
      _loading = true;
      _loginFailed = false;
    });

    try {
      _messenger.init(username);
      _messenger.login(username, password);

      // give server time to reply
      await Future.delayed(const Duration(milliseconds: 200));

      final success = _messenger.getLogStatus();

      if (success) {
        if (!mounted) return;
        Navigator.pushReplacement(
          context,
          MaterialPageRoute(builder: (_) => ChatListScreen()),
        );
      } else {
        setState(() => _loginFailed = true);
        _messenger.dispose();
      }
    } catch (_) {
      setState(() => _loginFailed = true);
    } finally {
      setState(() => _loading = false);
    }
  }

  void _tryAnotherUser() {
    _usernameController.clear();
    _passwordController.clear();
    setState(() => _loginFailed = false);
  }

  void _goToRegister() {
    _usernameController.clear();
    _passwordController.clear();

    Navigator.push(
      context,
      MaterialPageRoute(builder: (_) => const RegisterScreen()),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Text(
                'Messenger Login',
                style: Theme.of(context).textTheme.headlineSmall,
              ),
              const SizedBox(height: 24),

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
                  onPressed: _attemptLogin,
                  child: const Text('Login'),
                ),

              if (_loginFailed) ...[
                const SizedBox(height: 16),
                const Text('Login failed', style: TextStyle(color: Colors.red)),
                const SizedBox(height: 12),
                Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    OutlinedButton(
                      onPressed: _tryAnotherUser,
                      child: const Text('Try another username'),
                    ),
                    const SizedBox(width: 12),
                    ElevatedButton(
                      onPressed: _goToRegister,
                      child: const Text('Register'),
                    ),
                  ],
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}
