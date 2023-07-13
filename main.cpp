#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <map>
#include <vector>
#include <algorithm>
#include <sys/poll.h>

struct ServerConfig {
    std::string host;
    int port;
};

std::map<std::string, ServerConfig> parseConfiguration(const std::string& configFile) {
    std::map<std::string, ServerConfig> configMap;

    std::ifstream file(configFile);
    if (!file) {
        std::cerr << "Failed to open configuration file: " << configFile << std::endl;
        return configMap;
    }

    std::string line;
    std::string currentServer;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        if (line.substr(0, 7) == "server ") {
            currentServer = line.substr(7);
            configMap[currentServer] = ServerConfig();
        }
        else if (line.find("host") != std::string::npos) {
            size_t index = line.find("host") + 5;
            configMap[currentServer].host = line.substr(index);
            configMap[currentServer].host.erase(0, configMap[currentServer].host.find_first_not_of(" \t"));
            configMap[currentServer].host.erase(configMap[currentServer].host.find_last_not_of(" \r\n\t") + 1);
            std::cout << "host: " << configMap[currentServer].host << std::endl;
        }
        else if (line.find("port") != std::string::npos) {
            size_t index = line.find("port") + 5;
            configMap[currentServer].port = std::stoi(line.substr(index));
            std::cout << "port: " << configMap[currentServer].port << std::endl;
        }
    }

    file.close();
    return configMap;
}

int main(int argc, char** argv) {
    std::string configFile = "default.conf";
    if (argc > 1) {
        configFile = argv[1];
    }

    std::map<std::string, ServerConfig> configMap = parseConfiguration(configFile);

    std::vector<int> clientSockets;
    std::vector<pollfd> pollfds;

    std::map<std::string, ServerConfig>::iterator it;
    int listenSocket; // Declare listenSocket outside the loop
    for (it = configMap.begin(); it != configMap.end(); ++it) {
        const std::string& serverName = it->first;
        const ServerConfig& serverConfig = it->second;
        std::cout << "Starting server: " << serverName << " on " << serverConfig.host << ":" << serverConfig.port << std::endl;

        listenSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (listenSocket < 0) {
            std::cerr << "Failed to create a socket." << std::endl;
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in serverAddress;
        memset(&serverAddress, 0, sizeof(serverAddress));
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
        serverAddress.sin_port = htons(serverConfig.port);

        if (bind(listenSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
            std::cerr << "Failed to bind the socket to the address." << std::endl;
            close(listenSocket);
            exit(EXIT_FAILURE);
        }

        if (listen(listenSocket, SOMAXCONN) < 0) {
            std::cerr << "Failed to listen on the socket." << std::endl;
            close(listenSocket);
            exit(EXIT_FAILURE);
        }

        pollfd listenPollfd;
        listenPollfd.fd = listenSocket;
        listenPollfd.events = POLLIN;
        pollfds.push_back(listenPollfd);

        std::cout << "Server is listening on port " << serverConfig.port << std::endl;
    }

    while (true) {
        int ready = poll(&pollfds[0], pollfds.size(), -1);
        if (ready < 0) {
            std::cerr << "Failed to poll for events." << std::endl;
            exit(EXIT_FAILURE);
        }

        for (std::vector<pollfd>::iterator it = pollfds.begin(); it != pollfds.end(); ++it) {
            pollfd& pfd = *it;
            if (pfd.revents & POLLIN) {
                if (pfd.fd == listenSocket) {
                    struct sockaddr_in clientAddress;
                    socklen_t clientAddressLength = sizeof(clientAddress);
                    int clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddress, &clientAddressLength);
                    if (clientSocket < 0) {
                        std::cerr << "Failed to accept the new connection." << std::endl;
                        exit(EXIT_FAILURE);
                    }

                    pollfd clientPollfd;
                    clientPollfd.fd = clientSocket;
                    clientPollfd.events = POLLIN;
                    pollfds.push_back(clientPollfd);
                    clientSockets.push_back(clientSocket);

                    std::cout << "New client connected: " << inet_ntoa(clientAddress.sin_addr) << ":" << ntohs(clientAddress.sin_port) << std::endl;
                }
                else {
                    char buffer[1024];
                    int bytesRead = recv(pfd.fd, buffer, sizeof(buffer), 0);
                    if (bytesRead <= 0) {
                        if (bytesRead < 0)
                            std::cerr << "Failed to receive data on socket." << std::endl;
                        else
                            std::cout << "Client disconnected." << std::endl;

                        std::vector<int>::iterator clientSocketIter = std::find(clientSockets.begin(), clientSockets.end(), pfd.fd);
                        if (clientSocketIter != clientSockets.end()) {
                            clientSockets.erase(clientSocketIter);
                        }

                        close(pfd.fd);
                        it = pollfds.erase(it);
                        --it;
                    }
                    else {
                        std::cout << "Received " << bytesRead << " bytes from client." << std::endl;
                    }
                }
            }
        }
    }

    return 0;
}
