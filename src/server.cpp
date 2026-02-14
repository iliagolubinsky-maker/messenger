#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <boost/beast/ssl.hpp>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <sodium.h>
#include <chrono>
#include <queue>


using namespace std;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

class Wsession : public std::enable_shared_from_this<Wsession> {
public:
    Wsession(tcp::socket socket,
             std::unordered_map<std::string, std::shared_ptr<Wsession>>& map,
             sqlite3* database,
             sqlite3* password_db,
             sqlite3* reset_db,
             sqlite3* unsent_messages,
             sqlite3* chat_history,
             std::mutex& mtx,
             ssl::context& ctx)
        : ws_(std::move(socket), ctx),
          clients_(map),
          map_mtx_(mtx),
          db(database),
          pd(password_db),
          pr(reset_db),
          unsent_messages_db(unsent_messages),
          chat_history(chat_history)
    {}

    void start() {
        auto self = shared_from_this();
        ws_.next_layer().async_handshake(
            ssl::stream_base::server,
            [self](beast::error_code ec) {
                self->onTLSHandshake(ec);
            }
        );
    }

private:
    websocket::stream<ssl::stream<tcp::socket>> ws_;
    std::unordered_map<std::string, std::shared_ptr<Wsession>>& clients_;
    std::mutex& map_mtx_;
    std::string username_;
    boost::beast::flat_buffer buffer_;
    sqlite3* db;
    sqlite3* pd;
    sqlite3* pr;
    sqlite3* unsent_messages_db;
    sqlite3* chat_history;
    bool access = false;
    queue<string> write_queue_;
    bool is_writing_ = false;

    void onTLSHandshake(beast::error_code ec) {
        if(ec) {
            cerr << "TLS handshake error: " << ec.message() << endl;
            return;
        }
        ws_.async_accept([self = shared_from_this()](beast::error_code ec){
            self->onAccept(ec);
        });
    }

    void onAccept(beast::error_code ec){
        if(ec) {
            cerr << "Accept error: " << ec.message() << endl;
            return;
        }
        
        doRead();
    }

    void doRead() {
        auto self = shared_from_this();
        ws_.async_read(buffer_, [self](beast::error_code ec, size_t){
            if(ec) {
                self->disconnect();
                return;
            }
            self->processBuffer();
        });
    }

    void processBuffer() {
        auto data = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());
        
