#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <chrono>
#include <ctime>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#define socklen_t int
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

using namespace std;

// Структура для зберігання повідомлення в таблиці
struct Message {
    string nickname;
    string text;
    string timestamp;
};

// Функції для кодування
string cp1251_to_utf8(const string& cp1251_str) {
    int len = MultiByteToWideChar(1251, 0, cp1251_str.c_str(), -1, nullptr, 0);
    vector<wchar_t> wide(len);
    MultiByteToWideChar(1251, 0, cp1251_str.c_str(), -1, wide.data(), len);
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), -1, nullptr, 0, nullptr, nullptr);
    vector<char> utf8(utf8_len);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), -1, utf8.data(), utf8_len, nullptr, nullptr);
    return string(utf8.data());
}

string utf8_to_cp1251(const string& utf8_str) {
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, nullptr, 0);
    vector<wchar_t> wide(len);
    MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, wide.data(), len);
    int cp1251_len = WideCharToMultiByte(1251, 0, wide.data(), -1, nullptr, 0, nullptr, nullptr);
    vector<char> cp1251(cp1251_len);
    WideCharToMultiByte(1251, 0, wide.data(), -1, cp1251.data(), cp1251_len, nullptr, nullptr);
    return string(cp1251.data());
}

struct Client {
    string nickname;
    int color;
    Client() : color(7) {}
    Client(string nick, int c) : nickname(nick), color(c) {}
};

vector<Message> message_table; // Таблиця повідомлень
map<string, Client> clients;   // ключ: "ip:port"

string get_timestamp() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    char buf[80];
    strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
    return string(buf);
}

void broadcast(int sock, const string& message, const sockaddr_in* exclude_addr = nullptr) {
    for (const auto& client_pair : clients) {
        const string& addr_str = client_pair.first;
        sockaddr_in client_addr;
        memset(&client_addr, 0, sizeof(client_addr));
        client_addr.sin_family = AF_INET;
        string ip = addr_str.substr(0, addr_str.find(':'));
        string port_str = addr_str.substr(addr_str.find(':') + 1);
        int port = stoi(port_str);
        inet_pton(AF_INET, ip.c_str(), &client_addr.sin_addr);
        client_addr.sin_port = htons(static_cast<unsigned short>(port));

        if (!exclude_addr || memcmp(&client_addr, exclude_addr, sizeof(sockaddr_in)) != 0) {
            string utf8_message = cp1251_to_utf8(message);
            sendto(sock, utf8_message.c_str(), utf8_message.length(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
        }
    }
}

void send_table(int sock, const sockaddr_in& client_addr) {
    for (const auto& msg : message_table) {
        string table_entry = "[" + msg.timestamp + "] " + msg.nickname + ": " + msg.text;
        string utf8_entry = cp1251_to_utf8(table_entry);
        sendto(sock, utf8_entry.c_str(), utf8_entry.length(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
    }
}

void handle_clients(int sock) {
    char buffer[1024];
    sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (true) {
        int bytes_received = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&client_addr, &addr_len);
        if (bytes_received < 0) {
            cerr << "Ошибка получения данных: " << WSAGetLastError() << endl;
            continue;
        }

        buffer[bytes_received] = '\0';
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        string client_key = string(ip_str) + ":" + to_string(ntohs(client_addr.sin_port));

        string message = utf8_to_cp1251(string(buffer));

        if (clients.find(client_key) == clients.end()) {
            // Новий клієнт: "нік:колір"
            size_t colon_pos = message.find(':');
            string nickname = message.substr(0, colon_pos);
            int color = stoi(message.substr(colon_pos + 1));
            if (color < 1 || color > 15) color = 7;
            clients[client_key] = Client(nickname, color);

            string join_msg = "[Сервер] " + nickname + " вошел в чат";
            message_table.push_back({ "Сервер", join_msg, get_timestamp() });
            cout << join_msg << endl;
            broadcast(sock, join_msg, &client_addr);

            // Відправка всієї таблиці новому клієнту
            send_table(sock, client_addr);
        }
        else {
            string nickname = clients[client_key].nickname;
            if (message == "EXIT") {
                string exit_msg = "[Сервер] " + nickname + " вышел из чата";
                message_table.push_back({ "Сервер", exit_msg, get_timestamp() });
                cout << exit_msg << endl;
                broadcast(sock, exit_msg);
                clients.erase(client_key);
            }
            else {
                string formatted_msg = "[" + get_timestamp() + "] " + nickname + ": " + message;
                message_table.push_back({ nickname, message, get_timestamp() });
                cout << formatted_msg << endl;
                broadcast(sock, formatted_msg, &client_addr);
            }
        }
    }
}

int main() {
    SetConsoleOutputCP(1251);
    SetConsoleCP(1251);
    setlocale(LC_ALL, "Russian");
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "Ошибка инициализации Winsock" << endl;
        return 1;
    }

    SOCKET server_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_sock == INVALID_SOCKET) {
        cerr << "Ошибка создания сокета" << endl;
        return 1;
    }

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(12345);

    if (bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Ошибка привязки сокета" << endl;
        return 1;
    }

    cout << "Сервер запущен на порту 12345..." << endl;
    handle_clients(server_sock);

    closesocket(server_sock);
    WSACleanup();
    return 0;
}