
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include <ctime>

#include "util/threadpool.hpp"

#include "loginmanager.hpp"
#include "player.hpp"
#include "world.hpp"

LoginManager::LoginManager(Config& config, const std::unordered_map<HashFunc, std::shared_ptr<Hasher>>& passwordHashers)
    : _config(config)
    , _passwordHashers(passwordHashers)
    , _processCount(0)
{
}

bool LoginManager::CheckLogin(const std::string& username, util::secure_string&& password)
{
    auto res = this->CreateDbConnection()->Query("SELECT `password`, `password_version` FROM `accounts` WHERE `username` = '$'", username.c_str());

    if (!res.empty())
    {
        HashFunc dbPasswordVersion = static_cast<HashFunc>(res[0]["password_version"].GetInt());
        std::string dbPasswordHash = std::string(res[0]["password"]);

        password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(password)));

        return this->_passwordHashers[dbPasswordVersion]->check(password.str(), dbPasswordHash);
    }

    return false;
}

void LoginManager::SetPassword(const std::string& username, util::secure_string&& password)
{
    auto passwordVersion = static_cast<HashFunc>(int(this->_config["PasswordCurrentVersion"]));
    password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(password)));
    password = std::move(this->_passwordHashers[passwordVersion]->hash(password.str()));

    this->CreateDbConnection()->Query("UPDATE `accounts` SET `password` = '$', `password_version` = # WHERE username = '$'",
        password.str().c_str(),
        int(passwordVersion),
        username.c_str());
}

void LoginManager::CreateAccountAsync(AccountCreateInfo&& accountInfo, std::function<void(void)> successCallback, std::function<void(void)> failureCallback)
{
    auto createAccountThreadProc = [this, successCallback, failureCallback](const void * state)
    {
        auto accountCreateInfo = reinterpret_cast<const AccountCreateInfo*>(state);
        auto passwordVersion = static_cast<HashFunc>(int(this->_config["PasswordCurrentVersion"]));

        auto password = std::move(accountCreateInfo->password);
        password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), accountCreateInfo->username, std::move(password)));
        password = std::move(this->_passwordHashers[passwordVersion]->hash(password.str()));

        auto db_res = this->CreateDbConnection()->Query(
            "INSERT INTO `accounts` (`username`, `password`, `fullname`, `location`, `email`, `computer`, `hdid`, `regip`, `created`, `password_version`)"
            " VALUES ('$','$','$','$','$','$',#,'$',#,#)",
            accountCreateInfo->username.c_str(),
            password.str().c_str(),
            accountCreateInfo->fullname.c_str(),
            accountCreateInfo->location.c_str(),
            accountCreateInfo->email.c_str(),
            accountCreateInfo->computer.c_str(),
            accountCreateInfo->hdid,
            static_cast<std::string>(accountCreateInfo->remoteIp).c_str(),
            int(std::time(0)),
            int(passwordVersion));

        if (!db_res.Error())
        {
            successCallback();
        }
        else
        {
            failureCallback();
        }

        delete accountCreateInfo;
    };

    auto state = reinterpret_cast<void*>(new AccountCreateInfo(std::move(accountInfo)));
    util::ThreadPool::Queue(createAccountThreadProc, state);
}

void LoginManager::SetPasswordAsync(PasswordChangeInfo&& passwordChangeInfo, std::function<void(void)> successCallback, std::function<void(void)> failureCallback)
{
    auto setPasswordThreadProc = [this, successCallback, failureCallback](const void * state)
    {
        auto passwordChangeInfo = reinterpret_cast<const PasswordChangeInfo*>(state);
        auto oldPassword = std::move(passwordChangeInfo->oldpassword);
        auto newPassword = std::move(passwordChangeInfo->newpassword);

        if (this->CheckLogin(passwordChangeInfo->username, std::move(oldPassword)))
        {
            this->SetPassword(passwordChangeInfo->username, std::move(newPassword));
            successCallback();
        }
        else
        {
            failureCallback();
        }

        delete passwordChangeInfo;
    };

    auto state = reinterpret_cast<void*>(new PasswordChangeInfo(std::move(passwordChangeInfo)));
    util::ThreadPool::Queue(setPasswordThreadProc, state);
}

