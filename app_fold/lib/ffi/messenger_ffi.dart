import 'dart:ffi';
import 'dart:isolate';
import 'dart:io';
import 'dart:async';
import 'package:ffi/ffi.dart';
import 'dart:typed_data';

/// -------------------- FFI TYPEDEFS -------------------- ///

// Client creation and lifecycle
typedef ClientCreateC = Pointer<Void> Function(Pointer<Utf8>);
typedef ClientCreateDart = Pointer<Void> Function(Pointer<Utf8>);

typedef ClientStartC = Void Function(Pointer<Void>);
typedef ClientStartDart = void Function(Pointer<Void>);

typedef ClientSendC = Void Function(Pointer<Void>, Pointer<Utf8>);
typedef ClientSendDart = void Function(Pointer<Void>, Pointer<Utf8>);

typedef ClientStopC = Void Function(Pointer<Void>);
typedef ClientStopDart = void Function(Pointer<Void>);

typedef ClientDestroyC = Void Function(Pointer<Void>);
typedef ClientDestroyDart = void Function(Pointer<Void>);

typedef ClientChangeC = Void Function(Pointer<Void>, Pointer<Utf8>);
typedef ClientChangeDart = void Function(Pointer<Void>, Pointer<Utf8>);

typedef ClientLoginC =
    Void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef ClientLoginDart =
    void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);

typedef ClientSendBinaryC = Void Function(Pointer<Void>, Pointer<Uint8>, Int32);
typedef ClientSendBinaryDart =
    void Function(Pointer<Void>, Pointer<Uint8>, int);

typedef ClientPopMessageC =
    Int32 Function(
      Pointer<Void>,
      Pointer<Utf8>,
      Int32,
      Pointer<Utf8>,
      Int32,
      Pointer<Utf8>,
      Int32,
      Pointer<Int32>,
    );
typedef ClientPopMessageDart =
    int Function(
      Pointer<Void>,
      Pointer<Utf8>,
      int,
      Pointer<Utf8>,
      int,
      Pointer<Utf8>,
      int,
      Pointer<Int32>,
    );

typedef ClientGetLogStatusC = Bool Function(Pointer<Void>);
typedef ClientGetLogStatusDart = bool Function(Pointer<Void>);

typedef ClientRegisterC =
    Void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef ClientRegisterDart =
    void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);

typedef ClientGetSizeC =
    Bool Function(
      Pointer<Void>,
      Pointer<Int32>,
      Pointer<Int32>,
      Pointer<Int32>,
    );

typedef ClientGetSizeDart =
    bool Function(
      Pointer<Void>,
      Pointer<Int32>,
      Pointer<Int32>,
      Pointer<Int32>,
    );

/// -------------------- MESSENGER FFI CLASS -------------------- ///

class MessengerFFI {
  late DynamicLibrary _lib;
  // Client functions
  late ClientCreateDart _clientCreate;
  late ClientStartDart _clientStart;
  late ClientSendDart _clientSend;
  late ClientStopDart _clientStop;
  late ClientDestroyDart _clientDestroy;
  late ClientChangeDart _clientChange;
  late ClientPopMessageDart _clientPop;
  late Pointer<Void> _client;
  late ClientLoginDart _clientLogin;
  late ClientGetLogStatusDart _clientLogStatus;
  late ClientRegisterDart _clientRegister;
  late ClientSendBinaryDart _clientSendBinary;
  late ClientGetSizeDart _clientGetSize;
  Timer? _pollTimer;

  final isAudio_buf = malloc.allocate<Int32>(1);

  /// Dart callback for incoming messages
  void Function(
    String from,
    String to,
    String text,
    bool isAudio,
    Uint8List audioBytes,
  )?
  onMessage;

