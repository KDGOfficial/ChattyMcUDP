#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstring>
#include <vector>
#include <mutex>

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

mutex cout_mutex;

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

int colors[] = { 4, 2, 6, 1, 5, 3, 7, 12, 10, 14, 9, 11, 13, 8, 15 };

void set_color(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, colors[color - 1]);
}

void reset_color() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, 7);
}

void receive_messages(int sock, int self_color) {
    char buffer[1024];
    sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);

    while (true) {
        int bytes_received = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&server_addr, &addr_len);
        if (bytes_received < 0) {
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }

        buffer[bytes_received] = '\0';
        string message = utf8_to_cp1251(string(buffer));

        lock_guard<mutex> lock(cout_mutex);
        set_color(7);
        cout << message << endl;
        reset_color();
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

    SOCKET client_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client_sock == INVALID_SOCKET) {
        cerr << "Ошибка создания сокета" << endl;
        return 1;
    }

    sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = INADDR_ANY;
    client_addr.sin_port = htons(0);

    if (bind(client_sock, (sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        cerr << "Ошибка привязки клиентского сокета" << endl;
        closesocket(client_sock);
        WSACleanup();
        return 1;
    }

    u_long mode = 1;
    ioctlsocket(client_sock, FIONBIO, &mode);

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(12345);

    string nickname;
    int color;
    {
        lock_guard<mutex> lock(cout_mutex);
        cout << "Введите ваш ник: ";
    }
    getline(cin, nickname);
    {
        lock_guard<mutex> lock(cout_mutex);
        do {
            cout << "Введите номер цвета (1-15): ";
            cin >> color;
        } while (color < 1 || color > 15);
        cin.ignore();
    }

    string init_msg = cp1251_to_utf8(nickname + ":" + to_string(color));
    sendto(client_sock, init_msg.c_str(), init_msg.length(), 0, (sockaddr*)&server_addr, sizeof(server_addr));

    thread receiver(receive_messages, client_sock, color);
    receiver.detach();

    string message;
    while (true) {
        getline(cin, message);
        if (message == "exit") {
            string exit_msg = cp1251_to_utf8("EXIT");
            sendto(client_sock, exit_msg.c_str(), exit_msg.length(), 0, (sockaddr*)&server_addr, sizeof(server_addr));
            break;
        }
        string utf8_message = cp1251_to_utf8(message);
        int sent_bytes = sendto(client_sock, utf8_message.c_str(), utf8_message.length(), 0, (sockaddr*)&server_addr, sizeof(server_addr));
        if (sent_bytes >= 0) {
            lock_guard<mutex> lock(cout_mutex);
            cout << "Ваше сообщение отправлено на сервер, ник пользователя: " << nickname << endl;
        }
    }

    closesocket(client_sock);
    WSACleanup();
    {
        lock_guard<mutex> lock(cout_mutex);
        cout << "Вы вышли из чата." << endl;
    }
    return 0;
}