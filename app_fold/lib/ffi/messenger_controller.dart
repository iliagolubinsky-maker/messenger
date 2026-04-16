import 'package:flutter/material.dart';
import 'RecorderService.dart';
import 'dart:async';
import 'messenger_ffi.dart';
import '../main.dart';
import 'dart:typed_data';
import 'package:flutter_webrtc/flutter_webrtc.dart';
import 'dart:convert';

class MessengerController {
  static final MessengerController _instance = MessengerController._internal();
  factory MessengerController() => _instance;
  MessengerController._internal();

  late MessengerFFI _ffiClient;
  String? _myUsername;
  final Map<String, Chat> chats = {};
  final StreamController<void> _updateController = StreamController.broadcast();
  Stream<void> get updates => _updateController.stream;
  RTCPeerConnection? _peerConnection;
  MediaStream? _localStream;
  String? _currentRecipient;
  bool _initialized = false;
  final RTCVideoRenderer _remoteRenderer = RTCVideoRenderer();
  RTCVideoRenderer get remoteRenderer => _remoteRenderer;

  void init(String from) {
    _myUsername = from;
    _ffiClient = MessengerFFI(from);
    _ffiClient.start();
    _setupPeerConnection();

    _ffiClient.onMessage =
        (
          String type,
          String from,
          String to,
          String text,
          bool isAudio,
          Uint8List audioBytes,
        ) {
          final String peer = (from == _myUsername) ? to : from;
          _ensureChat(peer);
          if (type == "call_req") {
            handleIncomingOffer(text);
          } else if (type == "call_resp") {
            handleIncomingAnswer(text);
          } else if (type == "call_candidate") {
            handleIncomingCandidate(text);
          }
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
    String type = "msg";
    _ffiClient.send(type, text);
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

  Future<void> createCallOffer() async {
    _localStream = await navigator.mediaDevices.getUserMedia({
      'audio': true,
      'video': false,
    });

    _localStream!.getTracks().forEach((track) {
      _peerConnection!.addTrack(track, _localStream!);
    });

    RTCSessionDescription offer = await _peerConnection!.createOffer();
    await _peerConnection!.setLocalDescription(offer);

    _ffiClient.send("call_req", offer.sdp!);
  }

  Future<void> handleIncomingOffer(String sdpText) async {
    RTCSessionDescription remoteOffer = RTCSessionDescription(sdpText, 'offer');
    await _peerConnection!.setRemoteDescription(remoteOffer);

    RTCSessionDescription answer = await _peerConnection!.createAnswer();

    await _peerConnection!.setLocalDescription(answer);
    String type = "call_resp";
    _ffiClient.send(type, answer.sdp!);
  }

  Future<void> _setupPeerConnection() async {
    await _remoteRenderer.initialize();
    Map<String, dynamic> configuration = {
      "iceServers": [
        {"urls": "stun:stun.l.google.com:19302"},
      ],
      "sdpSemantics": "unified-plan",
    };

    _peerConnection = await createPeerConnection(configuration);
    _peerConnection!.onIceCandidate = (RTCIceCandidate candidate) {
      if (candidate.candidate != null) {
        _ffiClient.sendJson({
          'to': _currentRecipient,
          'from': _myUsername,
          'type': 'call_candidate',
          'candidate': {
            'candidate': candidate.candidate,
            'sdpMid': candidate.sdpMid,
            'sdpMLineIndex': candidate.sdpMLineIndex,
          },
        });
      }
    };

    _peerConnection!.onTrack = (RTCTrackEvent event) {
      if (event.track.kind == 'audio') {
        if (event.streams.isNotEmpty) {
          _remoteRenderer.srcObject = event.streams[0];
        }
      }
    };
  }

  Future<void> handleIncomingAnswer(String sdpText) async {
    if (_peerConnection == null) return;
    RTCSessionDescription description = RTCSessionDescription(
      sdpText,
      'answer',
    );
    await _peerConnection!.setRemoteDescription(description);
  }

  Future<void> handleIncomingCandidate(String jsonString) async {
    if (_peerConnection == null) return;

    final data = jsonDecode(jsonString);
    final candidateData = data['candidate'];

    RTCIceCandidate candidate = RTCIceCandidate(
      candidateData['candidate'],
      candidateData['sdpMid'],
      candidateData['sdpMLineIndex'],
    );

    await _peerConnection!.addCandidate(candidate);
  }
}
