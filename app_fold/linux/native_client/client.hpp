#ifdef _WIN32
  #define DLL_EXPORT __declspec(dllexport)
#else
  #define DLL_EXPORT
#endif


#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <boost/beast/ssl.hpp>
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <unordered_map>
#include <queue>
using namespace std;
using json = nlohmann::json;
struct Message{
  std::string from;
  std::string to;
  std::string message;
  std::string type;
  bool isAudio;
  bool isOffer;
};

constexpr size_t PUBKEY_BYTES = 32;    // crypto_box_PUBLICKEYBYTES
constexpr size_t PRIVKEY_BYTES = 32;   // crypto_box_SECRETKEYBYTES
constexpr size_t NONCE_BYTES = 24;     // crypto_box_NONCEBYTES
constexpr size_t MAC_BYTES = 16;       // crypto_box_MACBYTES
// Helper functions
std::string toHex(const unsigned char* data, size_t len);
std::vector<unsigned char> fromHex(const std::string& hex);

std::string encrypt_session(
    const std::string& msg,
    const std::array<unsigned char, crypto_secretbox_KEYBYTES>& session_key
);

std::string decrypt_session(
    const std::string& enc,
    const std::array<unsigned char, crypto_secretbox_KEYBYTES>& session_key
);

// MessengerClient class
class MessengerClient {
public:
    MessengerClient(const std::string& from);
    void change_recipient(const std::string& to);

    // Start client (connects to server and registers public key)
    void start();

    // Send a text message to the peer
    void send_message_to_peer(const string& type, const std::string& text);

    // Stop client (closes connection)
    void stop();

    int pop_message(char* from, int from_len, char* to, int to_len, char* text, int text_len, char* type, int type_len, int* isAudio);

    void login(std::string username, std::string password);

    void relogin();

    bool get_size(int* from_size, int* to_size, int* msg_size, int* type_size);

    bool get_log_status(){
      return log_status;
    }

    void register_client(std::string username, std::string password);

    void send_audio_to_peer(const std::vector<unsigned char>& audioData);

    void send_call_req(const std::string& sdp);

    void send_json(json payload);
    
private:

    std::queue<Message> message_queue;
    std::mutex message_mutex;
    std::string from_user;
    std::string to_user;
    std::array<unsigned char, PUBKEY_BYTES> my_public{};
    std::array<unsigned char, PRIVKEY_BYTES> my_private{};
    std::unordered_map<std::string, std::array<unsigned char, PUBKEY_BYTES>> peer_public_keys;
    std::unordered_map<std::string, std::array<unsigned char, crypto_secretbox_KEYBYTES>> session_keys;
    std::array<unsigned char, PUBKEY_BYTES> peer_public{};
    std::array<unsigned char, crypto_secretbox_KEYBYTES> session_key;
    bool session_ready = false;
    std::mutex peer_key_mtx;
    std::mutex session_keys_mtx;
    std::mutex msg_mtx;
    bool log_status = false;

    // Boost.Asio context
    boost::asio::io_context ioc;

    // SSL context
    boost::asio::ssl::context ctx{boost::asio::ssl::context::tlsv12_client};

    // WebSocket stream over SSL
    boost::beast::websocket::stream<boost::beast::ssl_stream<boost::asio::ip::tcp::socket>> ws{ioc, ctx};

    // Threading for reading messages
    std::thread reader_thread;
    std::mutex key_mtx;
    std::condition_variable key_cv;

    bool peer_key_received;
    bool connected;
    // Internal methods
    bool change_pubkey(const std::string& to);
    void connect();
    void register_pubkey();
    void reader_loop();
    bool loadKey(const std::string& filename);
    bool save_private_key(const std::string& filename, const unsigned char priv[PRIVKEY_BYTES]);
    void send_public_key();
    bool has_pubkey(const std::string& user);
    void derive_session_key();

};

#endif // CLIENT_HPP
