#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <vector>
#include <iostream>
using namespace std;
using namespace muduo;

// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 注册消息以及对应的Handler回调操作
ChatService::ChatService()
{
    // 用户基本业务管理相关事件处理回调注册
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    // 群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    // 连接redis服务器
    if (_redis.connect())
    {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}

// 服务器异常，业务重置方法
void ChatService::reset()
{
    // 把online状态的用户，设置成offline
    _userModel.resetState();
}

// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid) // 这里就是上面reg_msg这些。根据这些去处理。
{
    // 记录错误日志，msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end())
    {
        // 返回一个默认的处理器，空操作
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp) {
            LOG_ERROR << "msgid:" << msgid << " can not find handler!";
        };
    }
    else
    {
        return _msgHandlerMap[msgid];
    }
}
// {"msgid":1,"id":1,"password":"123456"}
// 处理登录业务  id  pwd   pwd
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    // LOG_INFO << "do login service!";
    int id = js["id"].get<int>();
    string pwd = js["password"];
    User user = _userModel.query(id); 
    if(user.getId() == id && user.getPwd() == pwd) { // 没查到的话id是-1。
        if(user.getState() == "online")
        {
            // 该用户已经登录，不允许重复登陆。
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "this account is using, input another!"; // 就是说已经登陆了。
            conn->send(response.dump());
        }
        else
        {
            // 登陆成功，记录用户连接信息
            {
                 lock_guard<mutex> lock(_connMutex); // C++STL容器里面根本没考虑过多线程问题 所以这部分需要我们去处理。 往map插入链接这个操作
                _userConnMap.insert({id, conn});
            }
           
            // id用户登录成功后，向redis订阅channel(id) 以id命名的通道
            _redis.subscribe(id); 


            // 登陆成功， 更新用户状态信息 state offline=>online
            user.setState("online"); // 由于登陆上了所以更改用户状态。这里只是代码里面设置了状态，但是
            _userModel.updateState(user); // 再

            json response;
            response["msgid"] = LOGIN_MSG_ACK; // 注册消息的确认
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();

            // 查询该用户是否有离线消息 一个用户上来先查有没有别人发的
            vector<string> vec = _offlineMsgModel.query(id);
            if(!vec.empty())
            {
                response["offlinemsg"] = vec; 
                // 读取该用户的离线消息后，把该用户的所有离线消息删除掉
                _offlineMsgModel.remove(id);
            }

            // 查询该用户的好友信息并返回
            vector<User> userVec = _friendModel.query(id);
            if(!userVec.empty())
            {
                vector<string> vec2;
                for(User &user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            }

            // 查询用户的群组信息 
            vector<Group> groupuserVec = _groupModel.queryGroups(id);
            if (!groupuserVec.empty())
            {
                // group:[{groupid:[xxx, xxx, xxx, xxx]}]

                /*
                {
                    groups:
                    {
                            id :    ,
                            groupname :     ,
                            groupdesc :     ,
                            users :
                            [
                                {
                                    id: ,
                                    name: , 
                                    state: ,
                                    role:},
                                {   id: ,
                                    name: , 
                                    state: ,
                                    role:}
                            ]
                                    
                    }
                            
                }
                */
                vector<string> groupV;
                for (Group &group : groupuserVec)
                {
                    json grpjson;
                    grpjson["id"] = group.getId();
                    grpjson["groupname"] = group.getName();
                    grpjson["groupdesc"] = group.getDesc();
                    vector<string> userV;
                    for (GroupUser &user : group.getUsers())
                    {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        userV.push_back(js.dump());
                    }
                    grpjson["users"] = userV;
                    groupV.push_back(grpjson.dump());
                }

                response["groups"] = groupV;  // 发送groups下，是一个一个users 每个user存的是这个用户的ID name和状态以及在这个群里面的身份。
            }
            

            conn->send(response.dump()); // 发送给当前的终端
        }
    }
    else
    {
        // 登陆失败
        json response;
        response["msgid"] = LOGIN_MSG_ACK; // 注册消息的确认
        response["errno"] = 1;
        response["errmsg"] = "id or password is invalid!";
        conn->send(response.dump());
    }


    // User user = _userModel.query(id);
    // if (user.getId() == id && user.getPwd() == pwd)
    // {
    //     if (user.getState() == "online")
    //     {
    //         // 该用户已经登录，不允许重复登录
    //         json response;
    //         response["msgid"] = LOGIN_MSG_ACK;
    //         response["errno"] = 2;
    //         response["errmsg"] = "this account is using, input another!";
    //         conn->send(response.dump());
    //     }
    //     else
    //     {
    //         // 登录成功，记录用户连接信息
    //         {
    //             lock_guard<mutex> lock(_connMutex);
    //             _userConnMap.insert({id, conn});
    //         }

    //         // id用户登录成功后，向redis订阅channel(id)
    //         _redis.subscribe(id); 

    //         // 登录成功，更新用户状态信息 state offline=>online
    //         user.setState("online");
    //         _userModel.updateState(user);

    //         json response;
    //         response["msgid"] = LOGIN_MSG_ACK;
    //         response["errno"] = 0;
    //         response["id"] = user.getId();
    //         response["name"] = user.getName();
    //         // 查询该用户是否有离线消息
    //         vector<string> vec = _offlineMsgModel.query(id);
    //         if (!vec.empty())
    //         {
    //             response["offlinemsg"] = vec;
    //             // 读取该用户的离线消息后，把该用户的所有离线消息删除掉
    //             _offlineMsgModel.remove(id);
    //         }

    //         // 查询该用户的好友信息并返回
    //         vector<User> userVec = _friendModel.query(id);
    //         if (!userVec.empty())
    //         {
    //             vector<string> vec2;
    //             for (User &user : userVec)
    //             {
    //                 json js;
    //                 js["id"] = user.getId();
    //                 js["name"] = user.getName();
    //                 js["state"] = user.getState();
    //                 vec2.push_back(js.dump());
    //             }
    //             response["friends"] = vec2;
    //         }

    //         // 查询用户的群组信息
    //         vector<Group> groupuserVec = _groupModel.queryGroups(id);
    //         if (!groupuserVec.empty())
    //         {
    //             // group:[{groupid:[xxx, xxx, xxx, xxx]}]
    //             vector<string> groupV;
    //             for (Group &group : groupuserVec)
    //             {
    //                 json grpjson;
    //                 grpjson["id"] = group.getId();
    //                 grpjson["groupname"] = group.getName();
    //                 grpjson["groupdesc"] = group.getDesc();
    //                 vector<string> userV;
    //                 for (GroupUser &user : group.getUsers())
    //                 {
    //                     json js;
    //                     js["id"] = user.getId();
    //                     js["name"] = user.getName();
    //                     js["state"] = user.getState();
    //                     js["role"] = user.getRole();
    //                     userV.push_back(js.dump());
    //                 }
    //                 grpjson["users"] = userV;
    //                 groupV.push_back(grpjson.dump());
    //             }

    //             response["groups"] = groupV;
    //         }

    //         conn->send(response.dump());
    //     }
    // }
    // else
    // {
    //     // 该用户不存在，用户存在但是密码错误，登录失败
    //     json response;
    //     response["msgid"] = LOGIN_MSG_ACK;
    //     response["errno"] = 1;
    //     response["errmsg"] = "id or password is invalid!";
    //     conn->send(response.dump());
    // }
}

