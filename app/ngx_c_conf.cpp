#include <mutex>
#include <fstream>
#include <string>
#include <cstring>

#include "ngx_c_conf.h"


CConfig* CConfig::m_instance = nullptr;

CConfig::CConfig()  
{
    
}

CConfig::~CConfig()
{
    for(auto& it : m_ConfigItemList)
    {
        delete it;
    }
    m_ConfigItemList.clear();
}

CConfig::DestroyCon::~DestroyCon()  //单例释放资源
{
    if(CConfig::m_instance != nullptr)
    {
        delete CConfig::m_instance;
        CConfig::m_instance = nullptr;
    }
}

void CConfig::Create(void)
{
    if(CConfig::m_instance == nullptr)
    {
        CConfig::m_instance = new CConfig;
        static DestroyCon cl;
    }
}

CConfig* CConfig::GetInstance(void)
{
    static std::once_flag create_falg;

    std::call_once(create_falg, Create);

    return CConfig::m_instance;
}

bool CConfig::Load(const char *pconfName)
{
    std::ifstream ifs;

    ifs.open(pconfName, std::ios::in);

    if(!ifs.is_open())
    {
        return false;   //文件打开失败，返回false;
    }

    char linebuf[501] = {0};

    while(!ifs.eof())   //读到文件末尾结束
    {
        ifs.getline(linebuf, 500);  //getline默认读到换行符

        //保证配置头正确  空白行第一个ascii值是0
        if(linebuf[0] == ' ' || linebuf[0] == '#' || linebuf[0] == ';' || linebuf[0] == '\t' || linebuf[0] == '\n' || linebuf[0] == '[' || linebuf[0] == 0)
            continue;
        std::string confdata(linebuf, strlen(linebuf));
        bzero(linebuf, confdata.size());
        LPCConfItem p_confitem = new CConfItem;
        //保证配置尾没有多余数据 包括换行 回车 空格等
        int posa = confdata.find(';');   //查找;位置
        int posb = confdata.find('=');  //查找=位置
        int posc = confdata.find(' ');  //查找第一个空格位置
        // confdata = confdata.substr(0, posa);//舍弃后面注释部分
        // std::cout << confdata << std::endl;
        if(posc == std::string::npos || posc > posb)
        {
            p_confitem->ItemName = confdata.substr(0, posb);
            //memcpy(p_confitem->ItemName, confdata.substr(0, posb).data(), confdata.substr(0, posb).size());
        }
        else
        {
            p_confitem->ItemName = confdata.substr(0, posc);
            //memcpy(p_confitem->ItemName, confdata.substr(0, posc).data(), confdata.substr(0, posb).size());
        }
        confdata = confdata.substr(posb + 1, posa - posb - 1);  //获取等号后的字符串
        while(confdata[0] == ' ')       //清除等号后的空格
        {
            confdata.erase(confdata.begin());
        }

        p_confitem->IterContent = confdata;
        //memcpy(p_confitem->IterContent, confdata.data(), confdata.size());
        m_ConfigItemList.emplace_back(p_confitem);
        //std::cout << p_confitem->ItemName << '\t' <<  p_confitem->IterContent << std::endl;
    }

    return true;
}

//获取字符类型配置信息 未查找到对应数据返回nullptr
const char* CConfig::GetString(const char *p_itemname)
{
    std::unique_ptr<CConfItem> compredata(new CConfItem(p_itemname, " "));  //vector内部存放的是指针，所以需要构造指针通过lambda表达式完成对比
    auto condition = [&](const LPCConfItem& p1)->bool{
        if(p1->ItemName == compredata.get()->ItemName)
            return true;
        else
            return false;
    };

    auto it = std::find_if(m_ConfigItemList.begin(), m_ConfigItemList.end(), condition);
    if(it == m_ConfigItemList.end())
    {
        return nullptr;   
    }

    return (*it)->IterContent.c_str();
}

template<>
float StoT<float>(std::string& str)
{
    return std::stod(str);
}

template<>
int StoT<int>(std::string& str)
{
    return std::stoi(str);
}

template<>
double StoT<double>(std::string& str)
{
    return std::stod(str);
}

template<>
short StoT<short>(std::string& str)
{
    return (short)std::stoi(str);
}

// //获取数据类型配置信息
// int CConfig::GetIntDefault(const char *p_itemname,const int def)    
// {
//     std::unique_ptr<CConfItem> compredata(new CConfItem(p_itemname, " "));  //vector内部存放的是指针，所以需要构造指针通过lambda表达式完成对比
//     auto condition = [&](const LPCConfItem& p1)->bool{
//         if(p1->ItemName == compredata.get()->ItemName)
//             return true;
//         else
//             return false;
//     };
//     std::unique_ptr<CConfItem> compre(new CConfItem(p_itemname, " "));
//     auto it = std::find_if(m_ConfigItemList.begin(), m_ConfigItemList.end(), condition);   
//     if(it == m_ConfigItemList.end())
//         return def;
//     else
//         return std::stoi((*it)->IterContent);
// }