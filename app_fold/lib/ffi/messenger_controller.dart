import 'package:flutter/material.dart';
import 'RecorderService.dart';
import 'dart:async';
import 'messenger_ffi.dart';
import '../main.dart';
import 'dart:typed_data';

class MessengerController {
  static final MessengerController _instance = MessengerController._internal();
  factory MessengerController() => _instance;
  MessengerController._internal();

  late MessengerFFI _ffiClient;
  String? _myUsername;
  final Map<String, Chat> chats = {};
  final StreamController<void> _updateController = StreamController.broadcast();
  Stream<void> get updates => _updateController.stream;

  String? _currentRecipient;
  bool _initialized = false;

  void init(String from) {
    _myUsername = from;
    _ffiClient = MessengerFFI(from);
    _ffiClient.start();

    _ffiClient.onMessage =
        (
          String from,
          String to,
          String text,
          bool isAudio,
          Uint8List audioBytes,
        ) {
          final String peer = (from == _myUsername) ? to : from;
          _ensureChat(peer);

          final chat = chats[peer]!;

          chat.messages.add(
            Message(
              text,
              from == _myUsername,
              isAudio: isAudio,
              audioBytes: isAudio ? audioBytes : null,
            ),
          );

          chat.lastMessage = isAudio ? "🎤 Voice message" : text;
          _updateController.add(null);
        };
  }

  void register(String username, String password) {
    _ffiClient.register(username, password);
  }

  void addChat(String username) {
    _ensureChat(username);
    changeRecipient(username);
    _updateController.add(null);
  }

  void changeRecipient(String to) {
    _currentRecipient = to;
    _ffiClient.change(to);
  }

  void stop() {
    _ffiClient.stop();
  }

  void sendMessage(String text) {
    if (_currentRecipient == null) return;
    final chat = chats[_currentRecipient]!;
    chat.messages.add(Message(text, true));

    chat.lastMessage = text;
    chat.time = _now();
    _ffiClient.send(text);
    _updateController.add(null);
  }

  void _ensureChat(String name) {
    chats.putIfAbsent(name, () => Chat(name));
  }

  String _now() {
    final t = TimeOfDay.now();
    return '${t.hour}:${t.minute.toString().padLeft(2, '0')}';
  }

  void dispose() {
    _ffiClient.stop();
    _ffiClient.dispose();
  }

  void login(String username, String password) {
    _ffiClient.login(username, password);
  }

  bool getLogStatus() {
    return _ffiClient.getStatus();
  }

  void reset() {
    _initialized = false;
    _currentRecipient = null;
    chats.clear();
  }

  void sendAudioMessage(Uint8List audioBytes) {
    if (_currentRecipient == null) return;

    final chat = chats[_currentRecipient];

    if (chat != null) {
      chat.messages.add(
        Message("", true, isAudio: true, audioBytes: audioBytes),
      );
      chat.lastMessage = "🎤 Voice message";
      chat.time = _now();
      _ffiClient.sendBinary(audioBytes);
      _updateController.add(null);
    }
  }
}
