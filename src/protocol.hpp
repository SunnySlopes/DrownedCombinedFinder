#pragma once

#include "storage.hpp"

namespace riverfinder_native {
/** 桌面协议请求体映射结果；deferExport等壳级字段不进入引擎Config。 */
struct DesktopRequest {
    Config config;
    bool fullWorld = false;
    bool deferExport = false;
};

/**
 * 把契约v3的config对象映射成后端请求：严格校验、展开fullWorld。
 * 校验失败抛std::invalid_argument，由协议层转成failed(invalid-config)。
 */
DesktopRequest parseDesktopRequest(const Json &config);

/** 桌面协议v3：stdin命令，stdout只写JSONL事件，诊断写stderr；返回进程退出码。 */
int desktop_protocol();
}
