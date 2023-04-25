//------------------------------------------------------------------------------
#include <winsock>
#include <iostream>
#include <vector>
#include <string>
//------------------------------------------------------------------------------
struct User
{
    std::string login;
    std::string password;
};
//------------------------------------------------------------------------------
struct Connection
{
    SOCKET s;
    char buffer[1024];
    int received;
    std::string login;
};
//------------------------------------------------------------------------------
std::vector<User> users;
std::vector<Connection> connections;
//------------------------------------------------------------------------------
void accept_if_needed(void)
{
    fd_set s_set;
    FD_ZERO(&s_set);
    FD_SET(s, &s_set);

    timeval timeout = {0, 0};
    int n = select(s + 1, &s_set, 0, 0, &timeout);
    if (!n || n == SOCKET_ERROR)
    {
        return;
    }

    SOCKADDR_IN nsa;
    int sizeof_nsa = sizeof(nsa);

    Connection c;
    c.s = accept(s, (SOCKADDR *)&nsa, &sizeof_nsa);
    if (c.s == INVALID_SOCKET)
    {
        return;
    }
    connections.push_back(c);
}
//------------------------------------------------------------------------------
bool read_string(std::string *str, char **data, int *data_size)
{
    if (*data_size < 1)  // no data
    {
        return false;
    }

    int str_size = 0;
    memcpy(&str_size, *data, 1);
    if (str_size > *data_size)  // overflow
    {
        return false;
    }
    *data++;
    *data_size--;

    str->resize(s_size);
    memcpy(&(*str)[0], *data, str_size);
    *data += str_size;
    *data_size -= str_size;
    return true;
}
//------------------------------------------------------------------------------
bool send_string(SOCKET s, const std::string &str)
{
    return
    (
        str.size() <= 0xFF &&  // overflow
        send(s, &str.size(), 1, 0) == 1 &&  // send error
        send(s, &str[0], str.size(), 0) == str.size()  // send error
    );
}
//------------------------------------------------------------------------------
User *find_user(const std::string &login)
{
    for (std::vector<User>::iterator i = users.begin(); i != users.end(); i++)
    {
        if (i->login == login)
        {
            return &*i;
        }
    }
    return 0;
}
//------------------------------------------------------------------------------
void recv_if_needed(void)
{
    fd_set s_set;
    FD_ZERO(&s_set);
    int max_s = 0;
    for (std::vector<Connection>::iterator i = connections.begin(); i != connections.end(); i++)
    {
        FD_SET(i->s, &s_set);
        if (max_s < i->s)
        {
            max_s = i->s;
        }
    }

    timeval timeout = {0, 0};
    int n = select(max_s + 1, &s_set, 0, 0, &timeout);
    if (!n || n == SOCKET_ERROR)
    {
        return;
    }

    for (std::vector<Connection>::iterator i = connections.begin(); i != connections.end();)
    {
        if (FD_ISSET(i->s, &s_set))
        {
            int package_size = recv
            (
                i->s,
                i->buffer + i->received,
                sizeof(i->buffer) - i->received,
                0
            );
            if (!package_size || package_size == SOCKET_ERROR)
            {
                i++;
                continue;
            }
            while (i->received > 2)
            {
                int data_size = 0;
                memcpy(&data_size, i->buffer, 2);

                if (data_size > sizeof(i->buffer))  // overflow
                {
                    closesocket(i->s);
                    i = connections.erase(i);
                    break;
                }

                if (data_size <= i->received)
                {
                    char *data = i->buffer;
                    std::string str1, str2;
                    bool res =
                    (
                        read_string(&str1, &data, &data_size) &&
                        read_string(&str2, &data, &data_size)
                    );
                    if (!res)  // bad data
                    {
                        closesocket(i->s);
                        i = connections.erase(i);
                        break;
                    }

                    if (i->login.empty())
                    {
                        // registration or sign in: str1 - login, str2 - password
                        User *user = find_user(str1);
                        if (!user)  // registration
                        {
                            User new_user;
                            new_user.login = str1;
                            new_user.password = str2;
                            users.push_back(new_user);
                        }
                        else if (user->password != str2)  // sign in
                        {
                            closesocket(i->s);
                            i = connections.erase(i);
                            break;
                        }
                        i->login = str1;
                        continue;
                    }

                    // message: str1 - receiver, str2 - text
                    bool erased = false;
                    for (std::vector<Connection>::iterator i2 = connections.begin(); i2 != connections.end();)
                    {
                        if (i2->login == str1)
                        {
                            res =
                            (
                                send_str(i2->s, i->login) &&
                                send_str(i2->s, str2)
                            );
                            if (!res)  // send error
                            {
                                closesocket(i2->s);
                                i2 = connections.erase(i2);
                                if (i->s == i2->s)
                                {
                                    erased = true;
                                    i = i2;
                                }
                                continue;
                            }
                        }
                        i2++;
                    }
                    if (erased)
                    {
                        break;
                    }

                    i->received -= data_size;
                    if (i->received)
                    {
                        char tmp[sizeof(i->buffer)];
                        memcpy(tmp, i->buffer + data_size, i->received);
                        memcpy(i->buffer, tmp, i->received);
                    }
                }
            }
            i++;
        }
    }
}
//------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    int port;

    std::cout << "port>";
    std::cin >> port;

    WSADATA wsa_data;
    if (WSAStartup(0x101, &wsa_data) || wsa_data.wVersion != 0x101)
    {
        std::cout << "WSAStartup error" << std::endl;
        system("pause");
        return -1;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        std::cout << "socket error" << std::endl;
        system("pause");
        return -1;
    }

    SOCKADDR_IN sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = INADDR_ANY;

    if (bind(s, (SOCKADDR *)&sa, sizeof(sa)) == SOCKET_ERROR)
    {
        std::cout << "bind error" << std::endl;
        system("pause");
        return -1;
    }

    if (listen(s, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cout << "listen error" << std::endl;
        system("pause");
        return -1;
    }

    while (true)
    {
        accept_if_needed();
        recv_if_needed();
    }

    closesocket(s);

    WSACleanup();

    system("pause");
    return 0;
}