  MessengerFFI(String from) {
    // Open shared library
    if (Platform.isWindows) {
      _lib = DynamicLibrary.open("messenger_client.dll");
    } else {
      _lib = DynamicLibrary.open("libmessenger_client.so");
    }

    // Lookup all functions
    _clientCreate = _lib.lookupFunction<ClientCreateC, ClientCreateDart>(
      "client_create",
    );
    _clientStart = _lib.lookupFunction<ClientStartC, ClientStartDart>(
      "client_start",
    );
    _clientSend = _lib.lookupFunction<ClientSendC, ClientSendDart>(
      "client_send",
    );
    _clientStop = _lib.lookupFunction<ClientStopC, ClientStopDart>(
      "client_stop",
    );
    _clientDestroy = _lib.lookupFunction<ClientDestroyC, ClientDestroyDart>(
      "client_destroy",
    );
    _clientChange = _lib.lookupFunction<ClientChangeC, ClientChangeDart>(
      "client_change_recipient",
    );
    _clientPop = _lib.lookupFunction<ClientPopMessageC, ClientPopMessageDart>(
      "client_pop_message",
    );
    _clientLogin = _lib.lookupFunction<ClientLoginC, ClientLoginDart>(
      "client_login",
    );
    _clientLogStatus = _lib
        .lookupFunction<ClientGetLogStatusC, ClientGetLogStatusDart>(
          "client_get_log_status",
        );
    _clientRegister = _lib.lookupFunction<ClientRegisterC, ClientRegisterDart>(
      "client_register",
    );

    _clientSendBinary = _lib
        .lookupFunction<ClientSendBinaryC, ClientSendBinaryDart>(
          "client_send_binary",
        );

    _clientGetSize = _lib.lookupFunction<ClientGetSizeC, ClientGetSizeDart>(
      "client_get_size",
    );

    // Create the client
    final fromPtr = from.toNativeUtf8();
    _client = _clientCreate(fromPtr);
    malloc.free(fromPtr);

    if (_client == Pointer<Void>.fromAddress(0)) {
      throw Exception('client_create returned NULL');
    }

    _pollTimer = Timer.periodic(
      const Duration(milliseconds: 50),
      (_) => popMessages(),
    );
  }

  void login(String username, String password) {
    final userptr = username.toNativeUtf8();
    final passptr = password.toNativeUtf8();
    _clientLogin(_client, userptr, passptr);
    malloc.free(userptr);
    malloc.free(passptr);
  }

  /// Change recipient
  void change(String to) {
    final toPtr = to.toNativeUtf8();
    _clientChange(_client, toPtr);
    malloc.free(toPtr);
  }

  void register(String username, String password) {
    final userptr = username.toNativeUtf8();
    final passptr = password.toNativeUtf8();
    _clientRegister(_client, userptr, passptr);
    malloc.free(userptr);
    malloc.free(passptr);
  }

  void popMessages() {
    final fromSizePtr = malloc.allocate<Int32>(1);
    final toSizePtr = malloc.allocate<Int32>(1);
    final msgSizePtr = malloc.allocate<Int32>(1);

    try {
      while (_clientGetSize(_client, fromSizePtr, toSizePtr, msgSizePtr)) {
        final fromBuf = malloc.allocate<Uint8>(fromSizePtr.value);
        final toBuf = malloc.allocate<Uint8>(toSizePtr.value);
        final msgBuf = malloc.allocate<Uint8>(msgSizePtr.value);

        try {
          if (_clientPop(
                _client,
                fromBuf.cast<Utf8>(),
                fromSizePtr.value,
                toBuf.cast<Utf8>(),
                toSizePtr.value,
                msgBuf.cast<Utf8>(),
                msgSizePtr.value,
                isAudio_buf,
              ) !=
              0) {
            final from = fromBuf.cast<Utf8>().toDartString();
            final to = toBuf.cast<Utf8>().toDartString();
            final bool isAudio = isAudio_buf.value != 0;

            if (isAudio) {
              final int audioDataSize = msgSizePtr.value - 1;
              final audioBytes = Uint8List.fromList(
                msgBuf.asTypedList(audioDataSize),
              );

              onMessage?.call(from, to, "Voice Message", true, audioBytes);
            } else {
              final messageText = msgBuf.cast<Utf8>().toDartString();
              onMessage?.call(from, to, messageText, false, Uint8List(0));
            }
          }
        } finally {
          malloc.free(fromBuf);
          malloc.free(toBuf);
          malloc.free(msgBuf);
        }
      }
    } finally {
      malloc.free(fromSizePtr);
      malloc.free(toSizePtr);
      malloc.free(msgSizePtr);
    }
  }

  /// Start client (connects & begins reader loop)
  void start() => _clientStart(_client);

  /// Send a message
  void send(String message) {
    final msgPtr = message.toNativeUtf8();
    _clientSend(_client, msgPtr);
    malloc.free(msgPtr);
  }

  /// Stop client
  void stop() => _clientStop(_client);

  /// Destroy client
  void dispose() {
    _pollTimer?.cancel();
    _clientDestroy(_client);
  }

  bool getStatus() {
    return _clientLogStatus(_client);
  }

  void sendBinary(Uint8List data) {
    final Pointer<Uint8> ptr = malloc.allocate<Uint8>(data.length);
    final Uint8List nativeBytes = ptr.asTypedList(data.length);
    nativeBytes.setAll(0, data);
    _clientSendBinary(_client, ptr, data.length);
    malloc.free(ptr);
  }
}
