#include "config/provider/SwanConfigProvider.h"

#include "app_config/AppConfig.h"
#include "common/MachineInfoUtil.h"
#include "common/LogFileUtils.h"
#include "common/StringTools.h"
#include "restclient-cpp/restclient.h"
#include "Logger.h"

#include <boost/format.hpp>
#include <yaml-cpp/yaml.h>
#include <vector>
#include <iostream>
#include <sstream>
#include <iterator>
#include <unistd.h>
#include <set>
#include <algorithm>
#include <string>
#include <regex>


namespace logtail {

std::unordered_map<std::string, std::string> getSendMQProperties(const std::string & propertiesStr) {
    std::unordered_map<std::string, std::string> properties;
    std::stringstream ss(propertiesStr);
    std::string property;
    while (std::getline(ss, property, ',')) {
        std::string key = property.substr(0, property.find("="));
        std::string value = property.substr(property.find("=") + 1, property.size() - 1);
        properties[key] = std::move(value);
    }

    return properties;
}

/**
 * 从sasl_jaas_config配置中获取用户名、密码
 */
std::pair<std::string, std::string> getUsernamePasswd(const std::string & saslJaasConfig) {
    std::regex username_regex(R"(username="([^"]*))", std::regex::extended);
    std::smatch username_match;
    std::regex_search(saslJaasConfig, username_match, username_regex);
    std::string username = username_match[1].str();

    std::regex password_regex(R"(password="([^"]*))", std::regex::extended);
    std::smatch password_match;
    std::regex_search(saslJaasConfig, password_match, password_regex);
    std::string password = password_match[1].str();

    return std::make_pair(username, password);
}

int StringToInt(const std::string& str) {
    try {
        return StringTo<int>(str);
    } catch (...) {
        return -1;
    }
}

void SwanConfigProvider::Init() {
    const Json::Value & confJson = AppConfig::GetInstance()->GetConfig();

    if (confJson.isMember("agent_manager_physical_config_api")) {
        agent_manager_physical_config_api = confJson["agent_manager_physical_config_api"].asString();
    } else {
        agent_manager_physical_config_api = "http://10.88.129.57:8000/agent-manager-gz/api/v2/agents/config/host";
    }

    if (confJson.isMember("agent_manager_container_config_api")) {
        agent_manager_container_config_api = confJson["agent_manager_container_config_api"].asString();
    } else {
        agent_manager_container_config_api = "http://10.88.129.57:8000/agent-manager-gz/api/v5/agents/config/model";
    }

    if (confJson.isMember("ddcloud_pods_api")) {
        ddcloud_pods_api = confJson["ddcloud_pods_api"].asString();
    } else {
        ddcloud_pods_api = "http://127.0.0.1:8031/v1/data/pods";
    }

    if (confJson.isMember("ddcloud_pods_dir_map_api")) {
        ddcloud_pods_dir_map_api = confJson["ddcloud_pods_dir_map_api"].asString();
    } else {
        ddcloud_pods_dir_map_api = "http://127.0.0.1:8031/v1/data/pod/dirmap";
    }

    if (confJson.isMember("ddcloud_host_prefix")) {
        ddcloud_host_prefix = confJson["ddcloud_host_prefix"].asString();
    } else {
        ddcloud_host_prefix = "ddcloud-";
    }
}

std::vector<SwanConfigProvider::Container> SwanConfigProvider::getContainers() {
    RestClient::Response response = RestClient::get(ddcloud_pods_api);
    if (response.code != 200) {
        throw std::runtime_error("get pods from ddcloud api exception");
    }

    std::istringstream inputStream(response.body);
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    bool parseResult = Json::parseFromStream(builder, inputStream, &root, &errs);
    if (!parseResult) {
        throw std::runtime_error("parse ddcloud api response exception: " + errs);
    }

    const Json::Int code = root["code"].asInt();
    if (code != 200) {
        throw std::runtime_error("ddcloud api return data may be error");
    }

    std::vector<Container> containers;
    const Json::Value & data = root["data"];
    for (const Json::Value & item : data) {
        std::string podName = item["hostname"].asString();
        std::string podCluster = item["odin_cluster"].asString();
        std::string podService = item["odin_service"].asString();
        podService.replace(podService.find(".didi.com"), 9, "");

        bool isDynamicPod = !podName.empty() && podName.find("-sf-") != std::string::npos;
        bool isGoodNs = !podService.empty() && !podCluster.empty();
        if (!isDynamicPod || !isGoodNs) {
            continue;
        }

        if (podService.find("Bigdata") == std::string::npos) {
            continue;
        }

        containers.emplace_back(podName, podService, podCluster);
    }

    return containers;
}

bool SwanConfigProvider::isNeededContainer(const std::string & filterRule, const std::set<std::string> & clusterNames, const Container & container) {
    if (filterRule == "NONE" || clusterNames.size() == 0)
        return true;

    const std::string & clusterName = container.clusterName;

    if (filterRule == "WHITELIST" && clusterNames.count(clusterName) > 0)
        return true;

    return filterRule == "BLACKLIST" && clusterNames.count(clusterName) <= 0;
}

std::string SwanConfigProvider::getContainerRealLogPath(const std::string & dockerName, const std::string & dockerPath) {
    boost::format params_fmt("[{\"hostname\":\"%s\",\"dirs\": [\"%s\"]}]");
    params_fmt % dockerName % dockerPath;
    std::string params = params_fmt.str();

    const RestClient::Response& response = RestClient::post(ddcloud_pods_dir_map_api, "", params);
    if (response.code != 200) {
        throw std::runtime_error("get ddcloud path from ddcloud api exception");
    }

    std::istringstream inputStream(response.body);
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    bool parseResult = Json::parseFromStream(builder, inputStream, &root, &errs);
    if (!parseResult)
        return {};

    std::string targetPath = root["data"][dockerName][dockerPath]["target"].asString();
    if (targetPath == "NO_HOST_MAP_DIR")
        return {};

    return targetPath;
}

std::string SwanConfigProvider::generateYamlConfig(const SwanConfig & swanConfig) {
    YAML::Node rootNode;
    rootNode["enable"] = true;

    // input插件
    YAML::Node inputsNode;
    YAML::Node input1;
    input1["Type"] = "input_file";

    YAML::Node input1FilePaths;
    input1FilePaths.push_back(swanConfig.logPathDir + swanConfig.filePattern);
    input1["FilePaths"] = input1FilePaths;

    /// 多行读取
    if(swanConfig.timeFormat != "NoLogTime") {
        input1["Multiline"]["Mode"] = "TimeRule";
        input1["Multiline"]["StartFlagIndex"] = swanConfig.timeStartFlagIndex;
        input1["Multiline"]["StartFlag"] = swanConfig.timeStartFlag;
        input1["Multiline"]["TimeStringLength"] = swanConfig.timeFormatLength;
        input1["Multiline"]["TimeFormat"] = convertJavaFormatToStrptime(swanConfig.timeFormat);
        input1["Multiline"]["UnmatchedContentTreatment"] = "single_line";
        /// 不支持解析毫秒时间
        if (swanConfig.timeFormat == "yyyy-MM-dd'T'HH:mm:ss.SSS") {
            /// yyyy-MM-dd'T'HH:mm:ss
            input1["Multiline"]["TimeStringLength"] = 19;
        }
    }

    input1["AppendingLogPositionMeta"] = true;
    input1["AllowingIncludedByMultiConfigs"] = true;

    inputsNode.push_back(input1);
    rootNode["inputs"] = inputsNode;

    // flusher插件
    YAML::Node flushersNode;
    YAML::Node flusher1;
    flusher1["Type"] = "flusher_kafka_v3";
    flusher1["Brokers"] = swanConfig.brokers;
    flusher1["Topic"] = swanConfig.topic;
    flusher1["Username"] = swanConfig.username;
    flusher1["Password"] = swanConfig.password;
    flusher1["HostName"] = swanConfig.hostName;
    flusher1["OriginalAppName"] = swanConfig.originalAppName;
    flusher1["OdinLeaf"] = swanConfig.odinLeaf;
    flusher1["LogId"] = StringToInt(swanConfig.logModelId);
    flusher1["AppName"] = swanConfig.appName;
    flusher1["QueryFrom"] = swanConfig.queryFrom;
    flusher1["IsService"] = StringToInt(swanConfig.isService);
    flusher1["DIDIENV_ODIN_SU"] = swanConfig.odinSu;
    flusher1["PathId"] = StringToInt(swanConfig.pathId);

    flushersNode.push_back(flusher1);
    rootNode["flushers"] = flushersNode;

    YAML::Emitter emitter;
    emitter << rootNode;

    return emitter.c_str();
}

void SwanConfigProvider::convertContainerConfigs(std::string & serviceName,
                                                 std::string & taskType,
                                                 const Json::Value & commonConfig,
                                                 const Json::Value & clusterConfig,
                                                 const Json::Value & sourceConfig,
                                                 const Json::Value & targetConfig,
                                                 const Json::Value & eventMetricsConfig,
                                                 std::vector<Container> & containers) {
    std::string filterRule = clusterConfig["filterRule"].asString();

    std::set<std::string> clusterNames;
    for (const auto & clusterName : clusterConfig["clusterNames"]) {
        clusterNames.insert(clusterName.asString());
    }

    for (const auto & container : containers) {
        bool isNeededContainerFlag = isNeededContainer(filterRule, clusterNames, container);
        if (!isNeededContainerFlag)
            continue;

        const std::string & containerName = container.containerName;

        // std::string fileSuffix = sourceConfig["matchConfig"]["fileSuffix"].asString();

       // 发送配置
        auto targetProperties = getSendMQProperties(targetConfig["properties"].asString());
        const std::pair<std::string, std::string> auth = getUsernamePasswd(targetProperties["sasl_jaas_config"]);

        SwanConfig swanConfig;
        swanConfig.timeStartFlagIndex = sourceConfig["timeStartFlagIndex"].asInt();
        swanConfig.timeStartFlag = sourceConfig["timeStartFlag"].asString();
        swanConfig.timeFormat = sourceConfig["timeFormat"].asString();
        swanConfig.timeFormatLength = sourceConfig["timeFormatLength"].asInt();

        swanConfig.username = auth.first;
        swanConfig.password = auth.second;
        swanConfig.topic = "logtail_survey_" + targetConfig["topic"].asString();
        swanConfig.brokers = targetProperties["gateway"];
        std::replace(swanConfig.brokers.begin(), swanConfig.brokers.end(), ';', ',');
        swanConfig.hostName = containerName;
        swanConfig.odinLeaf = container.clusterName;
        swanConfig.originalAppName = container.serviceName;
        swanConfig.logModelId = std::to_string(commonConfig["modelId"].asInt());
        swanConfig.appName = container.serviceName;
        swanConfig.queryFrom = eventMetricsConfig["queryFrom"].asString();
        swanConfig.isService = std::to_string(eventMetricsConfig["isService"].asInt());
        swanConfig.odinSu = swanConfig.odinLeaf + "." + swanConfig.appName;

        const Json::Value & logPaths = sourceConfig["logPaths"];
        for (const Json::Value & logPath : logPaths) {
            std::string path = logPath["path"].asString();
            std::string realLogPath = getContainerRealLogPath(containerName, path);
            if (realLogPath.empty()) {
                continue;
            }
            int lastSepPos = realLogPath.find_last_of("/") + 1;

            // 目录
            swanConfig.logPathDir = realLogPath.substr(0, lastSepPos);
            swanConfig.filePattern = realLogPath.substr(lastSepPos, realLogPath.length());
            swanConfig.pathId = std::to_string(logPath["pathId"].asInt());

            std::string yamlConfigName = containerName + "_" + swanConfig.logModelId + "_"   + swanConfig.pathId + ".yaml";
            configs[std::move(yamlConfigName)] = std::move(generateYamlConfig(swanConfig));
        }
    }
}

void SwanConfigProvider::convertContainerTasks(const std::string & configStr, std::unordered_map<std::string, std::vector<Container>> & containerMap) {
    std::istringstream inputStream(configStr);
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    bool parseResult = Json::parseFromStream(builder, inputStream, &root, &errs);
    if (!parseResult)
        return;

    const Json::Int code = root["code"].asInt();
    if (code != 0) {
        return;
    }

    const Json::Value & data = root["data"]["modelConfigs"];
    for (const Json::Value & item : data) {
        std::string serviceName = item["serviceName"].asString();
        if (serviceName == "Global") {
            continue;
        }

        auto it = containerMap.find(serviceName);
        if (it == containerMap.end()) {
            continue;
        }

        std::string taskType = item["tag"].asString();
        if (taskType != "log2kafka") {
            continue;
        }
        
        const Json::Value & commonConfig = item["commonConfig"];
        const Json::Value & sourceConfig = item["sourceConfig"];
        const Json::Value & eventMetricsConfig = item["eventMetricsConfig"];
        const Json::Value & clusterConfig = item["clusterConfig"];
        const Json::Value & targetConfig = item["targetConfig"];
        std::vector<Container> & containers = it->second;

        convertContainerConfigs(serviceName, taskType, commonConfig, clusterConfig, sourceConfig, targetConfig, eventMetricsConfig, containers);
    }
}

void SwanConfigProvider::generateDirectConfigs(const std::string & hostname) {
    try
    {
        // 1. 从AM获取采集配置
        boost::format url_fmt(agent_manager_physical_config_api + "?hostName=%s");
        url_fmt % hostname;
        std::string url = url_fmt.str();

        RestClient::Response response = RestClient::get(url);
        if (response.code != 200) {
            throw std::runtime_error("agent manager response, code: " + ToString(response.code) + ", exception: " + response.body + ", url: " + url);
        }

        std::istringstream inputStream(response.body);
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        bool parseResult = Json::parseFromStream(builder, inputStream, &root, &errs);
        if (!parseResult) {
            throw std::runtime_error("parse config exception: " + errs);
        }

        const Json::Value & modelConfigs = root["data"]["modelConfigs"];
        for (const Json::Value & modelConfig : modelConfigs) {
            const Json::Value & eventMetricsConfig = modelConfig["eventMetricsConfig"];
            std::string serviceName = eventMetricsConfig["belongToCluster"].asString();
            if (serviceName == "Global") {
                continue;
            }

            std::string taskType = modelConfig["tag"].asString();
            if (taskType != "log2kafka") {
                continue;
            }

            const Json::Value & commonConfig = modelConfig["commonConfig"];
            const Json::Value & sourceConfig = modelConfig["sourceConfig"];
            const Json::Value & targetConfig = modelConfig["targetConfig"];

            SwanConfig swanConfig;

            // 发送参数
            auto sendMQProperties = getSendMQProperties(targetConfig["properties"].asString());
            auto auth = getUsernamePasswd(sendMQProperties["sasl_jaas_config"]);
            // std::string fileSuffix = sourceConfig["matchConfig"]["fileSuffix"].asString();

            swanConfig.topic = "logtail_survey_" + targetConfig["topic"].asString();
            swanConfig.brokers = sendMQProperties["gateway"];
            std::replace(swanConfig.brokers.begin(), swanConfig.brokers.end(), ';', ',');
            swanConfig.originalAppName = eventMetricsConfig["originalAppName"].asString();
            swanConfig.odinLeaf = eventMetricsConfig["odinLeaf"].asString();
            swanConfig.logModelId = std::to_string(commonConfig["modelId"].asInt());
            swanConfig.appName = eventMetricsConfig["originalAppName"].asString();
            swanConfig.queryFrom = eventMetricsConfig["queryFrom"].asString();
            swanConfig.isService = std::to_string(eventMetricsConfig["isService"].asInt());
            swanConfig.odinSu = "";
            swanConfig.hostName = hostname;
            swanConfig.username = auth.first;
            swanConfig.password = auth.second;

            swanConfig.timeStartFlagIndex = sourceConfig["timeStartFlagIndex"].asInt();
            swanConfig.timeStartFlag = sourceConfig["timeStartFlag"].asString();
            swanConfig.timeFormat = sourceConfig["timeFormat"].asString();
            swanConfig.timeFormatLength = sourceConfig["timeFormatLength"].asInt();

            const Json::Value & logPaths = sourceConfig["logPaths"];
            for (const Json::Value & logPath : logPaths) {
                std::string path = logPath["path"].asString();
                const std::string realLogPath = logPath["realPath"].asString();
                int lastSepPos = realLogPath.find_last_of("/") + 1;

                // 目录
                swanConfig.logPathDir = realLogPath.substr(0, lastSepPos);
                swanConfig.filePattern = realLogPath.substr(lastSepPos, realLogPath.length());
                swanConfig.pathId = std::to_string(logPath["pathId"].asInt());

                std::string yamlConfigName = swanConfig.logModelId + "_" + swanConfig.pathId + ".yaml";
                configs[std::move(yamlConfigName)] = std::move(generateYamlConfig(swanConfig));
            }
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("get direct configs exception: " + std::string(e.what()));
    }
}

void SwanConfigProvider::generateContainerConfigs(const std::string & hostname) {
    try {
        // 1. 获取容器列表
        std::vector<Container> containers = getContainers();

        // 2. 构建服务名&容器之间的映射
        std::unordered_map<std::string, std::vector<Container>> serviceAndContainersMapping;
        for (auto & container : containers) {
            auto & serviceName = container.serviceName;

            auto it = serviceAndContainersMapping.find(serviceName);
            if (it != serviceAndContainersMapping.end()) {
                std::vector<Container> & entry = it->second;
                entry.push_back(container);
            } else {
                std::vector<Container> serviceContainers;
                serviceContainers.push_back(container);
                serviceAndContainersMapping[serviceName] = serviceContainers;
            }
        }

        // 3. 获取所有服务名
        std::set<std::string> serviceNames;
        for (const auto & item : serviceAndContainersMapping) {
            serviceNames.insert(item.first);
        }

        /// 构建请求参数
        std::map<std::string, std::string> requestParams;
        requestParams["hostName"] = hostname;

        std::stringstream ss;
        for (const auto & str : serviceNames) {
            if (ss.str().empty()) {
                ss << str;
            } else {
                ss << "," << str;
            }
        }
        requestParams["serviceNames"] = ss.str();

        // 4. 获取采集配置
        boost::format url_fmt(agent_manager_container_config_api + "?hostName=%s&serviceNames=%s");
        url_fmt % requestParams.at("hostName") % requestParams.at("serviceNames");
        std::string url = url_fmt.str();

        RestClient::Response response = RestClient::get(url);
        if (response.code != 200) {
            throw std::runtime_error("agent manager response, code: " + ToString(response.code) + ", exception: " + response.body + ", url: " + url);
        }

        // 5. 转换配置
        convertContainerTasks(response.body, serviceAndContainersMapping);
    } catch (const std::exception& e) {
        throw std::runtime_error("get container config exception: " +  ToString(e.what()));
    }
}

std::unordered_map<std::string, std::string> & SwanConfigProvider::getConfigs() {
    // 1. 获取主机名
    std::string hostname = GetHostName();

    configs.clear();
    try {
        // 2. 获取直采任务配置
        generateDirectConfigs(hostname);
    }
    catch(const std::exception& e) {
        if (!isDDCloudHost(hostname)) {
            throw e;
        }
        LOG_ERROR(sLogger, ("generate direct configs failed.", e.what()));
    }

    try {
        // 3. 获取容器的任务配置
        generateContainerConfigs(hostname);
    } catch(const std::exception& e) {
        if (configs.empty()) {
            throw e;
        }
        LOG_ERROR(sLogger, ("generate container configs failed.", e.what()));
        return configs;
    }

    LOG_INFO(sLogger, ("swan configs size.", configs.size()));
    return configs;
}

} // namespace logtail
