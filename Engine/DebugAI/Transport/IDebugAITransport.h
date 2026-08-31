#pragma once

#include <string>

class IDebugAITransport {
public:
    virtual ~IDebugAITransport() = default;

    virtual bool Start(const std::string& endpoint) = 0;
    virtual void Stop() = 0;
    virtual void Poll() = 0;
    virtual bool TryReceive(std::string& outMessage) = 0;
    virtual bool Send(const std::string& message) = 0;
    virtual bool IsRunning() const = 0;
    virtual const std::string& LastError() const = 0;
};
