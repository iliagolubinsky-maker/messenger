#include "client.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <boost/beast/ssl.hpp>
#include <nlohmann/json.hpp>
#include <sodium.h>
#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <chrono>
#include <fstream>
#include <filesystem>


using namespace std;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

//////////////////////////
// Helper functions
//////////////////////////

string toHex(const unsigned char* data, size_t len) {
    string hex(len * 2 + 1, '\0');
    sodium_bin2hex(&hex[0], hex.size(), data, len);
    return hex;
}

vector<unsigned char> fromHex(const string& hex) {
    vector<unsigned char> out(hex.size() / 2);
    size_t bin_len;
    sodium_hex2bin(out.data(), out.size(), hex.c_str(), hex.size(),
                   nullptr, &bin_len, nullptr);
    out.resize(bin_len);
    return out;
}

string encrypt_session(const string& msg, const array<unsigned char, crypto_secretbox_KEYBYTES>& session_key) {
    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(nonce, sizeof nonce);

    vector<unsigned char> cipher(msg.size() + crypto_secretbox_MACBYTES);

    crypto_secretbox_easy(
        cipher.data(),
        reinterpret_cast<const unsigned char*>(msg.data()),
        msg.size(),
        nonce,
        session_key.data()
    );

    // nonce + cipher
    vector<unsigned char> full(nonce, nonce + crypto_secretbox_NONCEBYTES);
    full.insert(full.end(), cipher.begin(), cipher.end());

    size_t b64_len = sodium_base64_encoded_len(
        full.size(),
        sodium_base64_VARIANT_ORIGINAL
    );

    string out(b64_len, '\0');

    sodium_bin2base64(
        out.data(),
        out.size(),
        full.data(),
        full.size(),
        sodium_base64_VARIANT_ORIGINAL
    );

    out.resize(strlen(out.c_str()));
    return out;
}



string decrypt_session(const string& enc, const array<unsigned char, crypto_secretbox_KEYBYTES>& session_key) {
    vector<unsigned char> buffer(enc.size());
    size_t decoded_len;

    sodium_base642bin(
        buffer.data(),
        buffer.size(),
        enc.c_str(),
        enc.size(),
        nullptr,
        &decoded_len,
        nullptr,
        sodium_base64_VARIANT_ORIGINAL
    );

    buffer.resize(decoded_len);

    if (decoded_len < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES)
        throw runtime_error("Invalid encrypted message");

    unsigned char* nonce = buffer.data();
    unsigned char* cipher = buffer.data() + crypto_secretbox_NONCEBYTES;

    size_t cipher_len = decoded_len - crypto_secretbox_NONCEBYTES;

    vector<unsigned char> msg(cipher_len - crypto_secretbox_MACBYTES);

    if (crypto_secretbox_open_easy(
            msg.data(),
            cipher,
            cipher_len,
            nonce,
            session_key.data()) != 0)
    {
        throw runtime_error("Bad MAC / tampered message");
    }

    return string(reinterpret_cast<char*>(msg.data()), msg.size());
}




//////////////////////////
// MessengerClient methods
//////////////////////////

MessengerClient::MessengerClient(const string& from)
    : from_user(from), peer_key_received(false), connected(false)
{
    int init_ret = sodium_init();
    if (init_ret < 0) {
        throw std::runtime_error("libsodium initialization failed");
    }
}

void MessengerClient::start() {
    connect();
    connected = true;
    reader_thread = thread([this]{ readerLoop(); });


    cout << "MessengerClient started for " << from_user << endl;
}

void MessengerClient::login(string username, string password){
    json j;
    j["req"] = "login";
    j["client"] = username;
    j["password"] = password;
    boost::system::error_code ec;
    ws.write(asio::buffer(j.dump()), ec);
    if (ec) {
        cerr << "Send failed: " << ec.message() << endl;
    }
}



void MessengerClient::change_recipient(const string& to) {
    to_user = to;
    if (change_pubkey(to)) {
        cerr << "Public key for the recipient has been found " << to << endl;
        session_key = session_keys[to];
        return;
    }
    {
        lock_guard<mutex> lk(key_mtx);
        peer_key_received = false;
    }
    json j;
    j["type"] = "receive_keys";
    j["to"] = to_user;
    boost::system::error_code ec;
    ws.write(asio::buffer(j.dump()), ec);
    if (ec) {
        cerr << "Send failed: " << ec.message() << endl;
    }
}

void MessengerClient::sendMessageToPeer(const string& text) {
    if (!connected || !log_status) return;
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    string cipher = encrypt_session(text, session_key);
    json j;
    j["from"] = from_user;
    j["to"] = to_user;
    j["cipher"] = cipher;
    j["timestamp"] = now;

    boost::system::error_code ec;
    ws.write(asio::buffer(j.dump()), ec);
    if (ec) {
        cerr << "Send failed: " << ec.message() << endl;
    }
}

