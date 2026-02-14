import 'dart:ffi';
import 'dart:isolate';
import 'dart:io';
import 'dart:async';
import 'package:ffi/ffi.dart';

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

typedef ClientPopMessageC =
    Int32 Function(
      Pointer<Void>,
      Pointer<Utf8>,
      Int32,
      Pointer<Utf8>,
      Int32,
      Pointer<Utf8>,
      Int32,
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
    );

typedef ClientGetLogStatusC = Bool Function(Pointer<Void>);
typedef ClientGetLogStatusDart = bool Function(Pointer<Void>);

typedef ClientRegisterC =
    Void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef ClientRegisterDart =
    void Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);

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
  Timer? _pollTimer;

  final from_buf = malloc.allocate<Uint8>(256);
  final to_buf = malloc.allocate<Uint8>(256);
  final message_buf = malloc.allocate<Uint8>(4096);

  /// Dart callback for incoming messages
  void Function(String from, String to, String text)? onMessage;

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
    while (_clientPop(
          _client,
          from_buf.cast<Utf8>(),
          256,
          to_buf.cast<Utf8>(),
          256,
          message_buf.cast<Utf8>(),
          4096,
        ) !=
        0) {
      final from = from_buf.cast<Utf8>().toDartString();
      final to = to_buf.cast<Utf8>().toDartString();
      final message = message_buf.cast<Utf8>().toDartString();
      onMessage?.call(from, to, message);
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
}
