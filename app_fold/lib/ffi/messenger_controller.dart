import 'package:flutter/material.dart';
import 'dart:async';
import 'messenger_ffi.dart';
import '../main.dart';

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
    // if (_initialized) return;
    _myUsername = from;
    _ffiClient = MessengerFFI(from);
    _ffiClient.start();

    _ffiClient.onMessage = (String fromUser, String toUser, String text) {
      final String peer = (fromUser == _myUsername) ? toUser : fromUser;
      _ensureChat(peer);
      final chat = chats[peer]!;
      final bool isMe = (fromUser == _myUsername);
      chat.messages.add(Message(text, isMe));
      chat.lastMessage = text;
      chat.time = _now();
      _updateController.add(null);
    };

    _initialized = true;
  }

  void register(String username, String password) {
    _ffiClient.register(username, password);
  }

  void addChat(String username) {
    _ensureChat(username);
    changeRecipient(username);
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
}
