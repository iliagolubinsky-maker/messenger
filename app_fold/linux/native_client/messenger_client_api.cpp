#include "client.hpp"
#include <stdexcept>
#include <iostream>

#ifdef _WIN32
  #define DLL_EXPORT __declspec(dllexport)
#else
  #define DLL_EXPORT
#endif

extern "C" {

struct ClientWrapper {
    MessengerClient* client;
};

DLL_EXPORT ClientWrapper* client_create(const char* from) {
    
    ClientWrapper* wrapper = new ClientWrapper;
    wrapper->client = new MessengerClient(from);
    return wrapper;
}

DLL_EXPORT void client_start(ClientWrapper* wrapper) {
    if (!wrapper || !wrapper->client) return;

    try {
        wrapper->client->start();
    }
    catch (const std::exception& e) {
        std::cerr << "[client_start] exception: " << e.what() << std::endl;
        std::terminate();
    }
    catch (...) {
        std::cerr << "[client_start] unknown exception" << std::endl;
        std::terminate();
    }
}

DLL_EXPORT void client_change_recipient(ClientWrapper* wrapper, const char* to){
    if (wrapper && wrapper->client) wrapper->client->change_recipient(to);
}

DLL_EXPORT void client_send(ClientWrapper* wrapper, const char* message) {
    if (wrapper && wrapper->client) wrapper->client->sendMessageToPeer(message);
}

DLL_EXPORT void client_stop(ClientWrapper* wrapper) {
    if (wrapper && wrapper->client) wrapper->client->stop();
}

DLL_EXPORT void client_destroy(ClientWrapper* wrapper) {
    if (!wrapper) return;
    if (wrapper->client) {
        wrapper->client->stop();
        delete wrapper->client;
    }
    delete wrapper;
}

DLL_EXPORT int client_pop_message(ClientWrapper* wrapper, char* from, int from_len, char* to, int to_len, char* text, int text_len){
    if (!wrapper) return 0;
    return wrapper->client->popMessage(from, from_len, to, to_len, text, text_len);
}

DLL_EXPORT void client_login(ClientWrapper* wrapper, const char* username, const char* password){
    if (!wrapper) return;
    return wrapper->client->login(username, password);
}

DLL_EXPORT bool client_get_log_status(ClientWrapper* wrapper){
    if (!wrapper) return false;
    return wrapper->client->getLogStatus();
}

DLL_EXPORT void client_register(ClientWrapper* wrapper, const char* username, const char* password){
    if (!wrapper) return;
    return wrapper->client->register_client(username, password);
}






}// extern "C"