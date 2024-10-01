/**
 * @file config.cc
 * @brief 配置模块
 * @author beanljun
 * @date 2024-04-28
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../util/util.h"
#include "../include/env.h"
#include "../include/config.h"

namespace sylar {

    static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

    ConfigVarBase::ptr Config::LookupBase(const std::string &name) {
        RWMutexType::ReadLock lock(GetMutex());
        auto it = GetDatas().find(name);
        return it == GetDatas().end() ? nullptr : it->second;
    }

    // 递归方式，遍历YAML格式的配置文件中的所有成员，将每个节点的名称和值存在list中
    static void ListAllMember(const std::string &prefix, const YAML::Node &node, std::list<std::pair<std::string, const YAML::Node>> &output) {
        // prefix字符不合法
        if (prefix.find_first_not_of("abcdefghijklmnopqrstuvwxyz._012345678") != std::string::npos) {
            SYLAR_LOG_ERROR(g_logger) << "Config invalid name: " << prefix << " : " << node;
            return;
        }
        output.emplace_back(std::make_pair(prefix, node));
        // 若解析的是map
        if (node.IsMap()) {
            for (auto it = node.begin(); it != node.end(); ++it) {
                // 若前缀为空,说明为顶层，prefix为key的值，否则为子层，prefix为父层加上当前层。it->second为当前node
                ListAllMember(prefix.empty() ? it->first.Scalar() : prefix + "." + it->first.Scalar(), it->second, output);
            }
        }
    }

    void Config::LoadFromYaml(const YAML::Node &root) {
        std::list<std::pair<std::string, const YAML::Node>> all_nodes;
        ListAllMember("", root, all_nodes);

        for (auto &i : all_nodes) {
            std::string key = i.first;
            if (key.empty()) continue;
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            ConfigVarBase::ptr var = LookupBase(key);

            if (var) {
                if (i.second.IsScalar()) var->fromString(i.second.Scalar()); 
                else {
                    std::stringstream ss;
                    ss << i.second;
                    var->fromString(ss.str());
                }
            }
        }
    }

    /// 记录文件的修改时间
    static std::map<std::string, uint64_t> s_file2modifytime;
    static sylar::Mutex s_mutex;

    void Config::LoadFromConfDir(const std::string &path, bool force) {
        std::string absoulte_path = sylar::EnvMgr::GetInstance()->getAbsolutePath(path);
        std::vector<std::string> files;
        sylar::FSUtil::ListAllFile(files, absoulte_path, ".yml");

        for (auto &i : files) {
            {
                struct stat st;
                lstat(i.c_str(), &st);
                sylar::Mutex::Lock lock(s_mutex);
                if (!force && s_file2modifytime[i] == (uint64_t)st.st_mtime)  continue;
                s_file2modifytime[i] = st.st_mtime;
            }
            try {
                YAML::Node root = YAML::LoadFile(i);
                LoadFromYaml(root);
                SYLAR_LOG_INFO(g_logger) << "LoadConfFile file="
                                        << i << " ok";
            } catch (...) {
                SYLAR_LOG_ERROR(g_logger) << "LoadConfFile file="
                                        << i << " failed";
            }
        }
    }

    void Config::Visit(std::function<void(ConfigVarBase::ptr)> cb) {
        RWMutexType::ReadLock lock(GetMutex());
        ConfigVarMap &datas = GetDatas();
        for (auto it = datas.begin(); it != datas.end(); ++it) {
            cb(it -> second);
        }
    }

}