void LoginManager::UpdatePasswordVersionAsync(const std::string& username, util::secure_string&& password, HashFunc hashFunc)
{
    auto updateThreadProc = [this](const void * state)
    {
        auto updateState = reinterpret_cast<const LoginManager::UpdateState*>(state);
        auto username = updateState->username;
        auto password = std::move(updateState->password);
        auto hashFunc = updateState->hashFunc;
        delete updateState;

        if (hashFunc != NONE)
        {
            password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(password)));
            password = std::move(this->_passwordHashers[hashFunc]->hash(std::move(password.str())));

            this->CreateDbConnection()->Query("UPDATE `accounts` SET `password` = '$', `password_version` = # WHERE `username` = '$'",
                password.str().c_str(),
                hashFunc,
                username.c_str());
        }
    };

    auto state = reinterpret_cast<void*>(new UpdateState { username, std::move(password), hashFunc });
    util::ThreadPool::Queue(updateThreadProc, state);
}

void LoginManager::CheckLoginAsync(const std::string& username, util::secure_string&& password, std::function<void(Database*)> successCallback, std::function<void(LoginReply)> failureCallback)
{
    auto loginThreadProc = [this, successCallback, failureCallback](const void * state)
    {
        auto updateState = reinterpret_cast<const LoginManager::UpdateState*>(state);
        auto username = updateState->username;
        auto password = std::move(updateState->password);
        delete updateState;

        auto database = this->CreateDbConnection();
        Database_Result res = database->Query("SELECT `password`, `password_version` FROM `accounts` WHERE `username` = '$'", username.c_str());

        if (!res.empty())
        {
            HashFunc dbPasswordVersion = static_cast<HashFunc>(res[0]["password_version"].GetInt());
            HashFunc currentPasswordVersion = static_cast<HashFunc>(this->_config["PasswordCurrentVersion"].GetInt());
            std::string dbPasswordHash = std::string(res[0]["password"]);

            if (dbPasswordVersion < currentPasswordVersion)
            {
                // A copy is made of the password since the background thread needs to have separate ownership of it
                //
                util::secure_string passwordCopy(std::string(password.str()));
                this->UpdatePasswordVersionAsync(updateState->username, std::move(passwordCopy), currentPasswordVersion);
            }

            password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(password)));
            if (this->_passwordHashers[dbPasswordVersion]->check(password.str(), dbPasswordHash))
            {
                successCallback(database.get());
            }
            else
            {
                failureCallback(LOGIN_WRONG_USERPASS);
            }
        }
        else
        {
            failureCallback(LOGIN_WRONG_USER);
        }

        this->_processCount--;
    };

    // use UpdateState in place of std::pair because it is easier to work with
    auto state = reinterpret_cast<void*>(new UpdateState { username, std::move(password), HashFunc::NONE });
    util::ThreadPool::Queue(loginThreadProc, state);
    this->_processCount++;
}

std::unique_ptr<Database> LoginManager::CreateDbConnection()
{
    auto dbType = util::lowercase(std::string(this->_config["DBType"]));

    Database::Engine engine = Database::MySQL;
    if (!dbType.compare("sqlite"))
    {
        engine = Database::SQLite;
    }
    else if (!dbType.compare("sqlserver"))
    {
        engine = Database::SqlServer;
    }
    else if (!dbType.compare("mysql"))
    {
        engine = Database::MySQL;
    }

    auto dbHost = std::string(this->_config["DBHost"]);
    auto dbUser = std::string(this->_config["DBUser"]);
    auto dbPass = std::string(this->_config["DBPass"]);
    auto dbName = std::string(this->_config["DBName"]);
    auto dbPort = int(this->_config["DBPort"]);

    return std::move(std::unique_ptr<Database>(new Database(engine, dbHost, dbPort, dbUser, dbPass, dbName)));
}