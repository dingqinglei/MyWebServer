//
// Created by 11240 on 2022/1/25.
//

#include "httprequest.h"
using namespace std;

const unordered_set<string> HttpRequest::DEFAULT_HTML{
        "/index", "/register", "/login",
        "/welcome", "/video", "/picture","/upload" };

const unordered_map<string, int> HttpRequest::DEFAULT_HTML_TAG {
        {"/register.html", 0}, {"/login.html", 1},  };

void HttpRequest::Init(const string& srcDir) {
    srcDir_ = srcDir;
    method_ = path_ = version_ = body_ = "";
    state_ = REQUEST_LINE;
    header_.clear();
    post_.clear();
}

bool HttpRequest::IsKeepAlive() const {
    if(header_.count("Connection") == 1) {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

bool HttpRequest::parse(Buffer& buff) {
    if(buff.ReadableBytes() <= 0) {
        return false;
    }
    LOG_DEBUG("/n--------------------BufferSize:%d--------------------------",buff.ReadableBytes());
    while(buff.ReadableBytes() && state_ != FINISH) {
        const char* lineEnd = search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);
        std::string line(buff.Peek(), lineEnd);
        switch(state_)
        {
            case REQUEST_LINE:
                LOG_DEBUG("--------------------in REQUEST_LINE:%s",line.data());
                if(!ParseRequestLine_(line)) {
                    return false;
                }
                ParsePath_();
                break;
            case HEADERS:
                ParseHeader_(line);
                LOG_DEBUG("--------------------in HEADERS: ReadableBytes:%d--------------------------",buff.ReadableBytes());
                if(buff.ReadableBytes() <= 2) {
                    state_ = FINISH;
                }
                break;
            case BODY:
                ParseBody_(line,buff);
                break;
            default:
                break;
        }
        if(lineEnd == buff.BeginWrite()) {
            break;
        }
        if(buff.ReadableBytes()!=0) {
            buff.RetrieveUntil(lineEnd + 2);
        }
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}

void HttpRequest::ParsePath_() {
    if(path_ == "/") {
        path_ = "/index.html";
    }
    else {
        for(auto &item: DEFAULT_HTML) {
            if(item == path_) {
                path_ += ".html";
                break;
            }
        }
    }
}

bool HttpRequest::ParseRequestLine_(const string& line) {
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {
        method_ = subMatch[1];
        path_ = subMatch[2];
        version_ = subMatch[3];
        state_ = HEADERS;
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}

void HttpRequest::ParseHeader_(const string& line) {
    regex patten("^([^:]*): ?(.*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {
        header_[subMatch[1]] = subMatch[2];
    }
    else {
        state_ = BODY;
    }
}

void HttpRequest::ParseBody_(const string& line, Buffer& buff) {
    LOG_DEBUG("--------------------inParseBody--------------------------");
    if(method_=="POST") ParsePost_(line,buff);
    state_ = FINISH;
    LOG_DEBUG("--------------Bodylen:%d--------------------", body_.size());
}

int HttpRequest::ConverHex(char ch) {
    if(ch >= 'A' && ch <= 'F') return ch -'A' + 10;
    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
    return ch;
}

bool HttpRequest::ParsePost_(const string& line, Buffer& buff) {
    LOG_DEBUG("--------------------inParsePost--------------------------");
    if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded") {
        body_ = line;
        ParseFormUrlencoded_();
        if(DEFAULT_HTML_TAG.count(path_)) {
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d", tag);
            if(tag == 0 || tag == 1) {
                bool isLogin = (tag == 1);
                if(UserVerify(post_["username"], post_["password"], isLogin)) {
                    path_ = "/welcome.html";
                }
                else {
                    path_ = "/error.html";
                }
            }
        }
    }
    else{
        // 丢弃使用后的buffer
        LOG_DEBUG("--------------------inParseFormdata--------------------------");
        buff.Retrieve(line.length()+2);
        post_["boundary"] = line;
        LOG_DEBUG("boundary: %s",line.c_str());
        while(true){
            const char* tempEnd = search(buff.Peek(), buff.BeginWriteConst(), CRLF,CRLF+2);
            string temp = string(buff.Peek(),tempEnd);
            buff.Retrieve(temp.length()+2);
            regex patten("^([^ ])*:");
            smatch subMatch;
            bool flag = regex_search(temp,subMatch,patten);
            string key = subMatch[0];
            if(key=="Content-Disposition:"){
                regex patten("filename=\"(.*)\"");
                smatch subMatch;
                regex_search(temp,subMatch,patten);
                string filename = subMatch[0];
                post_["filename"] = filename.substr(10,filename.size()-11);
            }
            if(temp.empty()) break;
        }
        LOG_DEBUG("--------------------inregex--------------------------");
        const char* lineEnd = search(buff.Peek(), buff.BeginWriteConst(), line.begin(), line.end());
        body_ = string(buff.Peek(),lineEnd);
        LOG_DEBUG("--------------------body-------------------------");
        ParseFormData_();
        LOG_DEBUG("---------------------buff.RetrieveAll------------------------");
        buff.RetrieveAll();
        LOG_DEBUG("--------------------buffMaxSize:%d-------------------------",buff.WritableBytes());
        path_ = "/success.html";
    }
}

void HttpRequest::ParseFormUrlencoded_() {
    if(body_.empty()) { return; }

    string key, value;
    int num = 0;
    int n = body_.size();
    int i = 0, j = 0;

    for(; i < n; i++) {
        char ch = body_[i];
        switch (ch) {
            case '=':
                key = body_.substr(j, i - j);
                j = i + 1;
                break;
            case '+':
                body_[i] = ' ';
                break;
            case '%':
                num = ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2]);
                body_[i + 2] = num % 10 + '0';
                body_[i + 1] = num / 10 + '0';
                i += 2;
                break;
            case '&':
                value = body_.substr(j, i - j);
                j = i + 1;
                post_[key] = value;
                LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
                break;
            default:
                break;
        }
    }
    assert(j <= i);
    if(post_.count(key) == 0 && j < i) {
        value = body_.substr(j, i - j);
        post_[key] = value;
    }
}

