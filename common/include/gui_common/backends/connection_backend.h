#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gui::common
{

struct PortInfo
{
    std::string portPath;
    std::string friendlyName;
    std::string hardwareId;
};

class ConnectionBackend
{
public:
    using DataCallback = std::function<void(const std::uint8_t *, std::size_t)>;
    using DisconnectCallback = std::function<void()>;

    virtual ~ConnectionBackend() = default;

    virtual std::vector<PortInfo> refreshPorts() = 0;
    virtual bool connect(const std::string &portPath) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual void setDataCallback(DataCallback callback) = 0;
    virtual void setDisconnectCallback(DisconnectCallback callback) = 0;

    virtual bool write(const std::uint8_t *data, std::size_t size) = 0;
};

} // namespace gui::common
