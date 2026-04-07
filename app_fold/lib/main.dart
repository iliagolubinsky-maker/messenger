import 'package:app_fold/ffi/RecorderService.dart';
import 'package:flutter/material.dart';
import 'dart:async';
import 'ffi/messenger_ffi.dart';
import 'ffi/messenger_controller.dart';
import 'ffi/login_screen.dart';
import 'dart:typed_data';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:path_provider/path_provider.dart';
import 'package:permission_handler/permission_handler.dart';

/// ------------------ MODELS ------------------ ///
class Message {
  final String text;
  final bool isMe;
  final bool isAudio;
  final String? audioPath;
  final Uint8List? audioBytes;
  Message(
    this.text,
    this.isMe, {
    this.isAudio = false,
    this.audioPath,
    this.audioBytes,
  });
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
void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await SoLoud.instance.init();
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
  bool _isRecording = false;
  // Assuming you have an AudioRecorder class as discussed previously
  final _audioRecorder = Recorderservice();

  StreamSubscription? _updateSubscription;

  @override
  void initState() {
    super.initState();
    _updateSubscription = messenger.updates.listen((_) {
      if (mounted) {
        setState(() {});
      }
    });
    _audioRecorder.init();
  }

  @override
  void dispose() {
    _updateSubscription?.cancel();
    _audioRecorder.dispose();
    controller.dispose();
    super.dispose();
  }

  void _send() {
    final text = controller.text.trim();
    if (text.isEmpty) return;
    messenger.sendMessage(text);
    controller.clear();
  }

  Future<void> _toggleRecording() async {
    if (!_isRecording) {
      // Start Recording
      await _audioRecorder.start();
      setState(() => _isRecording = true);
    } else {
      // Stop and Send
      final audioBytes = await _audioRecorder.stop();
      setState(() => _isRecording = false);

      if (audioBytes != null) {
        messenger.sendAudioMessage(audioBytes);
      }
    }
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
              // Inside ChatScreen's ListView.builder
              itemBuilder: (_, i) {
                final msg = messages[i];
                return Align(
                  alignment: msg.isMe
                      ? Alignment.centerRight
                      : Alignment.centerLeft,
                  child: Container(
                    margin: const EdgeInsets.symmetric(vertical: 4),
                    decoration: BoxDecoration(
                      color: msg.isMe
                          ? Theme.of(context).colorScheme.primary
                          : Colors.grey.shade300,
                      borderRadius: BorderRadius.circular(16),
                    ),
                    // Switch between Text and Audio UI
                    child: msg.isAudio
                        ? AudioMessageBubble(message: msg)
                        : Padding(
                            padding: const EdgeInsets.all(12),
                            child: Text(
                              msg.text,
                              style: TextStyle(
                                color: msg.isMe ? Colors.white : Colors.black,
                              ),
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
                // Change icon based on whether user is typing or wants to record
                IconButton(
                  icon: Icon(_isRecording ? Icons.stop : Icons.mic),
                  color: _isRecording ? Colors.red : null,
                  onPressed: _toggleRecording,
                ),
                Expanded(
                  child: TextField(
                    controller: controller,
                    enabled: !_isRecording, // Disable text while recording
                    decoration: InputDecoration(
                      hintText: _isRecording ? 'Recording...' : 'Message',
                      contentPadding: const EdgeInsets.all(12),
                    ),
                  ),
                ),
                IconButton(
                  icon: const Icon(Icons.send),
                  onPressed: _isRecording
                      ? null
                      : _send, // Disable send while recording
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class AudioMessageBubble extends StatefulWidget {
  final Message message;
  const AudioMessageBubble({super.key, required this.message});

  @override
  State<AudioMessageBubble> createState() => _AudioMessageBubbleState();
}

class _AudioMessageBubbleState extends State<AudioMessageBubble> {
  AudioSource? _source;
  SoundHandle? _handle;
  bool _isPlaying = false;
  double _progress = 0.0;
  Timer? _progressTimer;

  @override
  void dispose() {
    _progressTimer?.cancel();
    _stopAndDispose();
    super.dispose();
  }

  Future<void> _stopAndDispose() async {
    if (_handle != null) {
      // Use the specific handle to stop playback.
      await SoLoud.instance.stop(_handle!);
    }
    if (_source != null) {
      // Dispose the memory source to free up RAM.
      await SoLoud.instance.disposeSource(_source!);
    }
  }

  Future<void> _togglePlayback() async {
    if (widget.message.audioBytes == null ||
        widget.message.audioBytes!.isEmpty) {
      debugPrint("Playback Error: Audio buffer is empty.");
      return;
    }
    if (_isPlaying) {
      if (_handle != null) {
        await SoLoud.instance.stop(_handle!);
      }
      _progressTimer?.cancel();
      setState(() {
        _isPlaying = false;
        _progress = 0.0;
      });
    } else {
      if (widget.message.audioBytes == null) return;

      try {
        // 1. Load bytes into memory-backed AudioSource.
        _source = await SoLoud.instance.loadMem(
          "audio_${widget.message.hashCode}",
          widget.message.audioBytes!,
        );

        // 2. Play the source and capture the unique SoundHandle.
        _handle = await SoLoud.instance.play(_source!);
        setState(() => _isPlaying = true);

        // 3. Monitor position vs duration for the progress bar.
        _progressTimer = Timer.periodic(const Duration(milliseconds: 50), (
          timer,
        ) {
          if (!mounted || _handle == null || _source == null) return;

          final pos = SoLoud.instance.getPosition(_handle!);
          final dur = SoLoud.instance.getLength(_source!);

          if (dur.inMilliseconds > 0) {
            setState(() {
              _progress = pos.inMilliseconds / dur.inMilliseconds;
              // Auto-stop when the end is reached.
              if (_progress >= 1.0) {
                _isPlaying = false;
                _progress = 0.0;
                timer.cancel();
              }
            });
          }
        });
      } catch (e) {
        debugPrint("SoLoud Playback Error: $e");
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      width: 220,
      child: Row(
        children: [
          IconButton(
            icon: Icon(
              _isPlaying ? Icons.pause_circle : Icons.play_circle,
              color: widget.message.isMe ? Colors.white : Colors.blue,
            ),
            iconSize: 40,
            onPressed: _togglePlayback,
          ),
          Expanded(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                LinearProgressIndicator(
                  value: _progress,
                  backgroundColor: Colors.white24,
                  color: widget.message.isMe ? Colors.white : Colors.blue,
                ),
                const SizedBox(height: 4),
                Text(
                  _isPlaying ? "Playing..." : "Voice message",
                  style: TextStyle(
                    fontSize: 12,
                    color: widget.message.isMe
                        ? Colors.white70
                        : Colors.black54,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