void HttpRequest::ParseFormData_() {
    char path[10] = "./temp";
    char filename[FILE_NAME_LEN] ={0};
    snprintf(filename,FILE_NAME_LEN,"%s/%s",path,post_["filename"].c_str());
    LOG_DEBUG("------------inParseFormData readfile:%s------------",filename);
    if(access(path,0777)==-1){
        mkdir(path,0777);
    }
    FILE* fp_ = fopen(filename,"w");
    fwrite(body_.c_str(),1,body_.size(),fp_);
    //fflush(fp_);
    fclose(fp_);
}

bool HttpRequest::UserVerify(const string &name, const string &pwd, bool isLogin) {
    if(name == "" || pwd == "") { return false; }
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());
    MYSQL* sql;
    SqlConnRAII(&sql,  SqlConnPool::Instance());
    assert(sql);

    bool flag = false;
    unsigned int j = 0;
    char order[256] = { 0 };
    MYSQL_FIELD *fields = nullptr;
    MYSQL_RES *res = nullptr;

    if(!isLogin) { flag = true; }
    /* 查询用户及密码 */
    snprintf(order, 256, "SELECT username, passwd FROM user WHERE username='%s' LIMIT 1", name.c_str());
    LOG_DEBUG("%s", order);

    if(mysql_query(sql, order)) {
        mysql_free_result(res);
        return false;
    }
    res = mysql_store_result(sql);
    j = mysql_num_fields(res);
    fields = mysql_fetch_fields(res);

    while(MYSQL_ROW row = mysql_fetch_row(res)) {
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        string password(row[1]);
        /* 注册行为 且 用户名未被使用*/
        if(isLogin) {
            if(pwd == password) { flag = true; }
            else {
                flag = false;
                LOG_DEBUG("pwd error!");
            }
        }
        else {
            flag = false;
            LOG_DEBUG("user used!");
        }
    }
    mysql_free_result(res);

    /* 注册行为 且 用户名未被使用*/
    if(!isLogin && flag) {
        LOG_DEBUG("regirster!");
        bzero(order, 256);
        snprintf(order, 256,"INSERT INTO user(username, passwd) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG( "%s", order);
        if(mysql_query(sql, order)) {
            LOG_DEBUG( "Insert error!");
            flag = false;
        }
        flag = true;
    }
/* 错误释放已经释放的资源 */
    SqlConnPool::Instance()->FreeConn(sql);
    LOG_DEBUG( "UserVerify success!!");
    return flag;
}

std::string HttpRequest::path() const{
    return path_;
}

std::string& HttpRequest::path(){
    return path_;
}
std::string HttpRequest::method() const {
    return method_;
}

std::string HttpRequest::version() const {
    return version_;
}

std::string HttpRequest::GetPost(const std::string& key) const {
    assert(key != "");
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::GetPost(const char* key) const {
    assert(key != nullptr);
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