void MessengerClient::stop() {
    connected = false;
    boost::system::error_code ec;
    ws.close(websocket::close_code::normal, ec);
    if (reader_thread.joinable()) reader_thread.join();
}


int MessengerClient::popMessage(char* from, int from_len, char* to, int to_len, char* text, int text_len){
    std::lock_guard<mutex> lk(message_mutex);
    if (message_queue.empty()) return 0;
    Message msg = message_queue.front();
    message_queue.pop();

    strncpy(from, msg.from.c_str(), from_len-1);
    from[from_len-1] = '\0';
    strncpy(to, msg.to.c_str(), to_len-1);
    strncpy(text, msg.message.c_str(), text_len-1);
    text[text_len-1] = '\0';
    return 1;
}


void MessengerClient::relogin(){
    string username;
    string password;
    login(username, password);
}

void MessengerClient::register_client(string username, string password){
    json j;
    j["req"] = "register";
    j["client"] = username;
    j["password"] = password;
    boost::system::error_code ec;
    ws.write(asio::buffer(j.dump()), ec);
    if (ec) {
        cerr << "Send failed: " << ec.message() << endl;
    }
}

//////////////////////////
// Private methods
//////////////////////////

void MessengerClient::connect() {
    std::string cert_path;
    #if _WIN32
        cert_path = "C:\\messenger_app\\app_fold\\build\\windows\\x64\\runner\\Debug\\server.crt";
    #else 
        cert_path = "build/linux/x64/debug/bundle/lib/server.crt";
    #endif
    
    try {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            std::cerr << "Current working directory: " << cwd << std::endl;
        } else {
            std::cerr << "Failed to get current working directory." << std::endl;
        }

        if (!std::filesystem::exists(cert_path)) {
            std::cerr << "ERROR loading certificate" << std::endl;
        }
        ctx.load_verify_file(cert_path);
        ctx.set_verify_mode(ssl::verify_peer);
        
        ctx.set_verify_callback(
            [](bool preverified, ssl::verify_context& ctx) {
                if (!preverified) return false;
                return true;
            }
        );
        
        std::cerr << "Certificate loaded successfully." << std::endl;
    } catch (const boost::system::system_error& e) {
        std::cerr << "ERROR loading certificate: " << e.what() << std::endl;
        std::cerr << "Error code: " << e.code() << std::endl;
        throw;
    }
    
    tcp::resolver resolver(ioc);
    auto const results = resolver.resolve("192.168.86.168", "8080");

    asio::connect(ws.next_layer().next_layer(), results.begin(), results.end());

    SSL_set_tlsext_host_name(
        ws.next_layer().native_handle(),
        "localhost"
    );

    try {
        ws.next_layer().handshake(ssl::stream_base::client);
        ws.handshake("localhost", "/");
        std::cerr << "Connected securely (TLS)." << std::endl;
    } catch (const boost::system::system_error& e) {
        std::cerr << "Handshake failed: " << e.what() << std::endl;
        throw;
    }
}



void MessengerClient::register_pubkey() {
    
    
    
    if (!loadKey("private_key.bin")) {
        crypto_box_keypair(my_public.data(), my_private.data());
        if (!savePrivateKey("private_key.bin", my_private.data())) {
            cerr << "Warning: Failed to save key." << endl;
        }
        json reg;
        reg["from"] = from_user;
        reg["pubkey"] = toHex(my_public.data(), PUBKEY_BYTES);
        boost::system::error_code ec;
        ws.write(asio::buffer(reg.dump()), ec);
        if (ec) {
            cerr << "Public key registration failed: " << ec.message() << endl;
        }
    } else {
        json reg;
        reg["connect"] = from_user;
        boost::system::error_code ec;
        ws.write(asio::buffer(reg.dump()), ec);
        if (ec) {
            cerr << "Public key registration failed: " << ec.message() << endl;
        }
    }
}