// 处理注册业务  name  password  传进来的是反序列化后的数据
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if (state)
    {
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK; // 注册消息的确认
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
    }
    else
    {
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
    }
}

// 处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid); 

    // 更新用户的状态信息
    User user(userid, "", "", "offline");
    _userModel.updateState(user);
}

// 处理客户端异常退出  现在的问题是登陆了 然后退出 用户的状态已经被改为online，那么下次登陆就登不上去。因为状态变了。
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;
    { // 这个是给了缩小锁的范围
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                // 从map表删除用户的链接信息
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(user.getId()); 
 
    // 更新用户的状态信息   这个!=-1是上面的连接map里面没找到那么就没有必须设置
    if (user.getId() != -1)
    {
        user.setState("offline");
        _userModel.updateState(user);
    }
}

// 一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int toid = js["toid"].get<int>(); // 这是要写给哪个ID 也就是对方ID。

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            // toid在线，转发消息   服务器主动推送消息给toid用户
            it->second->send(js.dump());
            return;
        }
        else
        {
            cout << "buzaixian " << endl;
        }
    }

    // 查询toid是否在线   可能虽然不在我这太服务器上，online的话一定是在别的服务器上。
    User user = _userModel.query(toid);
    if (user.getState() == "online")
    {
        _redis.publish(toid, js.dump());
        return;
    }

    // toid不在线，存储离线消息
    _offlineMsgModel.insert(toid, js.dump());
}

// 添加好友业务 msgid id friendid
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>(); // 必须和数据库id一样
    int friendid = js["friendid"].get<int>();

    // 存储好友信息
    _friendModel.insert(userid, friendid);
}

// 创建群组业务
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    // 存储新创建的群组信息
    Group group(-1, name, desc);
    if (_groupModel.createGroup(group))
    {
        // 存储群组创建人信息
        _groupModel.addGroup(userid, group.getId(), "creator"); // 创建群组 并且提供谁创建的群组 GroupUser
    }
}

// 加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    _groupModel.addGroup(userid, groupid, "normal");
}

// 群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);

    lock_guard<mutex> lock(_connMutex); 
    for (int id : useridVec) // 给groupid这个群里除了自己id外的其它他用户都要发消息
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
            // 转发群消息
            it->second->send(js.dump()); 
        }
        else
        {
            // 查询toid是否在线 
            User user = _userModel.query(id);  
            if (user.getState() == "online")
            {
                _redis.publish(id, js.dump());
            }
            else
            {
                // 存储离线群消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}

// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);  
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
        it->second->send(msg);
        return;
    }
    
    // 存储该用户的离线消息
    _offlineMsgModel.insert(userid, msg);
}