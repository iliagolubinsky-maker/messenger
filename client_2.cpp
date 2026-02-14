#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <boost/beast/ssl.hpp>
#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>
#include <sodium.h>

using namespace std;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using json = nlohmann::json;


string toHex(const unsigned char* data, size_t len){
    string hex(len * 2 + 1, '\0');
    sodium_bin2hex(&hex[0], hex.size(), data, len);
    return hex;
}

vector<unsigned char> fromHex(const string& hex){
    vector<unsigned char> out(hex.size()/2);
    size_t bin_len;
    sodium_hex2bin(out.data(), out.size(), hex.c_str(), hex.size(), NULL, &bin_len, NULL);
    out.resize(bin_len);
    return out;
}

string encrypt_message(const string &text,
                       const unsigned char receiver_pub[crypto_box_PUBLICKEYBYTES],
                       const unsigned char sender_priv[crypto_box_SECRETKEYBYTES])
{
    unsigned char nonce[crypto_box_NONCEBYTES];
    randombytes_buf(nonce, sizeof nonce);

    vector<unsigned char> cipher(text.size() + crypto_box_MACBYTES);
    if (crypto_box_easy(cipher.data(),
                        reinterpret_cast<const unsigned char*>(text.data()),
                        text.size(),
                        nonce,
                        receiver_pub,
                        sender_priv) != 0)
    {
        throw runtime_error("Encryption failed");
    }

    vector<unsigned char> full(nonce, nonce + crypto_box_NONCEBYTES);
    full.insert(full.end(), cipher.begin(), cipher.end());

    size_t b64_len = sodium_base64_encoded_len(full.size(), sodium_base64_VARIANT_ORIGINAL);
    string b64(b64_len, '\0');
    sodium_bin2base64(&b64[0], b64.size(), full.data(), full.size(), sodium_base64_VARIANT_ORIGINAL);
    b64.resize(strlen(b64.c_str()));
    return b64;
}

string decrypt_message(const string &encoded,
                       const unsigned char sender_pub[crypto_box_PUBLICKEYBYTES],
                       const unsigned char receiver_priv[crypto_box_SECRETKEYBYTES])
{
    vector<unsigned char> buffer(encoded.size());
    size_t decoded_len;
    sodium_base642bin(buffer.data(), buffer.size(),
                      encoded.c_str(), encoded.size(),
                      NULL, &decoded_len, NULL,
                      sodium_base64_VARIANT_ORIGINAL);
    buffer.resize(decoded_len);

    if (decoded_len < crypto_box_NONCEBYTES + crypto_box_MACBYTES)
        throw runtime_error("Invalid encrypted message");

    unsigned char nonce[crypto_box_NONCEBYTES];
    memcpy(nonce, buffer.data(), crypto_box_NONCEBYTES);

    unsigned char* cipher = buffer.data() + crypto_box_NONCEBYTES;
    size_t cipher_len = decoded_len - crypto_box_NONCEBYTES;

    vector<unsigned char> decrypted(cipher_len - crypto_box_MACBYTES);

    if (crypto_box_open_easy(decrypted.data(), cipher, cipher_len, nonce, sender_pub, receiver_priv) != 0)
        throw runtime_error("Decryption failed (forged?)");

    return string(reinterpret_cast<char*>(decrypted.data()), decrypted.size());
}

int main() {
    try {
        string from_user = "client_2";
        string to_user = "client_1";
        if (sodium_init() < 0) throw runtime_error("Failed to init sodium");
        unsigned char my_public[crypto_box_PUBLICKEYBYTES];
        unsigned char my_private[crypto_box_SECRETKEYBYTES];
        
        
        crypto_box_keypair(my_public, my_private);

        unsigned char peer_public[crypto_box_PUBLICKEYBYTES];
        bool peer_key_received = false;

        asio::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        ctx.load_verify_file("server.crt");
        ctx.set_verify_mode(ssl::verify_peer);

        

        tcp::resolver resolver(ioc);
        auto const results = resolver.resolve("127.0.0.1", "8080");

        websocket::stream<beast::ssl_stream<tcp::socket>> ws(ioc, ctx);
        asio::connect(ws.next_layer().next_layer(), results.begin(), results.end());
        ws.next_layer().handshake(ssl::stream_base::client);
        ws.handshake("127.0.0.1", "/");

        cout << "Connected to WebSocket server." << endl;

        json reg;
        reg["from"] = from_user;
        reg["pubkey"] = toHex(my_public, crypto_box_PUBLICKEYBYTES);
        ws.write(asio::buffer(reg.dump()));
        beast::flat_buffer pub_buffer;
        while (!peer_key_received) {
            boost::system::error_code ec;
            ws.read(pub_buffer, ec);
            if (ec) throw runtime_error("Disconnected from server.");

            auto msg = beast::buffers_to_string(pub_buffer.data());
            pub_buffer.consume(pub_buffer.size());

            json pub_msg = json::parse(msg);
            if (pub_msg.contains("pubkey") && pub_msg["from"] == to_user) {
                vector<unsigned char> pk = fromHex(pub_msg["pubkey"]);
                if (pk.size() != crypto_box_PUBLICKEYBYTES) throw runtime_error("Invalid peer pubkey length");
                memcpy(peer_public, pk.data(), crypto_box_PUBLICKEYBYTES);
                peer_key_received = true;
                cout << "Received public key of " << to_user << endl;
            }
        }
        thread reader([&]() {
            beast::flat_buffer buffer;
            while (true) {
                boost::system::error_code ec;
                ws.read(buffer, ec);
                if (ec) {
                    cout << "Disconnected from server." << endl;
                    break;
                }
                auto raw = beast::buffers_to_string(buffer.data());
                buffer.consume(buffer.size());
                json j = json::parse(raw);

                if (j.contains("cipher")) {
                    try {
                        string decrypted = decrypt_message(j["cipher"], peer_public, my_private);
                        cout << "\n[DECRYPTED] " << decrypted << endl;
                    } catch (exception& e) {
                        cerr << "Decryption error: " << e.what() << endl;
                    }
                } else {
                    cout << "[INFO] " << raw << endl;
                }
            }
        });

        while (true) {
            string text;
            cout << "Enter message: ";
            getline(cin, text);

            if (!peer_key_received) {
                cout << "Peer key not received yet.\n";
                continue;
            }

            string cipher = encrypt_message(text, peer_public, my_private);
            json j;
            j["from"] = from_user;
            j["to"] = to_user;
            j["cipher"] = cipher;

            ws.write(asio::buffer(j.dump()));
        }

        reader.join();
    }
    catch (exception const& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
