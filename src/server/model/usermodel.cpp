#include "usermodel.hpp"
#include "db.h"
#include <iostream>
using namespace std;

// User表的增加方法
bool UserModel::insert(User &user)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into User(name, password, state) values('%s', '%s', '%s')",
            user.getName().c_str(), user.getPwd().c_str(), user.getState().c_str());

    MySQL mysql;
    if (mysql.connect())
    {
        if (mysql.update(sql)) // 把上面的三个参数更新到数据库中。 更新进取后 数据库自动产生一个id 但是我们的类没有 所以要拿出来设置进程序的user中。
        {
            // 获取插入成功的用户数据生成的主键id
            user.setId(mysql_insert_id(mysql.getConnection())); // id是插入的时候就会自动生成 拿出来传给类user
            return true;
        }
    }

    return false;
}

// 根据用户号码查询用户信息
User UserModel::query(int id) // 主键是不允许重复的 自然可以用id来查。
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select * from User where id = %d", id); // 作者的user是小写

    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);  // mysql.query是内置的。
        if (res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res); // 拿出来每一行
            if (row != nullptr) // 非空表示查成功了。
            {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setPwd(row[2]);
                user.setState(row[3]);
                mysql_free_result(res); // 去释放
                return user;
            }
        }
    }

    return User(); // 如果没找到返回一个临时构造的User对象，id是-1。
}

// 更新用户的状态信息
bool UserModel::updateState(User user)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "update User set state = '%s' where id = %d", user.getState().c_str(), user.getId());

    MySQL mysql;
    if (mysql.connect())
    {
        if (mysql.update(sql))
        {
            return true;
        }
    }
    return false;
}

// 重置用户的状态信息
void UserModel::resetState()
{
    // 1.组装sql语句
    char sql[1024] = "update User set state = 'offline' where state = 'online'";

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}