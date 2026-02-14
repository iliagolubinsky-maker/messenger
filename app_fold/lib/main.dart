import 'package:flutter/material.dart';
import 'dart:async';
import 'ffi/messenger_ffi.dart';
import 'ffi/messenger_controller.dart';
import 'ffi/login_screen.dart';

/// ------------------ MODELS ------------------ ///
class Message {
  final String text;
  final bool isMe;
  Message(this.text, this.isMe);
}

class Chat {
  final String name;
  String lastMessage = '';
  String time = '';
  final List<Message> messages = [];
  Chat(this.name);
}

/// ------------------ CONTROLLER ------------------ ///

/// ------------------ MAIN ------------------ ///
void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const MessengerApp());
}

class MessengerApp extends StatelessWidget {
  const MessengerApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'Messenger',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.blueAccent),
        useMaterial3: true,
      ),
      // Start with the login screen
      home: const LoginScreen(),
    );
  }
}

/// ------------------ CHAT LIST SCREEN ------------------ ///
class ChatListScreen extends StatefulWidget {
  @override
  State<ChatListScreen> createState() => _ChatListScreenState();
}

class _ChatListScreenState extends State<ChatListScreen> {
  final messenger = MessengerController();

  @override
  void initState() {
    super.initState();
    messenger.updates.listen((_) => setState(() {}));
  }

  void _showAddChatDialog() {
    final controller = TextEditingController();
    showDialog(
      context: context,
      builder: (_) => AlertDialog(
        title: const Text('New chat'),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: const InputDecoration(
            hintText: 'Username',
            border: OutlineInputBorder(),
          ),
          onSubmitted: (_) => _startChat(controller.text),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Cancel'),
          ),
          ElevatedButton(
            onPressed: () => _startChat(controller.text),
            child: const Text('Start'),
          ),
        ],
      ),
    );
  }

  void _startChat(String username) {
    final name = username.trim();
    if (name.isEmpty) return;

    messenger.addChat(name);
    Navigator.pop(context);

    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => ChatScreen(chat: messenger.chats[name]!),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final chats = messenger.chats.values.toList();
    return Scaffold(
      appBar: AppBar(title: const Text('Chats')),
      body: chats.isEmpty
          ? const Center(child: Text('No chats yet'))
          : ListView.builder(
              itemCount: chats.length,
              itemBuilder: (_, i) {
                final chat = chats[i];
                return ListTile(
                  leading: CircleAvatar(child: Text(chat.name[0])),
                  title: Text(chat.name),
                  subtitle: Text(chat.lastMessage),
                  trailing: Text(chat.time),
                  onTap: () {
                    messenger.changeRecipient(chat.name);
                    Navigator.push(
                      context,
                      MaterialPageRoute(builder: (_) => ChatScreen(chat: chat)),
                    );
                  },
                );
              },
            ),
      floatingActionButton: FloatingActionButton(
        onPressed: _showAddChatDialog,
        child: const Icon(Icons.add),
      ),
    );
  }
}

/// ------------------ CHAT SCREEN ------------------ ///
class ChatScreen extends StatefulWidget {
  final Chat chat;
  ChatScreen({required this.chat});

  @override
  State<ChatScreen> createState() => _ChatScreenState();
}

class _ChatScreenState extends State<ChatScreen> {
  final controller = TextEditingController();
  final messenger = MessengerController();

  @override
  void initState() {
    super.initState();
    messenger.updates.listen((_) => setState(() {}));
  }

  void _send() {
    final text = controller.text.trim();
    if (text.isEmpty) return;
    messenger.sendMessage(text);
    controller.clear();
  }

  @override
  Widget build(BuildContext context) {
    final messages = widget.chat.messages;
    return Scaffold(
      appBar: AppBar(title: Text(widget.chat.name)),
      body: Column(
        children: [
          Expanded(
            child: ListView.builder(
              padding: const EdgeInsets.all(12),
              itemCount: messages.length,
              itemBuilder: (_, i) {
                final msg = messages[i];
                return Align(
                  alignment: msg.isMe
                      ? Alignment.centerRight
                      : Alignment.centerLeft,
                  child: Container(
                    margin: const EdgeInsets.symmetric(vertical: 4),
                    padding: const EdgeInsets.all(12),
                    decoration: BoxDecoration(
                      color: msg.isMe
                          ? Theme.of(context).colorScheme.primary
                          : Colors.grey.shade300,
                      borderRadius: BorderRadius.circular(16),
                    ),
                    child: Text(
                      msg.text,
                      style: TextStyle(
                        color: msg.isMe ? Colors.white : Colors.black,
                      ),
                    ),
                  ),
                );
              },
            ),
          ),
          SafeArea(
            child: Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: controller,
                    decoration: const InputDecoration(
                      hintText: 'Message',
                      contentPadding: EdgeInsets.all(12),
                    ),
                  ),
                ),
                IconButton(icon: const Icon(Icons.send), onPressed: _send),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