        try {
            json j = json::parse(data);
            if(username_.empty() && j.contains("pubkey") && j.contains("from")) {
                
                username_ = j["from"];
                string pubkey = j["pubkey"];
                {
                    lock_guard<std::mutex> lock(map_mtx_);
                    clients_[username_] = shared_from_this();
                }
                cout << "User " << username_ << " connected.\n";
                cout << "Public key " << pubkey << endl;
                addOrUpdateSession(username_, pubkey);
                const char* sql = "SELECT message FROM unsent_messages WHERE session_id = ? ORDER BY timestamp;";
                sqlite3_stmt* stmt = nullptr;

                if (sqlite3_prepare_v2(unsent_messages_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(stmt, 1, username_.c_str(), -1, SQLITE_STATIC);

                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        const char* msg = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                        if (msg) sendMessage(msg);
                    }

                    sqlite3_finalize(stmt);
                } else {
                    std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(unsent_messages_db) << std::endl;
                }

            
            }
            else if (j.contains("connect") && username_.empty()){
                username_ = j["connect"];

                
                {
                    lock_guard<std::mutex> lock(map_mtx_);
                    clients_[username_] = shared_from_this();
                }
                sendChatHistory(this->username_);
                cout << "User " << username_ << " connected.\n";

            }
            else if(j.contains("to") && j.contains("cipher") && j.contains("from") && j.contains("timestamp")) {
                storeMessages(j["from"], j["to"], j["cipher"], j["timestamp"]);
                string to = j["to"];
                if (sessionExists(to) == true) {
                    shared_ptr<Wsession> peer;
                    {
                        lock_guard<std::mutex> lock(map_mtx_);
                        auto it = clients_.find(to);
                        if(it != clients_.end()) peer = it->second;
                    }
                    if(peer) {
                        peer->sendMessage(data);
                    } else {
                        storeUnsentMessages(to, data, j["timestamp"]);
                    }
                }
                
            } else if (j.contains("type") && j["type"] == "receive_keys" && j.contains("to")) {
                if (sessionExists(j["to"]) == true) {
                    sendAvailableKeys(j["to"]);
                    
                } else {
                    cerr << "Session for " << j["to"] << " does not exist. Cannot send keys.\n";
                }
                
            } else if (j.contains("req") && j["req"] == "login"){
                json p;
                p["req"] = "login";
                if (!login(j["client"], j["password"])){
                    p["status"] = "failed";
                    p["message"] = "Incorrect login or password, would you like to create an account?";
                } else {
                    p["status"] = "approved";
                    access = true;
                
                    
                }
                sendMessage(p.dump());
                
            } else if (j.contains("req") && j["req"] == "register"){
                json p;
                p["req"] = "register";
                if (create_user(j["client"], j["password"])){
                    p["status"] = "approved";
                    access = true;
                
                
                } else {
                    p["status"] = "failed";
                    p["message"] = "Failed to add a new user";
                }
                sendMessage(p.dump());
            }

        } catch(...) {
            cerr << "Invalid JSON received from " << username_ << endl;
        }
        doRead();
    }


    void sendAvailableKeys(const string& to) {

        string peer_pubkey = get_peer_pubkey(to);
        

        if(peer_pubkey.empty()) return;

        json j;
        j["type"] = "key";
        j["from"] = to;
        j["pubkey"] = peer_pubkey;
        std::cout<< "Sending public key of " << to << std::endl;
        sendMessage(j.dump());

        shared_ptr<Wsession> peer;
        {
            lock_guard<std::mutex> lock(map_mtx_);
            auto it = clients_.find(to);
            if(it != clients_.end()) peer = it->second;
            else return;
        }

        string my_pubkey = get_peer_pubkey(username_);

        if(!my_pubkey.empty()){
            json j2;
            j2["type"] = "key";
            j2["from"] = username_;
            j2["pubkey"] = my_pubkey;
            std::cout<< "Sending public key to " << to << std::endl;
            peer->sendMessage(j2.dump());
        }
    }



    void disconnect() {
        ws_.async_close(websocket::close_code::normal, [](beast::error_code){});
        if(!username_.empty()) {
            lock_guard<std::mutex> lock(map_mtx_);
            clients_.erase(username_);
            cout << "User " << username_ << " disconnected.\n";
        }
    }

    void storeUnsentMessages(const std::string& session_id, const std::string& message, long long timestamp) {
        const char* sql = "INSERT INTO unsent_messages (session_id, message, timestamp) VALUES (?, ?, ?);";
        sqlite3_stmt* stmt;
        if(sqlite3_prepare_v2(unsent_messages_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, message.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, timestamp);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    void sendChatHistory(const std::string& user) {
        const char* key_sql = "SELECT DISTINCT sender FROM chat_history WHERE receiver = ? "
                            "UNION SELECT DISTINCT receiver FROM chat_history WHERE sender = ?;";
        sqlite3_stmt* key_stmt;
        if (sqlite3_prepare_v2(chat_history, key_sql, -1, &key_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(key_stmt, 1, user.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(key_stmt, 2, user.c_str(), -1, SQLITE_STATIC);
            while (sqlite3_step(key_stmt) == SQLITE_ROW) {
                const char* peer = reinterpret_cast<const char*>(sqlite3_column_text(key_stmt, 0));
                if (peer && std::string(peer) != user) {
                    sendAvailableKeys(peer);
                }
            }
            sqlite3_finalize(key_stmt);
        }

        const char* msg_sql = "SELECT sender, receiver, cipher, timestamp FROM chat_history "
                            "WHERE sender = ? OR receiver = ? ORDER BY timestamp ASC;";
        sqlite3_stmt* msg_stmt;
        if (sqlite3_prepare_v2(chat_history, msg_sql, -1, &msg_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(msg_stmt, 1, user.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(msg_stmt, 2, user.c_str(), -1, SQLITE_STATIC);
            while (sqlite3_step(msg_stmt) == SQLITE_ROW) {
                json j;
                j["type"] = "history";
                j["from"] = reinterpret_cast<const char*>(sqlite3_column_text(msg_stmt, 0));
                j["to"] = reinterpret_cast<const char*>(sqlite3_column_text(msg_stmt, 1));
                j["cipher"] = reinterpret_cast<const char*>(sqlite3_column_text(msg_stmt, 2));
                j["timestamp"] = sqlite3_column_int64(msg_stmt, 3);
                sendMessage(j.dump());
            }
            sqlite3_finalize(msg_stmt);
        }
    }

    void storeMessages(const string& from, const string& to, const string& cipher, long long timestamp){
            
        const char* sql = "INSERT INTO chat_history (sender, receiver, cipher, timestamp) VALUES (?, ?, ?, ?);";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(chat_history, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

        sqlite3_bind_text(stmt, 1, from.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, to.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, cipher.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 4, timestamp);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

    }




    

public:

    string get_peer_pubkey(const string& to){
        string peer_pubkey;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT pubkey FROM sessions WHERE session_id = ?;";
        if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_text(stmt, 1, to.c_str(), -1, SQLITE_STATIC);
            if(sqlite3_step(stmt) == SQLITE_ROW){
                const unsigned char* text = sqlite3_column_text(stmt, 0);
                if(text) peer_pubkey = reinterpret_cast<const char*>(text);
            }
        }
        sqlite3_finalize(stmt);
        return peer_pubkey;
    }

    void addOrUpdateSession(const std::string& session_id, const std::string& pubkey) {
        const char* sql = "INSERT INTO sessions (session_id, pubkey) VALUES (?, ?) "
                          "ON CONFLICT(session_id) DO UPDATE SET pubkey = excluded.pubkey;";
        sqlite3_stmt* stmt;
        if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pubkey.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    bool sessionExists(const std::string& session_id) {
        const char* sql = "SELECT 1 FROM sessions WHERE session_id = ?;";
        sqlite3_stmt* stmt;
        if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
        bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        return exists;
    }

    void removeSession(const std::string& session_id) {
        const char* sql = "DELETE FROM sessions WHERE session_id = ?;";
        sqlite3_stmt* stmt;
        if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    void sendMessage(const std::string& msg) {
        auto self = shared_from_this();
        asio::post(ws_.get_executor(), [self, msg]() {
            self->write_queue_.push(msg);
            if (!self->is_writing_) {
                self->doWrite();
            }
        });
    }

    void doWrite() {
        if (write_queue_.empty()) return;

        is_writing_ = true;
        auto msg = write_queue_.front();
        write_queue_.pop();

        auto self = shared_from_this();
        ws_.text(true);
        ws_.async_write(asio::buffer(msg), [self](beast::error_code ec, size_t) {
            if (ec) {
                self->disconnect();
                return;
            }
            self->is_writing_ = false;
            self->doWrite();
        });
    }

    bool create_user(const std::string& username, const std::string& password) {
        char hash[crypto_pwhash_STRBYTES];
        if (crypto_pwhash_str(hash, password.c_str(), password.size(),
                              crypto_pwhash_OPSLIMIT_INTERACTIVE,
                              crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
            return false;
        }
        const char* sql = "INSERT INTO users (username, password_hash, created_at) VALUES (?, ?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(pd, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }
    
    bool login(const std::string& username, const std::string& password) {
        const char* sql = "SELECT id, password_hash, failed_attempts, locked_until FROM users WHERE username = ?;";
        sqlite3_stmt* stmt = nullptr;

        // Prepare the SQL statement
        if (sqlite3_prepare_v2(pd, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        // Bind the username parameter
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

        bool success = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            // Extract data from the row
            int user_id = sqlite3_column_int(stmt, 0);
            
            // CRITICAL FIX: Copy the hash out of SQLite memory into a local string
            const unsigned char* raw_hash = sqlite3_column_text(stmt, 1);
            std::string stored_hash = raw_hash ? reinterpret_cast<const char*>(raw_hash) : "";
            
            int failed_attempts = sqlite3_column_int(stmt, 2);
            long long locked_until = sqlite3_column_int64(stmt, 3);

            // Finalize the statement NOW; our copy in 'stored_hash' remains safe
            sqlite3_finalize(stmt);

            // Security checks: account locking and empty hashes
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            if (locked_until > now || stored_hash.empty()) {
                return false;
            }

            // Verify the password using the local copy of the hash
            if (crypto_pwhash_str_verify(stored_hash.c_str(), password.c_str(), password.size()) == 0) {
                success = true;
                
                // Reset failed attempts on success
                const char* upd = "UPDATE users SET failed_attempts = 0, last_login = ? WHERE id = ?;";
                sqlite3_stmt* upd_stmt = nullptr;
                if (sqlite3_prepare_v2(pd, upd, -1, &upd_stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(upd_stmt, 1, now);
                    sqlite3_bind_int(upd_stmt, 2, user_id);
                    sqlite3_step(upd_stmt);
                    sqlite3_finalize(upd_stmt);
                }
            } else {
                // Increment failed attempts and handle lockout
                failed_attempts += 1;
                long long new_locked_until = 0;
                if (failed_attempts >= 5) {
                    new_locked_until = now + 300; // Lock for 5 minutes
                }
                
                const char* fail_upd = "UPDATE users SET failed_attempts = ?, locked_until = ? WHERE id = ?;";
                sqlite3_stmt* fail_stmt = nullptr;
                if (sqlite3_prepare_v2(pd, fail_upd, -1, &fail_stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(fail_stmt, 1, failed_attempts);
                    sqlite3_bind_int64(fail_stmt, 2, new_locked_until);
                    sqlite3_bind_int(fail_stmt, 3, user_id);
                    sqlite3_step(fail_stmt);
                    sqlite3_finalize(fail_stmt);
                }
            }
        } else {
            // No user found with that name
            sqlite3_finalize(stmt);
        }
        
        return success;
    }


};

class WServer {
public:
    WServer(asio::io_context& ioc, tcp::endpoint endpoint, sqlite3* database, sqlite3* password_db, sqlite3* reset_db, sqlite3* unsent_messages, sqlite3* chat_history)
        : acceptor_(ioc, endpoint), db(database), pd(password_db), pr(reset_db), unsent_messages_db(unsent_messages), chat_history(chat_history)
    {
        ctx_.use_certificate_chain_file("server.crt");
        ctx_.use_private_key_file("server.key", ssl::context::pem);
        doAccept();
    }

private:
    tcp::acceptor acceptor_;
    ssl::context ctx_{ssl::context::tlsv12_server};
    std::unordered_map<std::string, std::shared_ptr<Wsession>> clients_;
    std::mutex clients_mtx_;
    sqlite3* db;
    sqlite3* pd;
    sqlite3* pr;
    sqlite3* unsent_messages_db;
    sqlite3* chat_history;

    void doAccept() {
        acceptor_.async_accept([this](beast::error_code ec, tcp::socket socket){
            if(!ec) {
                std::make_shared<Wsession>(std::move(socket), clients_, db, pd, pr, unsent_messages_db, chat_history, clients_mtx_, ctx_)->start();
            }
            doAccept();
        });
    }
};

int main() {
    try {
        sqlite3* db;
        sqlite3* pd;
        sqlite3* pr;
        sqlite3* unsent_messages;
        sqlite3* chat_history;
        int rc;
        int ld;
        int pdrc;
        int prc;
        int ch;
        rc = sqlite3_open("messenger.db", &db);
        ld = sqlite3_open("login.db", &pd);
        pdrc = sqlite3_open("password_reset.db", &pr);
        prc = sqlite3_open("unsent_messages.db", &unsent_messages);
        ch = sqlite3_open("chat_history.db", &chat_history);
        if(rc || ld || pdrc || prc || ch) {
            cerr << "Can't open database: " << sqlite3_errmsg(db) << endl;
            return 1;
        } else {
            cout << "Opened database successfully\n";
        }
        const char *sql_sessions = "CREATE TABLE IF NOT EXISTS sessions (session_id TEXT PRIMARY KEY, pubkey TEXT NOT NULL)";
        const char *sql_create = "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT UNIQUE NOT NULL, password_hash TEXT NOT NULL, created_at INTEGER NOT NULL, last_login INTEGER, failed_attempts INTEGER DEFAULT 0, locked_until INTEGER DEFAULT 0)";
        const char *pass_reset = "CREATE TABLE IF NOT EXISTS password_token (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL,token_hash TEXT NOT NULL,  expires_at INTEGER NOT NULL, used INTEGER DEFAULT 0, FOREIGN KEY(user_id) REFERENCES users(id))";
        const char* msg_sess = "CREATE TABLE IF NOT EXISTS unsent_messages ( id INTEGER PRIMARY KEY AUTOINCREMENT, session_id TEXT NOT NULL, message TEXT NOT NULL, timestamp INTEGER NOT NULL)";
        const char* cht_hstr = "CREATE TABLE IF NOT EXISTS chat_history ( id INTEGER PRIMARY KEY AUTOINCREMENT, sender TEXT NOT NULL, receiver TEXT NOT NULL, cipher TEXT NOT NULL, timestamp INTEGER NOT NULL);";
        char *errMsg = nullptr;
        rc = sqlite3_exec(db, sql_sessions, 0, 0, &errMsg);
        ld = sqlite3_exec(pd, sql_create, 0, 0, &errMsg);
        pdrc = sqlite3_exec(pr, pass_reset, 0, 0, &errMsg);
        prc = sqlite3_exec(unsent_messages, msg_sess, 0, 0, &errMsg);
        ch = sqlite3_exec(chat_history, cht_hstr, 0, 0, &errMsg);
        if (rc != SQLITE_OK) {
            std::cerr << "SQL error: " << errMsg << "\n";
            sqlite3_free(errMsg);
        } else {
            std::cout << "Table created successfully!\n";
        }
        asio::io_context ioc;
        WServer server(ioc, tcp::endpoint(tcp::v4(), 8080), db, pd, pr, unsent_messages, chat_history);
        ioc.run();
        sqlite3_close(db);
        sqlite3_close(pd);
        sqlite3_close(pr);
        sqlite3_close(unsent_messages);
        sqlite3_close(chat_history);
    } catch(const std::exception& e) {
        cerr << "Error: " << e.what() << endl;
    }
}
