#ifndef NGX_C_CONF_H
#define NGX_C_CONF_H

#include <vector>
#include <iostream>
#include <memory>
#include <algorithm>
#include "ngx_global.h"

class CConfig{
private:
    CConfig();

public:
    ~CConfig();

private:
    static CConfig* m_instance;
    class DestroyCon{
    public:
        ~DestroyCon();
    };
    static void Create(void);

public:
    static CConfig* GetInstance(void);
    bool Load(const char *pconfName); //装载配置文件
	const char *GetString(const char *p_itemname);  //根据ItemName获取配置信息字符串
    template<typename T>
	bool GetNum(const char *p_itemname, T& def);//根据ItemName获取数字类型配置信息
public:
    std::vector<LPCConfItem> m_ConfigItemList;
};

template<typename T>
T StoT(std::string& str){}

template<>
float StoT<float>(std::string& str);
template<>
double StoT<double>(std::string& str);
template<>
int StoT<int>(std::string& str);
template<>
short StoT<short>(std::string& str);

//获取数据类型配置信息 未查找到返回false
template<typename T>
bool CConfig::GetNum(const char *p_itemname, T& def)
{
    std::unique_ptr<CConfItem> compredata(new CConfItem(p_itemname, " "));
    auto condition = [&](const LPCConfItem& p1)->bool{
        if(p1->ItemName == compredata.get()->ItemName)
            return true;
        else
            return false;
    };
    std::unique_ptr<CConfItem> compre(new CConfItem(p_itemname, " "));
    auto it = std::find_if(m_ConfigItemList.begin(), m_ConfigItemList.end(), condition);   
    if(it == m_ConfigItemList.end())
        return false;
    else
        def = StoT<T>((*it)->IterContent);
    return true;
}


#endif