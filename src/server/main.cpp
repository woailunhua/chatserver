#include "chatserver.hpp"
#include "chatservice.hpp"
#include <iostream>
#include <signal.h>
#include <iostream>

using namespace std;

// 处理服务器ctrl+c结束后，重置user的状态信息
void resetHandler(int)
{
    ChatService::instance()->reset();
    exit(0);
}

int main(int argc, char **argv)
{
    // if (argc < 3)
    // {
    //     cerr << "command invalid! example: ./ChatServer 127.0.0.1 6000" << endl;
    //     exit(-1);
    // }

    // 解析通过命令行参数传递的ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);
    // char *ip = "127.0.0.1";
    // uint16_t port = 6000;
    cout << port << endl;
    /*
        - 不是程序员调用，而是当信号产生，由内核调用
        - 返回值:    成功，返回上一次注册的信号处理函数的地址。第一次返回NULL
                    失败,返回SIG_ERR,设置错误号
    */
    signal(SIGINT, resetHandler); 

    EventLoop loop;
    InetAddress addr(ip, port);
    ChatServer server(&loop, addr, "ChatServer");

    server.start();
    loop.loop();

    return 0;
}