void MessengerClient::readerLoop() {
    boost::beast::flat_buffer buffer;

    while (connected) {
        boost::system::error_code ec;

        // Blocking read from websocket
        ws.read(buffer, ec);
        if (ec) {
            std::cerr << "[readerLoop] disconnected: " << ec.message() << std::endl;
            connected = false;
            break;
        }

        // Convert buffer to string
        std::string raw = boost::beast::buffers_to_string(buffer.data());
        buffer.consume(buffer.size());

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(raw);
        } catch (...) {
            std::cerr << "[readerLoop] Invalid JSON: " << raw << std::endl;
            continue;
        }
        if (j.contains("type") && j["type"] == "key") {
            if (!j.contains("from") || !j.contains("pubkey")) {
                std::cerr << "[readerLoop] malformed key message\n";
                continue;
            }

            std::string from = j["from"].get<std::string>();
            std::vector<unsigned char> pk = fromHex(j["pubkey"].get<std::string>());

            if (pk.size() != PUBKEY_BYTES) {
                std::cerr << "[readerLoop] invalid pubkey size\n";
                continue;
            }

            {
                std::lock_guard<std::mutex> lk(peer_key_mtx);
                
                if (has_pubkey(from)){
                    memcpy(peer_public.data(), pk.data(), PUBKEY_BYTES);
                } else {
                    memcpy(peer_public_keys[from].data(), pk.data(), PUBKEY_BYTES);
                    memcpy(peer_public.data(), pk.data(), PUBKEY_BYTES);
                }
            }
            {
                std::lock_guard<std::mutex> sk(session_keys_mtx);
                derive_session_key();
                memcpy(session_keys[from].data(), session_key.data(), crypto_secretbox_KEYBYTES);
            }

            std::cerr << "[readerLoop] Stored and loaded public key for " << from << std::endl;
            continue;
        }
        else if (j.contains("req")){
            if (j["req"] == "login"){
                if (j["status"] == "approved"){
                    log_status = true;
                    register_pubkey();
                    continue;
                } else {
                    cerr << j["message"] << endl;
                    connected = false;
                    boost::system::error_code ec;
                    ws.close(websocket::close_code::protocol_error, ec);
                    return;

                }
            } else if (j["req"] == "register"){
                if (j["status"] == "approved"){
                    register_pubkey();
                    log_status = true;
                    continue;
                } else {
                    cerr << j["message"] << endl;
                }
            }
        }
        else if (j.contains("type") && j["type"] == "history"){
            try{
                string recipient = (j["from"] == from_user) ? j["to"] : j["from"];
                std::cerr << "the recipient is " << recipient << endl;
                if (has_pubkey(recipient)){
                    change_pubkey(recipient);
                    auto it = session_keys.find(recipient);
                    if (it == session_keys.end()){
                        derive_session_key();
                        {
                            std::lock_guard<std::mutex> sk(session_keys_mtx);
                            memcpy(session_keys[recipient].data(), session_key.data(), crypto_secretbox_KEYBYTES);
                        }
                        
                    } else {
                        session_key = it->second;
                    }
                    
                    
                    string decrypted = decrypt_session(j["cipher"].get<string>(), session_key);
                    cerr << decrypted << endl;
                    {
                        lock_guard<mutex> lk(message_mutex);
                        message_queue.push({j["from"], j["to"], decrypted});
                    }
                } else {
                    std::cerr << "No public key found " << std::endl;
                }
                
            } catch (const std::exception& e) {
                std::cerr << "[readerLoop] Decryption error: " << e.what() << std::endl;

            }

        }
        else if (j.contains("cipher") && j.contains("from")) {
            std::string sender = j["from"].get<std::string>();

            if (!has_pubkey(sender)) {
                std::cerr << "[readerLoop] cannot find public key for " << sender << std::endl;
                continue;
            }
            change_pubkey(sender);
            session_key = session_keys[sender];

            try {
                std::string decrypted = decrypt_session(j["cipher"], session_key);

                {
                    lock_guard<mutex> lk(message_mutex);
                    message_queue.push({sender, j["to"], decrypted});
                }

                std::cout << "[MESSAGE] " << sender << ": " << decrypted << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[readerLoop] Decryption error: "
                          << e.what() << std::endl;
            }
            
            

            continue;
        }

    }
}



bool MessengerClient::loadKey(const string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        cerr << "Failed to open private key file: " << filename << endl;
        return false;
    }
    
    file.read(reinterpret_cast<char*>(my_private.data()), PRIVKEY_BYTES);
    
    file.close();
    return true;
}

bool MessengerClient::savePrivateKey(const string& filename, const unsigned char priv[PRIVKEY_BYTES]) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        cerr << "Failed to open private key file for writing: " << filename << endl;
        return false;
    }
    file.write(reinterpret_cast<const char*>(priv), PRIVKEY_BYTES);
    file.close();
    return true;
}


bool MessengerClient::change_pubkey(const string& to) {
    std::lock_guard<std::mutex> lk(peer_key_mtx);
    auto it = peer_public_keys.find(to);
    if (it == peer_public_keys.end()) {
        return false;
    }

    std::memcpy(peer_public.data(), it->second.data(), PUBKEY_BYTES);
    return true;
}

bool MessengerClient::has_pubkey(const string& user) {
    return peer_public_keys.find(user) != peer_public_keys.end();

}

void MessengerClient::derive_session_key() {
    unsigned char shared[crypto_scalarmult_BYTES];

    if (crypto_scalarmult(shared,
                          my_private.data(),
                          peer_public.data()) != 0) {
        throw std::runtime_error("Key exchange failed");
    }

    crypto_generichash(session_key.data(),
                       session_key.size(),
                       shared, sizeof shared,
                       nullptr, 0);

    session_ready = true;
}








