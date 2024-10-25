#pragma once

#include "common/StringTools.h"

#include <unordered_map>
#include <set>
#include <vector>
#include <json/json.h>

namespace logtail {

/**
 * Swan的任务配置转换为ilogtail的任务配置.
 * 依赖于CommonConfigProvider运行
 */
class SwanConfigProvider {
public:
    SwanConfigProvider(const SwanConfigProvider&) = delete;
    SwanConfigProvider& operator=(const SwanConfigProvider&) = delete;

    static SwanConfigProvider* GetInstance() {
        static SwanConfigProvider instance;
        return &instance;
    }

    void Init();

    using Configs = std::unordered_map<std::string, std::string>;

    Configs getConfigs() const;

private:
    SwanConfigProvider() = default;
    ~SwanConfigProvider() = default;

    struct SwanConfig
    {
        std::string logPathDir;
        std::string filePattern;
        std::string hostName;
        std::string originalAppName;
        std::string odinLeaf;
        std::string logModelId;
        std::string appName;
        std::string queryFrom;
        std::string isService;
        std::string pathId;
        std::string odinSu;
        std::vector<std::string> brokers;
        std::string topic;
        std::string username;
        std::string password;
        std::string timeFormat;
        int timeFormatLength;
        std::string timeStartFlag;
        int timeStartFlagIndex;
    };

    struct Container {
        std::string containerName;
        std::string serviceName;
        std::string clusterName;

        Container(std::string & containerName_, std::string & serviceName_, std::string & clusterName_)
            : containerName(containerName_), serviceName(serviceName_), clusterName(clusterName_)
        {}
        
    };

    /// 转换swan非容器化采集
    Configs getDirectConfigs(const std::string & hostname) const;

    /// 转换swan容器化采集
    Configs getContainerConfigs(const std::string & hostname) const;

    /// 生成yaml任务配置
    std::string generateYamlConfig(const SwanConfig & swanConfig) const;

    std::vector<Container> getContainers() const;

    /// 转换采集配置
    Configs convertContainerTasks(const std::string & configStr, std::unordered_map<std::string, std::vector<Container>> & containerMap) const;

    void convertContainerConfigs(Configs & result,
                        std::string & serviceName,
                        std::string & taskType,
                        const Json::Value & commonConfig,
                        const Json::Value & clusterConfig,
                        const Json::Value & sourceConfig,
                        const Json::Value & targetConfig,
                        const Json::Value & eventMetricsConfig,
                        std::vector<Container> & containers) const;

    /// 获取容器的真实的物理路径
    std::string getContainerRealLogPath(const std::string & dockerName, const std::string & dockerPath) const;

    bool isDDCloudHost(const std::string & hostname) const {
        return StartWith(hostname, DDCLOUD_HOST_PREFIX);
    }

    bool isNeededContainer(const std::string & filterRule, const std::set<std::string> & clusterNames, const Container & container) const;

    /// 物理机直采的任务配置
    std::string agent_manager_physical_config_api;
    /// 容器的任务配置
    std::string agent_manager_container_config_api;

    /// didi-cloud
    const std::string DDCLOUD_HOST_PREFIX = "dcloud-";
    /// 获取容器API
    std::string ddcloud_pods_api;
    /// 容器路径与物理机路径映射
    std::string ddcloud_pods_dir_map_api;
};

} // namespace logtail
