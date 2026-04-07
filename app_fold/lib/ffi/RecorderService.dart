import 'package:record/record.dart';
import 'package:path_provider/path_provider.dart';
import 'dart:typed_data';
import 'dart:io';

class Recorderservice {
  // Uses the 'record' package for cross-platform support including Linux.
  final _recorder = AudioRecorder();

  Future<void> init() async {
    // Record handles its own initialization and permission checks.
  }

  Future<void> start() async {
    final tempDir = await getTemporaryDirectory();
    final path = '${tempDir.path}/audio_msg.wav';

    // Verify microphone access.
    if (await _recorder.hasPermission()) {
      const config = RecordConfig(encoder: AudioEncoder.wav);
      await _recorder.start(config, path: path);
    }
  }

  Future<Uint8List?> stop() async {
    final path = await _recorder.stop();
    if (path == null) return null;

    // Convert the file to bytes to pass through the FFI layer.
    return await File(path).readAsBytes();
  }

  void dispose() {
    _recorder.dispose();
  }
}
