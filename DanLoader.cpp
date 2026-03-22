#include "DanLoader.hpp"
#include "json.hpp"
#include "Logger.hpp"
#include <fstream>

using json = nlohmann::json;

DanData DanLoader::load(const std::string& rootPath) {
    DanData data;
    std::string path = rootPath + "danCourses.json";

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        LOG_WARN("DanLoader", "danCourses.json not found: %s", path.c_str());
        return data;
    }

    json j;
    try {
        ifs >> j;
    } catch (...) {
        LOG_ERROR("DanLoader", "Failed to parse danCourses.json");
        return data;
    }

    if (!j.contains("courses") || !j["courses"].is_array()) return data;

    for (auto& c : j["courses"]) {
        DanCourse course;
        course.id         = c.value("id",         "");
        course.name       = c.value("name",       "");
        course.visible    = c.value("visible",    true);
        course.startGauge = c.value("startGauge", 100.0);

        std::string gt = c.value("gaugeType", "NORMAL");
        if      (gt == "EASY")    course.gaugeType = DanGaugeType::EASY;
        else if (gt == "HARD")    course.gaugeType = DanGaugeType::HARD;
        else if (gt == "EX_HARD") course.gaugeType = DanGaugeType::EX_HARD;
        else if (gt == "DAN")     course.gaugeType = DanGaugeType::DAN;
        else                      course.gaugeType = DanGaugeType::NORMAL;

        if (c.contains("songs") && c["songs"].is_array()) {
            for (auto& s : c["songs"]) {
                std::string p = s.is_string() ? s.get<std::string>()
                                              : s.value("path", "");
                if (!p.empty()) course.songs.push_back({p});
            }
        }

        if (!course.id.empty() && !course.songs.empty())
            data.courses.push_back(std::move(course));
    }

    LOG_INFO("DanLoader", "Loaded %zu courses", data.courses.size());
    return data;
}
